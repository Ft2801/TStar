/*
 * SPCC.cpp - Spectrophotometric Color Calibration
 *
 * Algorithm ported from the Python SFCC reference implementation.
 *
 * Calibration pipeline (invoked by SPCCDialog via QtConcurrent::run):
 *   1.  loadTStarFits()          - Read SEDs, filters, and sensor QE curves
 *                                  from tstar_data.fits.
 *   2.  buildSystemThroughput()  - T_sys = T_filter * T_QE * T_LP1 * T_LP2
 *                                  computed independently per RGB channel.
 *   3.  aperturePhotometry()     - Background-subtracted circular aperture
 *                                  with surrounding sky annulus.
 *   4.  picklesMatchForSimbad()  - Map SIMBAD spectral type strings to
 *                                  Pickles SED library entries.
 *   5.  trapz()                  - Integrate SED * T_sys on the common
 *                                  wavelength grid to obtain synthetic fluxes.
 *   6.  fitColorModel()          - Evaluate slope-only, affine, and quadratic
 *                                  models; select by lowest RMS residual.
 *   7.  applyColorModel()        - Per-pixel polynomial color correction.
 *   8.  computeGradientSurface() - (Optional) Fit a polynomial surface to the
 *       applyGradientSurface()     differential-magnitude residuals and remove
 *                                  the resulting chromatic gradient.
 */

#include "SPCC.h"
#include "photometry/CatalogClient.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <fitsio.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// Suppress Clang warnings about GNU zero-argument variadic macro extensions
// used internally by Qt's logging macros.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

Q_LOGGING_CATEGORY(lcSPCC, "spcc")

// =============================================================================
// Anonymous namespace - file-private helper functions
// =============================================================================

namespace {

// -----------------------------------------------------------------------------
// vectorMedianImpl
//
// Compute the median of a vector using nth_element partial sort.
// The vector is passed by value so the caller's data is never modified.
// -----------------------------------------------------------------------------
double vectorMedianImpl(std::vector<double> v)
{
    if (v.empty())
        return 0.0;

    const size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + n / 2, v.end());

    if (n % 2 == 1)
        return v[n / 2];

    // Even number of elements: average the two central values.
    const double hi = v[n / 2];
    std::nth_element(v.begin(), v.begin() + n / 2 - 1, v.end());
    return (v[n / 2 - 1] + hi) * 0.5;
}

// -----------------------------------------------------------------------------
// leastSquares
//
// Solve the overdetermined linear system A * c = b in the least-squares sense
// using normal equations: (A^T * A) * c = A^T * b.
//
// Gauss elimination with partial pivoting is used for numerical stability.
// This is adequate for the small systems encountered here (k <= 10 columns).
//
// Parameters:
//   A  - m-by-k design matrix (m observations, k unknowns)
//   b  - m-element right-hand side vector
// Returns:
//   c  - k-element coefficient vector
// -----------------------------------------------------------------------------
std::vector<double> leastSquares(const std::vector<std::vector<double>>& A,
                                 const std::vector<double>&               b)
{
    const int m = static_cast<int>(A.size());
    const int k = static_cast<int>(A[0].size());

    // Accumulate the normal equations: AtA * c = Atb
    std::vector<std::vector<double>> AtA(k, std::vector<double>(k, 0.0));
    std::vector<double>              Atb(k, 0.0);

    for (int i = 0; i < m; ++i)
    {
        for (int j = 0; j < k; ++j)
        {
            Atb[j] += A[i][j] * b[i];
            for (int l = 0; l < k; ++l)
                AtA[j][l] += A[i][j] * A[i][l];
        }
    }

    // Forward elimination with partial pivoting
    for (int col = 0; col < k; ++col)
    {
        // Find the row with the largest absolute value in this column
        int pivot = col;
        for (int row = col + 1; row < k; ++row)
        {
            if (std::fabs(AtA[row][col]) > std::fabs(AtA[pivot][col]))
                pivot = row;
        }

        std::swap(AtA[col], AtA[pivot]);
        std::swap(Atb[col], Atb[pivot]);

        if (std::fabs(AtA[col][col]) < 1e-15)
            continue;  // Singular or near-singular column; skip

        const double inv = 1.0 / AtA[col][col];
        for (int row = col + 1; row < k; ++row)
        {
            const double f = AtA[row][col] * inv;
            for (int l = col; l < k; ++l)
                AtA[row][l] -= f * AtA[col][l];
            Atb[row] -= f * Atb[col];
        }
    }

    // Back-substitution
    std::vector<double> c(k, 0.0);
    for (int i = k - 1; i >= 0; --i)
    {
        if (std::fabs(AtA[i][i]) < 1e-15)
            continue;

        double sum = Atb[i];
        for (int j = i + 1; j < k; ++j)
            sum -= AtA[i][j] * c[j];

        c[i] = sum / AtA[i][i];
    }

    return c;
}

} // anonymous namespace

// =============================================================================
// SPCC::detectAndConvertToAngstrom
//
// Mirrors Python _ensure_angstrom(): if the median of the wavelength array
// falls in the range [250, 2000], the values are assumed to be in nm and are
// multiplied by 10 to convert to Angstrom.
// =============================================================================
bool SPCC::detectAndConvertToAngstrom(std::vector<double>& wl)
{
    if (wl.empty())
        return false;

    const double med = vectorMedian(wl);

    if (med >= 250.0 && med <= 2000.0)
    {
        for (double& v : wl)
            v *= 10.0;
        return true;
    }

    return false;
}

// =============================================================================
// SPCC::vectorMedian  (public wrapper around the file-private implementation)
// =============================================================================
double SPCC::vectorMedian(std::vector<double> v)
{
    return vectorMedianImpl(std::move(v));
}

// =============================================================================
// SPCC::findCurve
//
// Linear search for a spectral curve object by name within a list.
// Returns a pointer to the matching entry, or nullptr if not found.
// =============================================================================
const SPCCObject* SPCC::findCurve(const std::vector<SPCCObject>& list,
                                  const QString&                  name)
{
    for (const SPCCObject& obj : list)
    {
        if (obj.name == name)
            return &obj;
    }
    return nullptr;
}

// =============================================================================
// SPCC::loadTStarFits
//
// Read every BinaryTableHDU from <dataPath>/tstar_data.fits and populate the
// provided SPCCDataStore. The CTYPE FITS header keyword determines the category:
//
//   "SED"    -> out.sed_list    (EXTNAME = Pickles type, e.g. "A0V")
//   "FILTER" -> out.filter_list (band-pass curves; columns WAVELENGTH + THROUGHPUT)
//   "SENSOR" -> out.sensor_list (QE curves;        columns WAVELENGTH + QE/THROUGHPUT)
//
// For SED entries the value column may be named "FLUX" or "THROUGHPUT".
// Wavelengths are converted to Angstrom when they appear to be in nm.
// =============================================================================
bool SPCC::loadTStarFits(const QString& dataPath, SPCCDataStore& out)
{
    // Resolve the FITS file path; fall back to the parent directory if not found
    // directly in dataPath.
    QString fitsPath = dataPath + "/tstar_data.fits";
    if (!QFile::exists(fitsPath))
        fitsPath = QFileInfo(dataPath).dir().filePath("tstar_data.fits");

    if (!QFile::exists(fitsPath))
    {
        qCWarning(lcSPCC) << "tstar_data.fits not found in" << dataPath;
        return false;
    }

    fitsfile* fptr   = nullptr;
    int       status = 0;

    if (fits_open_file(&fptr, fitsPath.toLocal8Bit().constData(), READONLY, &status))
    {
        qCWarning(lcSPCC) << "CFITSIO could not open" << fitsPath << "status=" << status;
        return false;
    }

    int num_hdus = 0;
    fits_get_num_hdus(fptr, &num_hdus, &status);

    int loaded = 0;

    // Iterate over all HDUs, skipping the primary (index 1) and any non-binary-table HDUs.
    for (int hdu_idx = 2; hdu_idx <= num_hdus; ++hdu_idx)
    {
        int hdutype = 0;
        fits_movabs_hdu(fptr, hdu_idx, &hdutype, &status);

        if (status != 0 || hdutype != BINARY_TBL)
        {
            status = 0;
            continue;
        }

        // Read the extension name and category type from the header
        char extname[FLEN_VALUE] = "";
        char ctype[FLEN_VALUE]   = "";
        fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status);
        fits_read_key(fptr, TSTRING, "CTYPE",   ctype,   nullptr, &status);

        if (status != 0)
        {
            status = 0;
            continue;
        }

        const QString name     = QString(extname).trimmed().remove('\'');
        const QString ctype_up = QString(ctype).replace("'", "").trimmed().toUpper();

        if (name.isEmpty() || ctype_up.isEmpty())
            continue;

        if (ctype_up != "SED" && ctype_up != "FILTER" && ctype_up != "SENSOR")
            continue;

        // Locate the WAVELENGTH column (mandatory for all HDU types)
        int col_wl = 0;
        if (fits_get_colnum(fptr, CASEINSEN, (char*)"WAVELENGTH", &col_wl, &status))
        {
            status = 0;
            continue;
        }

        // Determine the primary value column name for this HDU type:
        //   SED    -> "FLUX"
        //   SENSOR -> "QE"
        //   FILTER -> "THROUGHPUT"
        int         col_val  = 0;
        const char* val_col  = (ctype_up == "SED")    ? "FLUX"
                             : (ctype_up == "SENSOR") ? "QE"
                                                      : "THROUGHPUT";

        if (fits_get_colnum(fptr, CASEINSEN, (char*)val_col, &col_val, &status))
        {
            // Primary column not found; try alternative column names before giving up.
            status = 0;
            const char* alt1 = "THROUGHPUT";
            const char* alt2 = (ctype_up == "SENSOR") ? "FLUX" : "QE";

            if (fits_get_colnum(fptr, CASEINSEN, (char*)alt1, &col_val, &status))
            {
                status = 0;
                if (fits_get_colnum(fptr, CASEINSEN, (char*)alt2, &col_val, &status))
                {
                    status = 0;
                    continue;
                }
            }
        }

        long num_rows = 0;
        fits_get_num_rows(fptr, &num_rows, &status);

        if (status != 0 || num_rows <= 0)
        {
            status = 0;
            continue;
        }

        // Read wavelength and value columns into contiguous arrays
        std::vector<double> wl(num_rows), vals(num_rows);
        double nullval = 0.0;
        int    anynul  = 0;
        fits_read_col(fptr, TDOUBLE, col_wl,  1, 1, num_rows, &nullval, wl.data(),   &anynul, &status);
        fits_read_col(fptr, TDOUBLE, col_val, 1, 1, num_rows, &nullval, vals.data(), &anynul, &status);

        if (status != 0)
        {
            status = 0;
            continue;
        }

        // Convert nm to Angstrom if the median suggests nm units
        detectAndConvertToAngstrom(wl);

        // Ensure wavelength array is sorted in ascending order
        if (wl.size() > 1 && wl.front() > wl.back())
        {
            std::reverse(wl.begin(),   wl.end());
            std::reverse(vals.begin(), vals.end());
        }

        // Populate and store the SPCCObject
        SPCCObject obj;
        obj.name          = name;
        obj.model         = name;
        obj.arrays_loaded = true;
        obj.x             = std::move(wl);
        obj.y             = std::move(vals);

        if (ctype_up == "SED")
        {
            obj.type = WB_REF;
            out.sed_list.push_back(std::move(obj));
        }
        else if (ctype_up == "FILTER" && !name.contains("OSC", Qt::CaseInsensitive))
        {
            obj.type = MONO_FILTER;
            out.filter_list.push_back(std::move(obj));
        }
        else
        {
            obj.type = name.contains("OSC", Qt::CaseInsensitive) ? OSC_SENSOR : MONO_SENSOR;
            out.sensor_list.push_back(std::move(obj));
        }

        ++loaded;
    }

    fits_close_file(fptr, &status);

    qCInfo(lcSPCC) << "Loaded" << loaded << "HDUs from" << fitsPath
                   << "(" << out.sed_list.size()    << "SEDs,"
                   <<         out.filter_list.size() << "filters,"
                   <<         out.sensor_list.size() << "sensors)";

    return loaded > 0;
}

// =============================================================================
// SPCC::loadTStarDatabase
//
// Load the TStar JSON spectral database from a directory tree. Each JSON file
// may contain a single object or an array of objects. The "type" field routes
// each entry to the appropriate list in the SPCCDataStore.
//
// Expected subdirectory layout:
//   <dbPath>/mono_filters/
//   <dbPath>/mono_sensors/
//   <dbPath>/osc_filters/
//   <dbPath>/osc_sensors/
//   <dbPath>/wb_refs/
// =============================================================================
bool SPCC::loadTStarDatabase(const QString& dbPath, SPCCDataStore& out)
{
    if (!QDir(dbPath).exists())
    {
        qCWarning(lcSPCC) << "TStar database directory not found:" << dbPath;
        return false;
    }

    const QStringList subdirs = {"mono_filters", "mono_sensors",
                                 "osc_filters",  "osc_sensors", "wb_refs"};
    int loadedFiles  = 0;
    int totalObjects = 0;

    for (const QString& sub : subdirs)
    {
        QDir dir(dbPath + "/" + sub);
        if (!dir.exists())
            continue;

        const QFileInfoList list = dir.entryInfoList(QStringList() << "*.json", QDir::Files);

        for (const QFileInfo& info : list)
        {
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;

            const QByteArray   data = f.readAll();
            f.close();

            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull())
                continue;

            // Accept both a top-level array and a top-level single object
            QJsonArray arr;
            if (doc.isArray())
                arr = doc.array();
            else if (doc.isObject())
                arr.append(doc.object());

            for (int i = 0; i < arr.size(); ++i)
            {
                const QJsonObject jobj = arr[i].toObject();
                if (jobj.isEmpty())
                    continue;

                SPCCObject obj;
                obj.name  = jobj.value("name").toString().trimmed().remove('\'');
                obj.model = jobj.value("model").toString().trimmed().remove('\'');

                if (obj.name.isEmpty())
                    continue;

                // Parse type string
                const QString typeStr = jobj.value("type").toString().toUpper();
                if      (typeStr == "MONO_SENSOR") obj.type = MONO_SENSOR;
                else if (typeStr == "OSC_SENSOR")  obj.type = OSC_SENSOR;
                else if (typeStr == "MONO_FILTER") obj.type = MONO_FILTER;
                else if (typeStr == "OSC_FILTER")  obj.type = OSC_FILTER;
                else if (typeStr == "OSC_LPF")     obj.type = OSC_LPF;
                else if (typeStr == "WB_REF")      obj.type = WB_REF;
                else continue;

                // Parse wavelength array
                const QJsonObject wlObj  = jobj.value("wavelength").toObject();
                const QJsonArray  wlArr  = wlObj.value("value").toArray();

                // Parse value (throughput / flux) array
                const QJsonObject valObj = jobj.value("values").toObject();
                const QJsonArray  valArr = valObj.value("value").toArray();

                // "range" specifies the full-scale value of the data.
                // If range > 1 (e.g. 100 for percentage data), normalize to [0, 1].
                const double valRange   = valObj.value("range").toDouble(1.0);
                const double normFactor = (valRange > 1.0) ? (1.0 / valRange) : 1.0;

                if (wlArr.isEmpty() || valArr.isEmpty() || wlArr.size() != valArr.size())
                    continue;

                obj.x.reserve(wlArr.size());
                obj.y.reserve(valArr.size());
                for (int j = 0; j < wlArr.size(); ++j)
                {
                    obj.x.push_back(wlArr[j].toDouble());
                    obj.y.push_back(valArr[j].toDouble() * normFactor);
                }

                // Convert to Angstrom based on the declared units field
                const QString units = wlObj.value("units").toString().toLower();
                if (units == "nm")
                {
                    for (double& v : obj.x) v *= 10.0;
                }
                else if (units == "micrometer" || units == "um")
                {
                    for (double& v : obj.x) v *= 10000.0;
                }

                // Fallback: auto-detect nm units from the median wavelength value
                detectAndConvertToAngstrom(obj.x);

                // Ensure wavelength array is sorted ascending
                if (obj.x.size() > 1 && obj.x.front() > obj.x.back())
                {
                    std::reverse(obj.x.begin(), obj.x.end());
                    std::reverse(obj.y.begin(), obj.y.end());
                }

                obj.arrays_loaded = true;

                // Route to the appropriate list based on object type
                if (obj.type == WB_REF)
                    out.sed_list.push_back(std::move(obj));
                else if (obj.type == MONO_SENSOR || obj.type == OSC_SENSOR)
                    out.sensor_list.push_back(std::move(obj));
                else
                    out.filter_list.push_back(std::move(obj));

                ++totalObjects;
            }

            ++loadedFiles;
        }
    }

    qCInfo(lcSPCC) << "Loaded TStar database from" << dbPath << ":"
                   << loadedFiles << "files," << totalObjects << "objects.";

    return totalObjects > 0;
}

// =============================================================================
// Name-list helpers  (used to populate dialog combo boxes)
// =============================================================================

QStringList SPCC::availableSEDs(const SPCCDataStore& store)
{
    QStringList result;
    for (const SPCCObject& o : store.sed_list)
        result << o.name;
    result.sort();
    return result;
}

QStringList SPCC::availableFilters(const SPCCDataStore& store)
{
    QStringList result;
    for (const SPCCObject& o : store.filter_list)
        result << o.name;
    result.sort();
    return result;
}

QStringList SPCC::availableSensors(const SPCCDataStore& store)
{
    QStringList result;
    for (const SPCCObject& o : store.sensor_list)
        result << o.name;
    result.sort();
    return result;
}

// =============================================================================
// SPCC::interpolateToGrid
//
// Linearly interpolate the curve (wl_aa, vals) onto the common 1 Angstrom grid
// spanning [WL_GRID_MIN_AA, WL_GRID_MAX_AA]. Points that fall outside the input
// wavelength range are assigned 0 (same as NumPy interp with left=0, right=0).
// =============================================================================
void SPCC::interpolateToGrid(const std::vector<double>& wl_aa,
                             const std::vector<double>& vals,
                             double out[WL_GRID_LEN])
{
    const int n_src = static_cast<int>(wl_aa.size());

    for (int i = 0; i < WL_GRID_LEN; ++i)
    {
        const double wl = WL_GRID_MIN_AA + i;  // 1 Angstrom step

        if (n_src == 0 || wl < wl_aa.front() || wl > wl_aa.back())
        {
            out[i] = 0.0;
            continue;
        }

        // Binary search for the bracketing index pair
        auto it = std::lower_bound(wl_aa.begin(), wl_aa.end(), wl);

        if (it == wl_aa.end())   { out[i] = vals.back();  continue; }
        if (it == wl_aa.begin()) { out[i] = vals.front(); continue; }

        const int    i2 = static_cast<int>(it - wl_aa.begin());
        const int    i1 = i2 - 1;
        const double dw = wl_aa[i2] - wl_aa[i1];

        if (std::fabs(dw) < 1e-15) { out[i] = vals[i1]; continue; }

        const double t = (wl - wl_aa[i1]) / dw;
        out[i] = std::max(0.0, vals[i1] * (1.0 - t) + vals[i2] * t);
    }
}

// =============================================================================
// SPCC::trapz
//
// Trapezoidal integral of the element-wise product f[i] * g[i] on the common
// 1 Angstrom grid (uniform spacing, h = 1).
// Mirrors Python: np.trapz(f * T_sys, x=wl_grid).
// =============================================================================
double SPCC::trapz(const double f[WL_GRID_LEN], const double g[WL_GRID_LEN])
{
    // With h = 1 Angstrom, the trapezoidal rule simplifies to:
    //   integral ~= 0.5 * sum_i (f[i]*g[i] + f[i+1]*g[i+1])
    double sum = 0.0;

    for (int i = 0; i < WL_GRID_LEN - 1; ++i)
        sum += f[i] * g[i] + f[i + 1] * g[i + 1];

    return sum * 0.5;
}

// =============================================================================
// SPCC::buildSystemThroughput
//
// Construct the total system throughput curve for one color channel:
//   T_sys = T_filter * T_sensor * T_LP1 * T_LP2
//
// Components whose name is "(None)" or empty contribute a flat response of 1.
// For OSC systems, per-channel curve variants are looked up first using the
// naming conventions "<Name> Red" / "<Name>Red" before falling back to the
// base name.
// =============================================================================
void SPCC::buildSystemThroughput(const SPCCDataStore& store,
                                 const QString&       filterName,
                                 const QString&       sensorName,
                                 const QString&       lp1Name,
                                 const QString&       lp2Name,
                                 const QString&       channel,
                                 double               T_sys[WL_GRID_LEN])
{
    // Initialize to unity (flat response)
    for (int i = 0; i < WL_GRID_LEN; ++i)
        T_sys[i] = 1.0;

    // Multiply T_sys in-place by one spectral component. Skipped when the name
    // is "(None)" or empty; a warning is logged if the name is non-trivial but
    // no matching curve can be found in the database.
    auto applyComponent = [&](const std::vector<SPCCObject>& list,
                              const QString&                  name)
    {
        if (name == "(None)" || name.isEmpty())
            return;

        const SPCCObject* obj = nullptr;

        if (!channel.isEmpty())
        {
            // Priority 1: "<Name> <Channel>" (e.g. "Baader R Red")
            obj = findCurve(list, name + " " + channel);
            // Priority 2: "<Name><Channel>" (e.g. "BaaderRRed")
            if (!obj)
                obj = findCurve(list, name + channel);
        }

        // Priority 3: exact base name match
        if (!obj)
            obj = findCurve(list, name);

        if (!obj || !obj->arrays_loaded)
        {
            if (name != "(None)")
                qCWarning(lcSPCC) << "Curve not found in database:" << name << "channel:" << channel;
            return;
        }

        double curve[WL_GRID_LEN];
        interpolateToGrid(obj->x, obj->y, curve);

        for (int i = 0; i < WL_GRID_LEN; ++i)
            T_sys[i] *= curve[i];
    };

    applyComponent(store.filter_list, filterName);
    applyComponent(store.sensor_list, sensorName);
    applyComponent(store.filter_list, lp1Name);
    applyComponent(store.filter_list, lp2Name);
}

// =============================================================================
// SPCC::picklesMatchForSimbad
//
// Map a SIMBAD spectral type string to a ranked list of matching Pickles SED
// EXTNAME values. Mirrors Python pickles_match_for_simbad().
//
// Matching is attempted in the following priority order:
//   1. Same letter class + same sub-type digit + same luminosity class
//   2. Same letter class + same sub-type digit (any luminosity)
//   3. Same letter class + same luminosity class (nearest digit)
//   4. Same letter class only (fallback)
// =============================================================================
QStringList SPCC::picklesMatchForSimbad(const QString&    simbadSp,
                                        const QStringList& availSEDs)
{
    if (simbadSp.isEmpty() || availSEDs.isEmpty())
        return {};

    const QString sp = simbadSp.trimmed().toUpper();

    // Parse the SIMBAD string into letter class, digit, and luminosity class
    static const QRegularExpression re("^([OBAFGKMLT])(\\d?)((I{1,3}|IV|V)?)");
    const QRegularExpressionMatch   m  = re.match(sp);

    if (!m.hasMatch())
        return {};

    const QString letterClass = m.captured(1);
    const QString digitPart   = m.captured(2);
    const QString lumPart     = m.captured(3);
    const bool    hasDigit    = !digitPart.isEmpty();
    const bool    hasLum      = !lumPart.isEmpty();
    const int     digitVal    = hasDigit ? digitPart.toInt() : -1;

    // Parse each available SED name into the same components
    struct ParsedSED
    {
        QString extname, letter, digit, lum;
        int     digitVal = -1;
    };

    static const QRegularExpression sedRe("^([OBAFGKMLT])(\\d?)((I{1,3}|IV|V)?)");
    std::vector<ParsedSED> parsed;

    for (const QString& ext : availSEDs)
    {
        const QRegularExpressionMatch sm = sedRe.match(ext.toUpper());
        if (!sm.hasMatch())
            continue;

        ParsedSED p;
        p.extname  = ext;
        p.letter   = sm.captured(1);
        p.digit    = sm.captured(2);
        p.lum      = sm.captured(3);
        p.digitVal = p.digit.isEmpty() ? -1 : p.digit.toInt();
        parsed.push_back(p);
    }

    // Predicate helpers
    auto sameClass = [&](const ParsedSED& p) { return p.letter   == letterClass; };
    auto sameLum   = [&](const ParsedSED& p) { return p.lum      == lumPart;    };
    auto sameDigit = [&](const ParsedSED& p) { return p.digitVal == digitVal;   };

    // Build a sorted name list from the nearest-digit candidates
    auto pickNearest = [&](const std::vector<ParsedSED>& candidates) -> QStringList
    {
        if (candidates.empty())
            return {};

        if (!hasDigit)
        {
            QStringList r;
            for (const ParsedSED& p : candidates)
                r << p.extname;
            r.sort();
            return r;
        }

        // Find the smallest digit distance from the target
        int bestDist = INT_MAX;
        for (const ParsedSED& p : candidates)
        {
            if (p.digitVal < 0)
                continue;
            bestDist = std::min(bestDist, std::abs(p.digitVal - digitVal));
        }

        QStringList r;
        for (const ParsedSED& p : candidates)
        {
            if (std::abs(p.digitVal - digitVal) == bestDist)
                r << p.extname;
        }
        r.sort();
        return r;
    };

    if (hasDigit && hasLum)
    {
        // Priority 1: letter + digit + luminosity
        std::vector<ParsedSED> c1;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p) && sameDigit(p)) c1.push_back(p);

        if (!c1.empty())
        {
            QStringList r;
            for (const ParsedSED& p : c1) r << p.extname;
            return r;
        }

        // Priority 2: letter + digit (any luminosity)
        std::vector<ParsedSED> c2;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameDigit(p)) c2.push_back(p);
        if (!c2.empty()) return pickNearest(c2);

        // Priority 3: letter + luminosity (nearest digit)
        std::vector<ParsedSED> c3;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p)) c3.push_back(p);
        if (!c3.empty()) return pickNearest(c3);
    }
    else if (hasDigit && !hasLum)
    {
        // Priority 2 path: letter + any digit, pick nearest
        std::vector<ParsedSED> c;
        for (const ParsedSED& p : parsed)
            if (sameClass(p)) c.push_back(p);
        return pickNearest(c);
    }
    else if (!hasDigit && hasLum)
    {
        // Priority 3 path: letter + luminosity
        std::vector<ParsedSED> c;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p)) c.push_back(p);

        if (!c.empty())
        {
            QStringList r;
            for (const ParsedSED& p : c) r << p.extname;
            return r;
        }
    }

    // Fallback (priority 4): same letter class only
    QStringList r;
    for (const ParsedSED& p : parsed)
        if (sameClass(p)) r << p.extname;
    r.sort();
    return r;
}

// =============================================================================
// SPCC::inferTypeFromBpRp
//
// Map a Gaia DR3 BP-RP color index to an approximate Pickles Main Sequence (V)
// spectral type. The thresholds are derived from the empirical relation:
//   B-V ~= 0.393 + 0.475*(BP-RP) - 0.055*(BP-RP)^2
// =============================================================================
QString SPCC::inferTypeFromBpRp(double bp_rp)
{
    if (!std::isfinite(bp_rp))
        return QString();

    if (bp_rp < -0.35) return "O5V";
    if (bp_rp < -0.25) return "B0V";
    if (bp_rp < -0.10) return "B5V";
    if (bp_rp <  0.10) return "A0V";
    if (bp_rp <  0.25) return "A5V";
    if (bp_rp <  0.40) return "F0V";
    if (bp_rp <  0.55) return "F5V";
    if (bp_rp <  0.65) return "G0V";
    if (bp_rp <  0.75) return "G2V";  // Solar analog
    if (bp_rp <  0.85) return "G5V";
    if (bp_rp <  1.00) return "K0V";
    if (bp_rp <  1.30) return "K5V";
    if (bp_rp <  1.60) return "M0V";
    return "M5V";
}

// =============================================================================
// SPCC::aperturePhotometry
//
// Background-subtracted circular aperture photometry on an RGB float32 image.
// Mirrors Python measure_star_rgb_photometry():
//   mu_bg    = ann_sum / ann_area          (mean background per pixel)
//   star_sum = raw_sum - mu_bg * ap_area   (background-free star flux)
//
// Image layout: packed RGB, row-major, values in [0, 1].
//   pixel (x, y) -> base index = (y * width + x) * 3
// =============================================================================
PhotometryResult SPCC::aperturePhotometry(const float* img_float,
                                          int    width,  int    height,
                                          double cx,     double cy,
                                          double r,      double r_in, double r_out)
{
    PhotometryResult res;

    const double r2    = r     * r;
    const double rin2  = r_in  * r_in;
    const double rout2 = r_out * r_out;

    // Bounding boxes for the aperture and annulus regions
    const int x0_ap  = std::max(0,       static_cast<int>(std::floor(cx - r)));
    const int x1_ap  = std::min(width,   static_cast<int>(std::ceil(cx  + r))  + 1);
    const int y0_ap  = std::max(0,       static_cast<int>(std::floor(cy - r)));
    const int y1_ap  = std::min(height,  static_cast<int>(std::ceil(cy  + r))  + 1);

    const int x0_ann = std::max(0,       static_cast<int>(std::floor(cx - r_out)));
    const int x1_ann = std::min(width,   static_cast<int>(std::ceil(cx  + r_out)) + 1);
    const int y0_ann = std::max(0,       static_cast<int>(std::floor(cy - r_out)));
    const int y1_ann = std::min(height,  static_cast<int>(std::ceil(cy  + r_out)) + 1);

    // Accumulate annulus pixel sums for background estimation
    double ann_R    = 0.0, ann_G    = 0.0, ann_B    = 0.0;
    double ann_area = 0.0;

    for (int y = y0_ann; y < y1_ann; ++y)
    {
        for (int x = x0_ann; x < x1_ann; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            const double d2 = dx * dx + dy * dy;

            if (d2 >= rin2 && d2 <= rout2)
            {
                const size_t idx = (static_cast<size_t>(y) * width + x) * 3;
                ann_R    += img_float[idx];
                ann_G    += img_float[idx + 1];
                ann_B    += img_float[idx + 2];
                ann_area += 1.0;
            }
        }
    }

    if (ann_area < 1.0)
        return res;  // No background pixels available; result is invalid

    // Mean background level per pixel in each channel
    const double mu_R = ann_R / ann_area;
    const double mu_G = ann_G / ann_area;
    const double mu_B = ann_B / ann_area;

    // Accumulate aperture pixel sums (raw, including background)
    double raw_R    = 0.0, raw_G = 0.0, raw_B = 0.0;
    double ap_area  = 0.0;
    bool   any_invalid = false;

    for (int y = y0_ap; y < y1_ap && !any_invalid; ++y)
    {
        for (int x = x0_ap; x < x1_ap && !any_invalid; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;

            if (dx * dx + dy * dy <= r2)
            {
                const size_t idx = (static_cast<size_t>(y) * width + x) * 3;
                raw_R    += img_float[idx];
                raw_G    += img_float[idx + 1];
                raw_B    += img_float[idx + 2];
                ap_area  += 1.0;
            }
        }
    }

    if (ap_area < 1.0 || any_invalid)
        return res;

    // Background-subtracted star flux: star_sum = raw_sum - mu_bg * ap_area
    res.R_star = raw_R - mu_R * ap_area;
    res.G_star = raw_G - mu_G * ap_area;
    res.B_star = raw_B - mu_B * ap_area;

    res.R_bg = mu_R;
    res.G_bg = mu_G;
    res.B_bg = mu_B;

    // A result is valid only when all star fluxes are finite and strictly positive
    res.valid = std::isfinite(res.R_star) && std::isfinite(res.G_star) &&
                std::isfinite(res.B_star) &&
                res.R_star > 0.0 && res.G_star > 0.0 && res.B_star > 0.0;

    return res;
}

// =============================================================================
// SPCC::rmsFrac
//
// Compute the root-mean-square fractional residual between predicted and
// expected values:
//   rms = sqrt( mean( ((pred_i / exp_i) - 1)^2 ) )
//
// Samples where |exp_i| < 1e-30 are excluded to avoid division by zero.
// =============================================================================
double SPCC::rmsFrac(const std::vector<double>& pred,
                     const std::vector<double>& exp_vals)
{
    const size_t n = pred.size();
    if (n == 0)
        return std::numeric_limits<double>::infinity();

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        if (std::fabs(exp_vals[i]) < 1e-30)
            continue;

        const double v = pred[i] / exp_vals[i] - 1.0;
        sum += v * v;
    }

    return std::sqrt(sum / n);
}

// =============================================================================
// SPCC::polyFit2
//
// Fit a quadratic polynomial y = a*x^2 + b*x + c to the provided data points
// using the leastSquares() normal-equations solver.
// Returns false when fewer than 3 data points are available.
// =============================================================================
bool SPCC::polyFit2(const std::vector<double>& x,
                    const std::vector<double>& y,
                    double& a, double& b, double& c)
{
    const int n = static_cast<int>(x.size());
    if (n < 3)
        return false;

    // Design matrix: each row is [x^2, x, 1]
    std::vector<std::vector<double>> A(n, std::vector<double>(3));
    for (int i = 0; i < n; ++i)
    {
        A[i][0] = x[i] * x[i];
        A[i][1] = x[i];
        A[i][2] = 1.0;
    }

    const std::vector<double> c3 = leastSquares(A, y);
    a = c3[0];
    b = c3[1];
    c = c3[2];

    return true;
}

// =============================================================================
// SPCC::fitColorModel
//
// Evaluate three polynomial models against the provided (measured, expected)
// color ratio pairs and return the model that achieves the lowest combined
// RMS fractional residual across the R/G and B/G channels.
//
// Mirrors the Python model-selection block in run_spcc().
//
// Models:
//   0 - slope-only: meas = slope * exp
//   1 - affine:     meas = slope * exp + intercept
//   2 - quadratic:  meas = a * exp^2 + b * exp + c  (requires n >= 6)
//
// The coefficients are stored in the form [a, b, c] where the evaluation
// is: a*x^2 + b*x + c.  For slope-only, c = 0; for affine, a = 0.
// =============================================================================
CalibrationModel SPCC::fitColorModel(const std::vector<double>& meas_RG,
                                     const std::vector<double>& exp_RG,
                                     const std::vector<double>& meas_BG,
                                     const std::vector<double>& exp_BG)
{
    CalibrationModel best;
    const int        n   = static_cast<int>(meas_RG.size());
    if (n == 0)
        return best;

    const double eps = 1e-12;

    // -------------------------------------------------------------------------
    // Model 0: slope-only  (meas = slope * exp)
    // Optimal slope: sum(meas * exp) / sum(exp^2)
    // -------------------------------------------------------------------------
    auto slopeOnly = [&](const std::vector<double>& ex,
                         const std::vector<double>& my) -> double
    {
        double num = 0.0, den = 0.0;
        for (int i = 0; i < n; ++i)
        {
            num += ex[i] * my[i];
            den += ex[i] * ex[i];
        }
        return (den > eps) ? num / den : 1.0;
    };

    const double mR_s = slopeOnly(exp_RG, meas_RG);
    const double mB_s = slopeOnly(exp_BG, meas_BG);

    std::vector<double> predR_s(n), predB_s(n);
    for (int i = 0; i < n; ++i)
    {
        predR_s[i] = mR_s * exp_RG[i];
        predB_s[i] = mB_s * exp_BG[i];
    }
    const double rms_s = rmsFrac(predR_s, meas_RG) + rmsFrac(predB_s, meas_BG);

    // -------------------------------------------------------------------------
    // Model 1: affine  (meas = slope * exp + intercept)
    // -------------------------------------------------------------------------
    auto affineFit = [&](const std::vector<double>& ex,
                         const std::vector<double>& my,
                         double& slope, double& intercept)
    {
        std::vector<std::vector<double>> A(n, std::vector<double>(2));
        for (int i = 0; i < n; ++i)
        {
            A[i][0] = ex[i];
            A[i][1] = 1.0;
        }
        const std::vector<double> c2 = leastSquares(A, my);
        slope     = c2[0];
        intercept = c2[1];
    };

    double mR_a, bR_a, mB_a, bB_a;
    affineFit(exp_RG, meas_RG, mR_a, bR_a);
    affineFit(exp_BG, meas_BG, mB_a, bB_a);

    std::vector<double> predR_a(n), predB_a(n);
    for (int i = 0; i < n; ++i)
    {
        predR_a[i] = mR_a * exp_RG[i] + bR_a;
        predB_a[i] = mB_a * exp_BG[i] + bB_a;
    }
    const double rms_a = rmsFrac(predR_a, meas_RG) + rmsFrac(predB_a, meas_BG);

    // -------------------------------------------------------------------------
    // Model 2: quadratic  (meas = a*exp^2 + b*exp + c, requires n >= 6)
    // -------------------------------------------------------------------------
    double aR_q = 0.0, bR_q = 1.0, cR_q = 0.0;
    double aB_q = 0.0, bB_q = 1.0, cB_q = 0.0;
    double rms_q = std::numeric_limits<double>::infinity();

    if (n >= 6)
    {
        const bool ok = polyFit2(exp_RG, meas_RG, aR_q, bR_q, cR_q) &&
                        polyFit2(exp_BG, meas_BG, aB_q, bB_q, cB_q);
        if (ok)
        {
            std::vector<double> predR_q(n), predB_q(n);
            for (int i = 0; i < n; ++i)
            {
                predR_q[i] = aR_q * exp_RG[i] * exp_RG[i] + bR_q * exp_RG[i] + cR_q;
                predB_q[i] = aB_q * exp_BG[i] * exp_BG[i] + bB_q * exp_BG[i] + cB_q;
            }
            rms_q = rmsFrac(predR_q, meas_RG) + rmsFrac(predB_q, meas_BG);
        }
    }

    // -------------------------------------------------------------------------
    // Select the model with the lowest combined RMS residual
    // -------------------------------------------------------------------------
    int    best_idx = 0;
    double best_rms = rms_s;

    if (rms_a < best_rms) { best_rms = rms_a; best_idx = 1; }
    if (rms_q < best_rms) { best_rms = rms_q; best_idx = 2; }

    CalibrationModel model;
    model.rms_total = best_rms;

    // Store coefficients in [a, b, c] layout (a*x^2 + b*x + c).
    // slope-only stores as {0, slope, 0}; affine as {0, slope, intercept}.
    if (best_idx == 0)
    {
        model.kind       = MODEL_SLOPE_ONLY;
        model.coeff_R[0] = 0.0;  model.coeff_R[1] = mR_s; model.coeff_R[2] = 0.0;
        model.coeff_B[0] = 0.0;  model.coeff_B[1] = mB_s; model.coeff_B[2] = 0.0;
    }
    else if (best_idx == 1)
    {
        model.kind       = MODEL_AFFINE;
        model.coeff_R[0] = 0.0;  model.coeff_R[1] = mR_a; model.coeff_R[2] = bR_a;
        model.coeff_B[0] = 0.0;  model.coeff_B[1] = mB_a; model.coeff_B[2] = bB_a;
    }
    else
    {
        model.kind       = MODEL_QUADRATIC;
        model.coeff_R[0] = aR_q; model.coeff_R[1] = bR_q; model.coeff_R[2] = cR_q;
        model.coeff_B[0] = aB_q; model.coeff_B[1] = bB_q; model.coeff_B[2] = cB_q;
    }

    return model;
}

// =============================================================================
// SPCC::applyColorModel
//
// Apply the chosen polynomial color model to every pixel of the image in-place.
//
// The model was fitted as: Measured = f(Expected).
// To correct the image we evaluate f at the unity ratio (Expected = 1.0) to
// find the predicted measured value at that reference point, then use its
// reciprocal as the corrective multiplier:
//   multiplier = 1.0 / f(1.0)
//
// The correction is applied relative to the per-channel median pivot so that
// the background level is preserved:
//   R' = pivot_avg + (R - pivot_R) * mR
//   G' = pivot_avg + (G - pivot_G)
//   B' = pivot_avg + (B - pivot_B) * mB
//
// Multipliers are clamped to [0.25, 4.0]. Output is clamped to [0, 1].
// Mirrors Python: R' = pivot_R + (R - pivot_R) * mR.
// =============================================================================
void SPCC::applyColorModel(float* img_float,
                           int    width,   int    height,
                           const CalibrationModel& model,
                           double pivot_R, double pivot_G, double pivot_B)
{
    const size_t n_pixels = static_cast<size_t>(width) * height;

    // Neutral background level: average of the three channel pivots.
    // Shifting all channels to this common level removes any pre-existing
    // color tint in the background.
    const double pivot_avg = (pivot_R + pivot_G + pivot_B) / 3.0;

    // Evaluate the model at the reference ratio (Expected = 1.0) to obtain
    // the predicted measured value at that point, then invert it.
    const double predR = polyEval(model.coeff_R, 1.0);
    const double predB = polyEval(model.coeff_B, 1.0);

    double mR = (std::fabs(predR) > 1e-9) ? (1.0 / predR) : 1.0;
    double mB = (std::fabs(predB) > 1e-9) ? (1.0 / predB) : 1.0;

    // Clamp multipliers to a physically reasonable range
    mR = std::max(0.25, std::min(4.0, mR));
    mB = std::max(0.25, std::min(4.0, mB));

    for (size_t i = 0; i < n_pixels; ++i)
    {
        const size_t base = i * 3;

        const double R = img_float[base];
        const double G = img_float[base + 1];
        const double B = img_float[base + 2];

        const double R_new = pivot_avg + (R - pivot_R) * mR;
        const double G_new = pivot_avg + (G - pivot_G);
        const double B_new = pivot_avg + (B - pivot_B) * mB;

        img_float[base]     = static_cast<float>(std::max(0.0, std::min(1.0, R_new)));
        img_float[base + 1] = static_cast<float>(std::max(0.0, std::min(1.0, G_new)));
        img_float[base + 2] = static_cast<float>(std::max(0.0, std::min(1.0, B_new)));
    }
}

// =============================================================================
// SPCC::fitPoly2Surface
//
// Fit a second-degree bivariate polynomial surface:
//   z = c0 + c1*x + c2*y + c3*x^2 + c4*x*y + c5*y^2
// to the scattered control points (pts, vals) using least squares, then
// evaluate it on the full pixel grid.
//
// Mirrors Python compute_gradient_map(..., method="poly2").
// =============================================================================
std::vector<double> SPCC::fitPoly2Surface(
        const std::vector<std::array<double, 2>>& pts,
        const std::vector<double>&                vals,
        int width, int height)
{
    const int n = static_cast<int>(pts.size());

    // Design matrix: one row per control point, six columns for poly2 basis
    std::vector<std::vector<double>> A(n, std::vector<double>(6));
    for (int i = 0; i < n; ++i)
    {
        const double x = pts[i][0];
        const double y = pts[i][1];
        A[i] = {1.0, x, y, x * x, x * y, y * y};
    }

    const std::vector<double> c = leastSquares(A, vals);

    // Evaluate the fitted surface on every pixel of the output grid
    std::vector<double> surf(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row)
    {
        for (int col = 0; col < width; ++col)
        {
            const double x = col;
            const double y = row;
            surf[static_cast<size_t>(row) * width + col] =
                    c[0] + c[1]*x + c[2]*y + c[3]*x*x + c[4]*x*y + c[5]*y*y;
        }
    }

    return surf;
}

// =============================================================================
// SPCC::fitPoly3Surface
//
// Fit a third-degree bivariate polynomial surface (10 terms) to scattered
// control points using least squares, then evaluate it on the full pixel grid.
//
// Mirrors Python compute_gradient_map(..., method="poly3").
// =============================================================================
std::vector<double> SPCC::fitPoly3Surface(
        const std::vector<std::array<double, 2>>& pts,
        const std::vector<double>&                vals,
        int width, int height)
{
    const int n = static_cast<int>(pts.size());

    // Design matrix: 10 columns for the complete degree-3 polynomial basis
    std::vector<std::vector<double>> A(n, std::vector<double>(10));
    for (int i = 0; i < n; ++i)
    {
        const double x = pts[i][0];
        const double y = pts[i][1];
        A[i] = {1.0, x, y,
                x*x, x*y, y*y,
                x*x*x, x*x*y, x*y*y, y*y*y};
    }

    const std::vector<double> c = leastSquares(A, vals);

    // Evaluate the fitted surface on every pixel of the output grid
    std::vector<double> surf(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row)
    {
        for (int col = 0; col < width; ++col)
        {
            const double x = col;
            const double y = row;
            surf[static_cast<size_t>(row) * width + col] =
                    c[0] + c[1]*x  + c[2]*y  +
                    c[3]*x*x + c[4]*x*y + c[5]*y*y +
                    c[6]*x*x*x + c[7]*x*x*y + c[8]*x*y*y + c[9]*y*y*y;
        }
    }

    return surf;
}

// =============================================================================
// SPCC::computeGradientSurface
//
// Compute per-channel multiplicative correction surfaces using the
// differential-magnitude approach. Mirrors Python run_gradient_extraction().
//
// For each matched star:
//   dm[ch] = measured_mag[ch] - expected_mag[ch]
//
// The dm values are median-centered, fitted with a polynomial spatial surface,
// re-centered, and clamped to +/-max_allowed_mag. The final correction scale is:
//   scale[ch][pixel] = 10^(-0.4 * dm_surface[pixel])
//
// The image is corrected by: corrected[ch] = img[ch] / scale[ch].
// =============================================================================
GradientSurface SPCC::computeGradientSurface(
        const float* /*img_float*/,
        int width, int height,
        const std::vector<EnrichedMatch>& matches,
        const double /*T_sys_R*/[WL_GRID_LEN],
        const double /*T_sys_G*/[WL_GRID_LEN],
        const double /*T_sys_B*/[WL_GRID_LEN],
        const QString& method,
        double max_allowed_mag)
{
    GradientSurface surf;
    surf.width  = width;
    surf.height = height;

    // A minimum of 6 control points is required for a well-determined poly2 fit
    if (matches.size() < 6)
        return surf;

    const int    n   = static_cast<int>(matches.size());
    const double eps = 1e-30;

    // Compute differential magnitudes for each matched star
    std::vector<std::array<double, 2>> pts(n);
    std::vector<double> dmR(n), dmG(n), dmB(n);

    for (int i = 0; i < n; ++i)
    {
        const EnrichedMatch& em = matches[i];
        pts[i] = {em.x_img, em.y_img};

        // Instrumental magnitude (log of background-subtracted star flux)
        const double mag_R_meas = (em.R_meas > eps) ? -2.5 * std::log10(em.R_meas) : 0.0;
        const double mag_G_meas = (em.G_meas > eps) ? -2.5 * std::log10(em.G_meas) : 0.0;
        const double mag_B_meas = (em.B_meas > eps) ? -2.5 * std::log10(em.B_meas) : 0.0;

        // Expected synthetic magnitude from the SED integral
        const double mag_R_exp = (em.S_star_R > eps) ? -2.5 * std::log10(em.S_star_R) : 0.0;
        const double mag_G_exp = (em.S_star_G > eps) ? -2.5 * std::log10(em.S_star_G) : 0.0;
        const double mag_B_exp = (em.S_star_B > eps) ? -2.5 * std::log10(em.S_star_B) : 0.0;

        dmR[i] = mag_R_meas - mag_R_exp;
        dmG[i] = mag_G_meas - mag_G_exp;
        dmB[i] = mag_B_meas - mag_B_exp;
    }

    // Subtract the median from each dm vector so the surface has zero mean
    // (removes the global photometric zero-point offset before fitting)
    auto center = [&](std::vector<double>& dm)
    {
        const double med = vectorMedianImpl(dm);
        for (double& v : dm) v -= med;
    };

    center(dmR);
    center(dmG);
    center(dmB);

    // Fit a polynomial surface to the differential magnitudes
    auto fitSurface = [&](const std::vector<double>& dm) -> std::vector<double>
    {
        if (method.toLower() == "poly2")
            return fitPoly2Surface(pts, dm, width, height);
        else
            return fitPoly3Surface(pts, dm, width, height);  // default: poly3
    };

    std::vector<double> sR = fitSurface(dmR);
    std::vector<double> sG = fitSurface(dmG);
    std::vector<double> sB = fitSurface(dmB);

    // Re-center the fitted surfaces to remove any fitting-induced DC offset
    center(sR);
    center(sG);
    center(sB);

    // Clamp peak amplitude to prevent overcorrection
    auto clampSurface = [&](std::vector<double>& s)
    {
        double peak = 0.0;
        for (double v : s)
            peak = std::max(peak, std::fabs(v));

        if (peak > max_allowed_mag)
        {
            const double scale = max_allowed_mag / peak;
            for (double& v : s) v *= scale;
        }
    };

    clampSurface(sR);
    clampSurface(sG);
    clampSurface(sB);

    // Convert differential magnitudes to multiplicative scale factors:
    //   scale = 10^(-0.4 * dm)
    // Dividing the image by these factors removes the residual chromatic gradient.
    const size_t np = static_cast<size_t>(width) * height;
    surf.R.resize(np);
    surf.G.resize(np);
    surf.B.resize(np);

    for (size_t i = 0; i < np; ++i)
    {
        surf.R[i] = static_cast<float>(std::pow(10.0, -0.4 * sR[i]));
        surf.G[i] = static_cast<float>(std::pow(10.0, -0.4 * sG[i]));
        surf.B[i] = static_cast<float>(std::pow(10.0, -0.4 * sB[i]));
    }

    surf.valid = true;
    return surf;
}

// =============================================================================
// SPCC::applyGradientSurface
//
// Divide each pixel of img_float by the corresponding scale factor from the
// gradient correction surface. Values are clamped to [0, 1].
//   corrected[ch] = img[ch] / max(scale[ch], 1e-8)
// =============================================================================
void SPCC::applyGradientSurface(float* img_float, int width, int height,
                                const GradientSurface& surf)
{
    if (!surf.valid)
        return;

    const size_t np    = static_cast<size_t>(width) * height;
    const float  eps_f = 1e-8f;

    for (size_t i = 0; i < np; ++i)
    {
        const size_t base = i * 3;

        img_float[base]     = std::max(0.f, std::min(1.f, img_float[base]     / std::max(surf.R[i], eps_f)));
        img_float[base + 1] = std::max(0.f, std::min(1.f, img_float[base + 1] / std::max(surf.G[i], eps_f)));
        img_float[base + 2] = std::max(0.f, std::min(1.f, img_float[base + 2] / std::max(surf.B[i], eps_f)));
    }
}

// =============================================================================
// SPCC::calibrateWithStarList
//
// Full spectrophotometric color calibration pipeline.
// Called from SPCCDialog via QtConcurrent::run().
//
// Pipeline stages:
//   1.  Load spectral database (FITS + JSON)
//   2.  Build per-channel system throughput curves
//   3.  Integrate the white-reference SED against T_sys for normalization
//   4.  Pre-compute Pickles template integrals over an anchor BP-RP grid
//   5.  Extract float32 pixel data from the ImageBuffer
//   6.  Main star loop: aperture photometry + ratio accumulation
//   7.  Fit the color model (slope-only / affine / quadratic)
//   8.  Apply the correction to the image
//   9.  Optional: compute and apply chromatic gradient surface
//   10. Pack the corrected data into a new ImageBuffer and return
// =============================================================================
SPCCResult SPCC::calibrateWithStarList(const ImageBuffer&             buf,
                                       const SPCCParams&              params,
                                       const std::vector<StarRecord>& starRecords)
{
    SPCCResult result;
    result.stars_found = static_cast<int>(starRecords.size());

    // Convenience lambda for progress reporting
    auto progress = [&](int pct, const QString& msg)
    {
        if (params.progressCallback)
            params.progressCallback(pct, msg);
    };

    // =========================================================================
    // Stage 1: Load spectral database
    //
    // Both tstar_data.fits and the TStar-spcc-database JSON files are loaded
    // because the UI combo boxes may expose entries from either source. Loading
    // both guarantees that every user-selected filter/sensor name can be
    // resolved by buildSystemThroughput().
    // =========================================================================
    progress(2, "Loading spectral database...");

    SPCCDataStore store;
    const bool fitsOk = loadTStarFits(params.dataPath, store);
    const bool dbOk   = loadTStarDatabase(params.dataPath + "/TStar-spcc-database", store);

    if (!fitsOk && !dbOk)
    {
        result.error_msg = "Failed to load spectral database from: " + params.dataPath;
        return result;
    }

    // =========================================================================
    // Stage 2: Build system throughput curves for R, G, B
    // =========================================================================
    if (params.cancelFlag && params.cancelFlag->load())
    { result.error_msg = "Cancelled"; return result; }

    progress(5, "Building system throughput curves...");

    double T_sys_R[WL_GRID_LEN], T_sys_G[WL_GRID_LEN], T_sys_B[WL_GRID_LEN];

    buildSystemThroughput(store, params.rFilter, params.sensor,
                          params.lpFilter1, params.lpFilter2, "Red",   T_sys_R);
    buildSystemThroughput(store, params.gFilter, params.sensor,
                          params.lpFilter1, params.lpFilter2, "Green", T_sys_G);
    buildSystemThroughput(store, params.bFilter, params.sensor,
                          params.lpFilter1, params.lpFilter2, "Blue",  T_sys_B);

    // Log the selected equipment configuration
    QString log = "Built system throughput:\n";
    auto logChan = [&](const QString& ch, const QString& f,
                       const QString& s, const QString& lp1, const QString& lp2)
    {
        log += QString("  - %1: Filter=%2, Sensor=%3, LP=%4/%5\n").arg(ch, f, s, lp1, lp2);
    };
    logChan("Red",   params.rFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    logChan("Green", params.gFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    logChan("Blue",  params.bFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    result.log_msg += log;
    qCInfo(lcSPCC).noquote() << log;

    // =========================================================================
    // Stage 3: Integrate the white-reference SED against T_sys
    //
    // These integrals normalize the per-star expected ratios so that a star
    // whose SED matches the white reference produces eRG = 1.0 and eBG = 1.0.
    // =========================================================================
    progress(8, "Integrating reference SED...");

    const SPCCObject* refSED = findCurve(store.sed_list, params.whiteRef);
    if (!refSED)
    {
        result.error_msg = "Reference SED not found: " + params.whiteRef;
        return result;
    }

    double refGrid[WL_GRID_LEN];
    interpolateToGrid(refSED->x, refSED->y, refGrid);

    const double S_ref_R = trapz(refGrid, T_sys_R);
    const double S_ref_G = trapz(refGrid, T_sys_G);
    const double S_ref_B = trapz(refGrid, T_sys_B);

    qCInfo(lcSPCC) << "Reference SED integrals: R=" << S_ref_R
                   << " G=" << S_ref_G << " B=" << S_ref_B;

    // =========================================================================
    // Stage 4: Pre-compute Pickles SED integrals over an anchor BP-RP grid
    //
    // Rather than integrating each star's SED individually, we build a sparse
    // table of (BP-RP, S_R, S_G, S_B) anchor points and linearly interpolate
    // between them for each star. This avoids repeated expensive integrations.
    // =========================================================================
    if (params.cancelFlag && params.cancelFlag->load())
    { result.error_msg = "Cancelled"; return result; }

    progress(10, "Pre-computing Pickles SED integrals...");

    const QStringList allSEDNames = availableSEDs(store);

    struct AnchorPoint
    {
        QString type;
        double  bp_rp;
        double  sumR, sumG, sumB;
    };

    // Representative Main Sequence templates spanning the full O-to-M color range
    std::vector<AnchorPoint> anchors =
    {
        {"O5V", -0.33, 0,0,0}, {"B0V", -0.24, 0,0,0}, {"B5V", -0.11, 0,0,0},
        {"A0V",  0.00, 0,0,0}, {"A5V",  0.14, 0,0,0}, {"F0V",  0.31, 0,0,0},
        {"F5V",  0.44, 0,0,0}, {"G0V",  0.59, 0,0,0}, {"G2V",  0.64, 0,0,0},
        {"G5V",  0.76, 0,0,0}, {"K0V",  0.93, 0,0,0}, {"K5V",  1.15, 0,0,0},
        {"M0V",  1.45, 0,0,0}, {"M5V",  1.84, 0,0,0}
    };

    // Case-insensitive substring search for an SED by spectral type pattern
    auto findSedByPattern = [&](const QString& pattern) -> const SPCCObject*
    {
        for (const auto& s : store.sed_list)
        {
            if (s.name.contains(pattern, Qt::CaseInsensitive))
                return &s;
        }
        return nullptr;
    };

    for (auto& a : anchors)
    {
        const SPCCObject* sed = findSedByPattern(a.type);

        if (sed && sed->arrays_loaded)
        {
            double grid[WL_GRID_LEN];
            interpolateToGrid(sed->x, sed->y, grid);
            a.sumR = trapz(grid, T_sys_R);
            a.sumG = trapz(grid, T_sys_G);
            a.sumB = trapz(grid, T_sys_B);
        }
        else
        {
            // Fall back to G2V (solar analog) if the requested template is absent
            const SPCCObject* fallback = findSedByPattern("G2V");
            if (fallback)
            {
                double grid[WL_GRID_LEN];
                interpolateToGrid(fallback->x, fallback->y, grid);
                a.sumR = trapz(grid, T_sys_R);
                a.sumG = trapz(grid, T_sys_G);
                a.sumB = trapz(grid, T_sys_B);
            }
        }
    }

    // Sort anchors by BP-RP to enable binary-search interpolation
    std::sort(anchors.begin(), anchors.end(),
              [](const AnchorPoint& a, const AnchorPoint& b) { return a.bp_rp < b.bp_rp; });

    // =========================================================================
    // Stage 5: Extract float32 pixel data from the ImageBuffer
    // =========================================================================
    progress(15, "Preparing image data...");

    const int    W        = buf.width();
    const int    H        = buf.height();
    const size_t n_pixels = static_cast<size_t>(W) * H;

    // Create a working float32 copy; the original buffer is not modified until
    // the corrected data is written back in stage 10.
    std::vector<float> img_f32(n_pixels * 3);
    {
        const float* src = buf.data().data();
        std::copy(src, src + n_pixels * 3, img_f32.data());
    }

    // =========================================================================
    // Stage 6: Main star loop - aperture photometry and ratio accumulation
    //
    // For each star in the input list:
    //   a. Compute the aperture geometry from the SEP semi-major axis.
    //   b. Run background-subtracted aperture photometry.
    //   c. Determine the expected color ratios via BP-RP anchor interpolation.
    //   d. Accumulate (measured, expected) ratio pairs for model fitting.
    // =========================================================================
    if (params.cancelFlag && params.cancelFlag->load())
    { result.error_msg = "Cancelled"; return result; }

    progress(20, "Performing aperture photometry on matched stars...");

    std::vector<double>        meas_RG, meas_BG, exp_RG, exp_BG;
    std::vector<EnrichedMatch> enriched;

    int    logCount          = 0;
    int    validColorCount   = 0;
    int    fallbackColorCount = 0;
    double minExpRG = 1e9, maxExpRG = -1e9;
    double minExpBG = 1e9, maxExpBG = -1e9;

    for (const StarRecord& sr : starRecords)
    {
        if (params.cancelFlag && params.cancelFlag->load())
        { result.error_msg = "Cancelled"; return result; }

        // Aperture geometry mirrors the Python scaling rules:
        //   r    = clip(2.5 * semi_a,  2.0, 12.0)
        //   r_in = clip(3.0 * r,       6.0, 40.0)
        //   rout = clip(5.0 * r, r_in+2.0, 60.0)
        const double a    = sr.semi_a;
        const double r    = std::max(2.0, std::min(12.0, 2.5 * a));
        const double rin  = std::max(6.0, std::min(40.0, 3.0 * r));
        const double rout = std::max(rin + 2.0, std::min(60.0, 5.0 * r));

        const PhotometryResult phot = aperturePhotometry(
                img_f32.data(), W, H, sr.x_img, sr.y_img, r, rin, rout);

        if (!phot.valid)
            continue;

        // Determine the BP-RP color index, falling back to G2V (solar analog)
        // when no Gaia catalog match is available.
        double bprp = sr.gaia_bp_rp;
        if (!std::isfinite(bprp))
        {
            bprp = 0.64;  // G2V default
            ++fallbackColorCount;
        }
        else
        {
            ++validColorCount;
        }

        // Interpolate the anchor table to obtain expected synthetic fluxes
        double S_sr = 0.0, S_sg = 0.0, S_sb = 0.0;

        if (bprp <= anchors.front().bp_rp)
        {
            S_sr = anchors.front().sumR;
            S_sg = anchors.front().sumG;
            S_sb = anchors.front().sumB;
        }
        else if (bprp >= anchors.back().bp_rp)
        {
            S_sr = anchors.back().sumR;
            S_sg = anchors.back().sumG;
            S_sb = anchors.back().sumB;
        }
        else
        {
            // Linear interpolation between the two bracketing anchor points
            for (size_t i = 0; i < anchors.size() - 1; ++i)
            {
                if (bprp >= anchors[i].bp_rp && bprp < anchors[i + 1].bp_rp)
                {
                    const double t = (bprp - anchors[i].bp_rp) /
                                     (anchors[i + 1].bp_rp - anchors[i].bp_rp);
                    S_sr = anchors[i].sumR * (1.0 - t) + anchors[i + 1].sumR * t;
                    S_sg = anchors[i].sumG * (1.0 - t) + anchors[i + 1].sumG * t;
                    S_sb = anchors[i].sumB * (1.0 - t) + anchors[i + 1].sumB * t;
                    break;
                }
            }
        }

        if (!std::isfinite(S_sr) || !std::isfinite(S_sg) || !std::isfinite(S_sb))
            continue;
        if (S_sg <= 0.0 || S_sr <= 0.0 || S_sb <= 0.0)
            continue;

        // Normalize expected ratios by the white-reference SED ratios.
        // A star that perfectly matches the white reference will yield eRG = eBG = 1.
        const double eRG = (S_sr / S_sg) / (S_ref_R / S_ref_G);
        const double eBG = (S_sb / S_sg) / (S_ref_B / S_ref_G);

        // Measured color ratios from aperture photometry
        const double mRG = phot.R_star / phot.G_star;
        const double mBG = phot.B_star / phot.G_star;

        if (!std::isfinite(eRG) || !std::isfinite(eBG))  continue;
        if (!std::isfinite(mRG) || !std::isfinite(mBG))  continue;

        meas_RG.push_back(mRG);  meas_BG.push_back(mBG);
        exp_RG.push_back(eRG);   exp_BG.push_back(eBG);

        minExpRG = std::min(minExpRG, eRG);  maxExpRG = std::max(maxExpRG, eRG);
        minExpBG = std::min(minExpBG, eBG);  maxExpBG = std::max(maxExpBG, eBG);

        // Log per-star diagnostics for the first 20 accepted stars
        if (logCount < 20)
        {
            qCInfo(lcSPCC) << "[SPCC] Star match:" << sr.pickles_match
                           << " eRG:" << eRG << " eBG:" << eBG
                           << " mRG:" << mRG << " mBG:" << mBG;
            ++logCount;
        }

        // Store enriched match record for gradient removal and diagnostic output
        EnrichedMatch em;
        em.x_img    = sr.x_img;   em.y_img    = sr.y_img;
        em.R_meas   = phot.R_star; em.G_meas   = phot.G_star; em.B_meas = phot.B_star;
        em.S_star_R = S_sr;        em.S_star_G = S_sg;         em.S_star_B = S_sb;
        em.exp_RG   = eRG;         em.exp_BG   = eBG;
        em.meas_RG  = mRG;         em.meas_BG  = mBG;
        em.r_ap     = r;           em.r_in     = rin;          em.r_out    = rout;
        enriched.push_back(em);
    }

    qCInfo(lcSPCC) << "[SPCC] Color data quality:" << validColorCount
                   << "stars with catalog BP-RP,"
                   << fallbackColorCount << "stars using default G2V.";
    qCInfo(lcSPCC) << "[SPCC] Expected ratio spread: RG ["
                   << minExpRG << "," << maxExpRG << "]  BG ["
                   << minExpBG << "," << maxExpBG << "]";

    result.stars_used = static_cast<int>(meas_RG.size());

    if (result.stars_used < 3)
    {
        result.error_msg = QString("Too few valid stars for calibration: %1 (need >= 3)")
                           .arg(result.stars_used);
        return result;
    }

    qCInfo(lcSPCC) << "[SPCC] Valid stars for model fit:" << result.stars_used;

    // =========================================================================
    // Stage 7: Fit the color calibration model
    // =========================================================================
    progress(60, "Fitting colour calibration model...");

    const CalibrationModel model = fitColorModel(meas_RG, exp_RG, meas_BG, exp_BG);
    result.model    = model;
    result.residual = model.rms_total;

    // Compute global slope-only multipliers for the linear application mode.
    // We fit: Measured = k * Expected, so the corrective gain is 1.0 / k.
    // k is estimated as sum(meas * exp) / sum(exp^2) (least-squares slope).
    double mR_slope = 1.0, mB_slope = 1.0;

    if (!meas_RG.empty())
    {
        double sum_me_R = 0.0, sum_ee_R = 0.0;
        double sum_me_B = 0.0, sum_ee_B = 0.0;

        for (size_t i = 0; i < meas_RG.size(); ++i)
        {
            sum_me_R += meas_RG[i] * exp_RG[i];  sum_ee_R += exp_RG[i] * exp_RG[i];
            sum_me_B += meas_BG[i] * exp_BG[i];  sum_ee_B += exp_BG[i] * exp_BG[i];
        }

        if (sum_ee_R > 0.0) mR_slope = sum_me_R / sum_ee_R;
        if (sum_ee_B > 0.0) mB_slope = sum_me_B / sum_ee_B;
    }

    const float kR = (mR_slope > 1e-9) ? static_cast<float>(1.0 / mR_slope) : 1.0f;
    const float kB = (mB_slope > 1e-9) ? static_cast<float>(1.0 / mB_slope) : 1.0f;

    result.white_balance_k[0] = kR;
    result.white_balance_k[1] = 1.0;
    result.white_balance_k[2] = kB;

    result.scaleR = kR;
    result.scaleG = 1.0;
    result.scaleB = kB;

    result.corrMatrix[0][0] = kR;
    result.corrMatrix[1][1] = 1.0;
    result.corrMatrix[2][2] = kB;

    // For non-linear mode, re-evaluate the scale factors at the reference point
    // (Expected = 1.0) using the fitted polynomial for display purposes only.
    if (!params.linearMode)
    {
        const double pR = polyEval(model.coeff_R, 1.0);
        const double pB = polyEval(model.coeff_B, 1.0);
        result.scaleR = (std::fabs(pR) > 1e-9) ? (1.0 / pR) : 1.0;
        result.scaleB = (std::fabs(pB) > 1e-9) ? (1.0 / pB) : 1.0;
    }

    // Populate per-star diagnostics for the scatter plot overlay
    for (const EnrichedMatch& em : enriched)
    {
        SPCCResult::DiagStar ds;
        ds.x_img   = em.x_img;    ds.y_img   = em.y_img;
        ds.meas_RG = em.meas_RG;  ds.meas_BG = em.meas_BG;
        ds.exp_RG  = em.exp_RG;   ds.exp_BG  = em.exp_BG;
        ds.is_inlier = true;
        result.diagnostics.push_back(ds);
    }

    // =========================================================================
    // Stage 8: Apply the color correction to the image
    // =========================================================================
    if (params.cancelFlag && params.cancelFlag->load())
    { result.error_msg = "Cancelled"; return result; }

    progress(75, "Applying colour calibration...");

    // Compute the per-channel median as the background pivot value
    std::vector<double> chanR(n_pixels), chanG(n_pixels), chanB(n_pixels);
    for (size_t i = 0; i < n_pixels; ++i)
    {
        chanR[i] = img_f32[i * 3];
        chanG[i] = img_f32[i * 3 + 1];
        chanB[i] = img_f32[i * 3 + 2];
    }

    const double pivot_R = vectorMedianImpl(chanR);
    const double pivot_G = vectorMedianImpl(chanG);
    const double pivot_B = vectorMedianImpl(chanB);

    if (params.linearMode)
    {
        // Linear application: simple per-channel multiplicative correction.
        // Formula: P'_ch = P_ch * k_ch + offset_ch
        // where offset aligns the corrected background to the green channel median.
        const float kr      = kR;
        const float kb      = kB;
        const float offsetR = static_cast<float>(pivot_G - pivot_R * kr);
        const float offsetB = static_cast<float>(pivot_G - pivot_B * kb);

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n_pixels); ++i)
        {
            const float r = img_f32[i * 3];
            const float g = img_f32[i * 3 + 1];
            const float b = img_f32[i * 3 + 2];

            img_f32[i * 3]     = std::clamp(r * kr + offsetR, 0.0f, 1.0f);
            img_f32[i * 3 + 1] = std::clamp(g,                0.0f, 1.0f);
            img_f32[i * 3 + 2] = std::clamp(b * kb + offsetB, 0.0f, 1.0f);
        }
    }
    else
    {
        // Non-linear polynomial application
        applyColorModel(img_f32.data(), W, H, model, pivot_R, pivot_G, pivot_B);
    }

    // =========================================================================
    // Stage 9: Optional chromatic gradient removal
    // =========================================================================
    if (params.runGradient && enriched.size() >= 6)
    {
        progress(82, "Computing chromatic gradient surface...");

        const GradientSurface gsurf = computeGradientSurface(
                img_f32.data(), W, H, enriched,
                T_sys_R, T_sys_G, T_sys_B,
                params.gradientMethod);

        if (gsurf.valid)
        {
            progress(90, "Applying gradient correction...");
            applyGradientSurface(img_f32.data(), W, H, gsurf);
        }
    }

    // =========================================================================
    // Stage 10: Write the corrected data into a new ImageBuffer and return
    // =========================================================================
    progress(95, "Finalising output image...");

    auto out = std::make_shared<ImageBuffer>(buf);  // Copy metadata and dimensions
    std::copy(img_f32.begin(), img_f32.end(), out->data().begin());
    result.modifiedBuffer = out;

    progress(100, "Done.");
    result.success = true;
    result.log_msg = QString("[SPCC] Calibration complete. Stars=%1, model=%2, RMS=%3")
                     .arg(result.stars_used)
                     .arg(model.kind == MODEL_SLOPE_ONLY ? "slope-only" :
                          model.kind == MODEL_AFFINE     ? "affine"     : "quadratic")
                     .arg(result.residual, 0, 'f', 4);

    qCInfo(lcSPCC) << result.log_msg;
    return result;
}

// =============================================================================
// SPCC::bpRpFromType
//
// Return an approximate Gaia BP-RP color index for a given spectral type string.
// Used when a catalog entry has a spectral type but no direct BP-RP measurement.
// =============================================================================
double SPCC::bpRpFromType(const QString& spec)
{
    const QString s = spec.toUpper();

    if (s.startsWith("O"))   return -0.4;
    if (s.startsWith("B0"))  return -0.3;
    if (s.startsWith("B5"))  return -0.14;
    if (s.startsWith("B"))   return -0.2;
    if (s.startsWith("A0"))  return  0.0;
    if (s.startsWith("A5"))  return  0.15;
    if (s.startsWith("A"))   return  0.1;
    if (s.startsWith("F0"))  return  0.3;
    if (s.startsWith("F5"))  return  0.46;
    if (s.startsWith("F"))   return  0.4;
    if (s.startsWith("G0"))  return  0.58;
    if (s.startsWith("G2"))  return  0.64;
    if (s.startsWith("G5"))  return  0.78;
    if (s.startsWith("G"))   return  0.65;
    if (s.startsWith("K0"))  return  0.96;
    if (s.startsWith("K5"))  return  1.41;
    if (s.startsWith("K"))   return  1.1;
    if (s.startsWith("M0"))  return  1.84;
    if (s.startsWith("M5"))  return  2.80;
    if (s.startsWith("M"))   return  2.2;

    return 0.64;  // Default: G2V (solar analog)
}

// =============================================================================
// SPCC::calibrateWithCatalog
//
// Legacy entry point retained for compatibility with existing dialog code.
// Converts a vector of CatalogStar records into the StarRecord format expected
// by the primary calibration pipeline and delegates to calibrateWithStarList().
// =============================================================================
SPCCResult SPCC::calibrateWithCatalog(const ImageBuffer&              buf,
                                      const SPCCParams&               params,
                                      const std::vector<CatalogStar>& stars)
{
    // Load the spectral database to resolve spectral type -> Pickles SED mappings
    SPCCDataStore store;
    loadTStarFits(params.dataPath, store);
    const QStringList allSEDs = availableSEDs(store);

    std::vector<StarRecord> records;
    records.reserve(stars.size());

    for (const CatalogStar& cs : stars)
    {
        StarRecord sr;
        sr.ra          = cs.ra;
        sr.dec         = cs.dec;
        sr.x_img       = 0.0;   // Pixel coordinates are not available in CatalogStar
        sr.y_img       = 0.0;
        sr.semi_a      = 2.0;   // Default aperture size when SEP data is unavailable
        sr.gaia_bp_rp  = cs.bp_rp;
        sr.gaia_gmag   = (cs.magV > 0) ? cs.magV : cs.magB;

        if (!sr.sp_type.isEmpty())
        {
            // Extract the leading letter class for display and fallback matching
            sr.sp_clean = sr.sp_type.left(1).toUpper();

            const QStringList candidates = picklesMatchForSimbad(sr.sp_type, allSEDs);
            if (!candidates.isEmpty())
                sr.pickles_match = candidates.first();
        }
        else if (cs.teff > 1000.0)
        {
            // Approximate BP-RP from effective temperature when direct BP-RP is absent
            if (std::isnan(sr.gaia_bp_rp))
            {
                if      (cs.teff > 30000) sr.gaia_bp_rp = -0.4;
                else if (cs.teff > 15000) sr.gaia_bp_rp = -0.15;
                else if (cs.teff >  9000) sr.gaia_bp_rp =  0.0;
                else if (cs.teff >  7000) sr.gaia_bp_rp =  0.3;
                else if (cs.teff >  6000) sr.gaia_bp_rp =  0.58;
                else if (cs.teff >  5500) sr.gaia_bp_rp =  0.65;
                else if (cs.teff >  5000) sr.gaia_bp_rp =  0.9;
                else if (cs.teff >  4000) sr.gaia_bp_rp =  1.3;
                else                      sr.gaia_bp_rp =  2.0;
            }
        }

        records.push_back(sr);
    }

    return calibrateWithStarList(buf, params, records);
}

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif