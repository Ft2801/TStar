/*
 * SPCC.cpp  —  Spectrophotometric Color Calibration
 *
 * Algorithm ported from the Python SFCC implementation.
 *
 * Pipeline (called by SPCCDialog via QtConcurrent::run):
 *   1. loadTStarFits()         — read SEDs / filters / sensors from tstar_data.fits.
 *   2. buildSystemThroughput() — T_sys = T_filter * T_QE * T_LP1 * T_LP2 per channel.
 *   3. aperturePhotometry()    — background-subtracted circular aperture + annulus.
 *   4. picklesMatchForSimbad() — map SIMBAD spectral type to Pickles SED name.
 *   5. trapz()                 — integrate SED * T_sys on the common wavelength grid.
 *   6. fitColorModel()         — compare slope-only / affine / quadratic models.
 *   7. applyColorModel()       — per-pixel polynomial colour correction.
 *   8. (optional) computeGradientSurface() + applyGradientSurface()
 *                              — differential-magnitude chromatic gradient removal.
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

Q_LOGGING_CATEGORY(lcSPCC, "spcc")

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Median of a sorted or unsorted vector (copy is intentional).
double vectorMedianImpl(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + n / 2, v.end());
    if (n % 2 == 1) return v[n / 2];
    double hi = v[n / 2];
    std::nth_element(v.begin(), v.begin() + n / 2 - 1, v.end());
    return (v[n / 2 - 1] + hi) * 0.5;
}

// Least-squares solve of an (m x k) system A * c = b, returns c.
// Uses normal equations (A^T A) c = A^T b — fine for k <= 10.
std::vector<double> leastSquares(const std::vector<std::vector<double>>& A,
                                  const std::vector<double>& b) {
    const int m = (int)A.size();
    const int k = (int)A[0].size();

    // Build A^T * A and A^T * b
    std::vector<std::vector<double>> AtA(k, std::vector<double>(k, 0.0));
    std::vector<double>              Atb(k, 0.0);

    for (int i = 0; i < m; ++i) {
        for (int j = 0; j < k; ++j) {
            Atb[j] += A[i][j] * b[i];
            for (int l = 0; l < k; ++l)
                AtA[j][l] += A[i][j] * A[i][l];
        }
    }

    // Gauss elimination with partial pivoting
    for (int col = 0; col < k; ++col) {
        // Pivot
        int pivot = col;
        for (int row = col + 1; row < k; ++row)
            if (std::fabs(AtA[row][col]) > std::fabs(AtA[pivot][col]))
                pivot = row;
        std::swap(AtA[col], AtA[pivot]);
        std::swap(Atb[col], Atb[pivot]);

        if (std::fabs(AtA[col][col]) < 1e-15) continue;

        double inv = 1.0 / AtA[col][col];
        for (int row = col + 1; row < k; ++row) {
            double f = AtA[row][col] * inv;
            for (int l = col; l < k; ++l)
                AtA[row][l] -= f * AtA[col][l];
            Atb[row] -= f * Atb[col];
        }
    }

    // Back-substitution
    std::vector<double> c(k, 0.0);
    for (int i = k - 1; i >= 0; --i) {
        if (std::fabs(AtA[i][i]) < 1e-15) continue;
        double sum = Atb[i];
        for (int j = i + 1; j < k; ++j) sum -= AtA[i][j] * c[j];
        c[i] = sum / AtA[i][i];
    }
    return c;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::detectAndConvertToAngstrom
// Mirrors Python _ensure_angstrom(): if median(wl) is in the range 250–2000,
// treat the values as nm and multiply by 10.
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::detectAndConvertToAngstrom(std::vector<double>& wl) {
    if (wl.empty()) return false;
    double med = vectorMedian(wl);
    if (med >= 250.0 && med <= 2000.0) {
        for (double& v : wl) v *= 10.0;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::vectorMedian  (public wrapper)
// ─────────────────────────────────────────────────────────────────────────────
double SPCC::vectorMedian(std::vector<double> v) {
    return vectorMedianImpl(std::move(v));
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::findCurve
// ─────────────────────────────────────────────────────────────────────────────
const SPCCObject* SPCC::findCurve(const std::vector<SPCCObject>& list,
                                   const QString& name) {
    for (const SPCCObject& obj : list)
        if (obj.name == name) return &obj;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::loadTStarFits
// Reads every BinaryTableHDU from <dataPath>/tstar_data.fits.
// CTYPE header key determines the category:
//   "SED"    -> sed_list    (EXTNAME = Pickles type, e.g. "A0V")
//   "FILTER" -> filter_list (band-pass curves; WAVELENGTH + THROUGHPUT columns)
//   "SENSOR" -> sensor_list (QE curves;        WAVELENGTH + THROUGHPUT columns)
// For SED entries, the value column may be named "FLUX" or "THROUGHPUT".
// Wavelengths are converted to Angstrom if they appear to be in nm.
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::loadTStarFits(const QString& dataPath, SPCCDataStore& out) {
    // Locate the FITS file
    QString fitsPath = dataPath + "/tstar_data.fits";
    if (!QFile::exists(fitsPath))
        fitsPath = QFileInfo(dataPath).dir().filePath("tstar_data.fits");
    if (!QFile::exists(fitsPath)) {
        qCWarning(lcSPCC) << "tstar_data.fits not found in" << dataPath;
        return false;
    }

    fitsfile* fptr = nullptr;
    int status = 0;
    if (fits_open_file(&fptr, fitsPath.toLocal8Bit().constData(), READONLY, &status)) {
        qCWarning(lcSPCC) << "CFITSIO could not open" << fitsPath << "status=" << status;
        return false;
    }

    int num_hdus = 0;
    fits_get_num_hdus(fptr, &num_hdus, &status);
    int loaded = 0;

    for (int hdu_idx = 2; hdu_idx <= num_hdus; ++hdu_idx) {
        int hdutype = 0;
        fits_movabs_hdu(fptr, hdu_idx, &hdutype, &status);
        if (status != 0 || hdutype != BINARY_TBL) { status = 0; continue; }

        // Read EXTNAME and CTYPE
        char extname[FLEN_VALUE] = "";
        char ctype[FLEN_VALUE]   = "";
        fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status);
        fits_read_key(fptr, TSTRING, "CTYPE",   ctype,   nullptr, &status);
        if (status != 0) { status = 0; continue; }

        const QString name  = QString(extname).trimmed().remove('\'');
        const QString ctype_up = QString(ctype).replace("'","").trimmed().toUpper();

        if (name.isEmpty() || ctype_up.isEmpty()) continue;
        if (ctype_up != "SED" && ctype_up != "FILTER" && ctype_up != "SENSOR") continue;

        // Locate WAVELENGTH column
        int col_wl = 0;
        if (fits_get_colnum(fptr, CASEINSEN, (char*)"WAVELENGTH", &col_wl, &status)) {
            status = 0; continue;
        }

        // Locate value column: FLUX for SED, QE or THROUGHPUT for SENSOR, THROUGHPUT for FILTER
        int col_val = 0;
        const char* val_col = (ctype_up == "SED") ? "FLUX" : (ctype_up == "SENSOR" ? "QE" : "THROUGHPUT");
        if (fits_get_colnum(fptr, CASEINSEN, (char*)val_col, &col_val, &status)) {
            // Fallback: try the other common column names
            status = 0;
            const char* alt1 = (ctype_up == "SED") ? "THROUGHPUT" : "THROUGHPUT";
            const char* alt2 = (ctype_up == "SENSOR") ? "FLUX" : "QE";
            if (fits_get_colnum(fptr, CASEINSEN, (char*)alt1, &col_val, &status)) {
                status = 0;
                if (fits_get_colnum(fptr, CASEINSEN, (char*)alt2, &col_val, &status)) {
                    status = 0; continue;
                }
            }
        }

        long num_rows = 0;
        fits_get_num_rows(fptr, &num_rows, &status);
        if (status != 0 || num_rows <= 0) { status = 0; continue; }

        // Read arrays
        std::vector<double> wl(num_rows), vals(num_rows);
        double nullval = 0.0;
        int    anynul  = 0;
        fits_read_col(fptr, TDOUBLE, col_wl,  1, 1, num_rows, &nullval, wl.data(),   &anynul, &status);
        fits_read_col(fptr, TDOUBLE, col_val,  1, 1, num_rows, &nullval, vals.data(), &anynul, &status);
        if (status != 0) { status = 0; continue; }

        // Convert nm -> Angstrom when needed
        detectAndConvertToAngstrom(wl);

        // Ensure wavelengths are monotonically increasing
        if (wl.size() > 1 && wl[0] > wl[wl.size()-1]) {
            std::reverse(wl.begin(), wl.end());
            std::reverse(vals.begin(), vals.end());
        }

        SPCCObject obj;
        obj.name          = name;
        obj.model         = name;
        obj.arrays_loaded = true;
        obj.x             = std::move(wl);
        obj.y             = std::move(vals);

        if (ctype_up == "SED") {
            obj.type = WB_REF;
            out.sed_list.push_back(std::move(obj));
        } else if (ctype_up == "FILTER") {
            obj.type = MONO_FILTER;
            out.filter_list.push_back(std::move(obj));
        } else {
            obj.type = MONO_SENSOR;
            out.sensor_list.push_back(std::move(obj));
        }
        ++loaded;
    }

    fits_close_file(fptr, &status);
    qCInfo(lcSPCC) << "Loaded" << loaded << "HDUs from" << fitsPath
                   << "(" << out.sed_list.size() << "SEDs,"
                   << out.filter_list.size() << "filters,"
                   << out.sensor_list.size() << "sensors)";
    return loaded > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::loadTStarDatabase
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::loadTStarDatabase(const QString& dbPath, SPCCDataStore& out) {
    if (!QDir(dbPath).exists()) {
        qCWarning(lcSPCC) << "TStar database directory not found:" << dbPath;
        return false;
    }

    QStringList subdirs = {"mono_filters", "mono_sensors", "osc_filters", "osc_sensors", "wb_refs"};
    int loadedFiles = 0;
    int totalObjects = 0;

    for (const QString& sub : subdirs) {
        QDir dir(dbPath + "/" + sub);
        if (!dir.exists()) continue;

        QStringList filters;
        filters << "*.json";
        QFileInfoList list = dir.entryInfoList(filters, QDir::Files);

        for (const QFileInfo& info : list) {
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;

            QByteArray data = f.readAll();
            f.close();

            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull()) continue;

            // Could be a single object or an array of objects
            QJsonArray arr;
            if (doc.isArray()) {
                arr = doc.array();
            } else if (doc.isObject()) {
                arr.append(doc.object());
            }

            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject jobj = arr[i].toObject();
                if (jobj.isEmpty()) continue;

                SPCCObject obj;
                obj.name  = jobj.value("name").toString().trimmed().remove('\'');
                obj.model = jobj.value("model").toString().trimmed().remove('\'');
                if (obj.name.isEmpty()) continue;

                QString typeStr = jobj.value("type").toString().toUpper();
                if (typeStr == "MONO_SENSOR") obj.type = MONO_SENSOR;
                else if (typeStr == "OSC_SENSOR")  obj.type = OSC_SENSOR;
                else if (typeStr == "MONO_FILTER") obj.type = MONO_FILTER;
                else if (typeStr == "OSC_FILTER")  obj.type = OSC_FILTER;
                else if (typeStr == "OSC_LPF")     obj.type = OSC_LPF;
                else if (typeStr == "WB_REF")      obj.type = WB_REF;
                else continue;

                // Wavelengths
                QJsonObject wlObj = jobj.value("wavelength").toObject();
                QJsonArray  wlArr = wlObj.value("value").toArray();
                // Values
                QJsonObject valObj = jobj.value("values").toObject();
                QJsonArray  valArr = valObj.value("value").toArray();
                // "range" indicates the full-scale value of the data (e.g. 100 means
                // values are percentages 0-100 and must be divided by 100 to get 0-1).
                const double valRange = valObj.value("range").toDouble(1.0);
                const double normFactor = (valRange > 1.0) ? (1.0 / valRange) : 1.0;

                if (wlArr.isEmpty() || valArr.isEmpty() || wlArr.size() != valArr.size()) continue;

                for (int j = 0; j < wlArr.size(); ++j) {
                    obj.x.push_back(wlArr[j].toDouble());
                    obj.y.push_back(valArr[j].toDouble() * normFactor);
                }

                // If units are nm, convert to Angstrom
                QString units = wlObj.value("units").toString().toLower();
                if (units == "nm") {
                    for (double& v : obj.x) v *= 10.0;
                } else if (units == "micrometer" || units == "um") {
                    for (double& v : obj.x) v *= 10000.0;
                }

                // Ensure Angstrom detection fallback
                detectAndConvertToAngstrom(obj.x);

                // Ensure monotonically increasing
                if (obj.x.size() > 1 && obj.x[0] > obj.x[obj.x.size()-1]) {
                    std::reverse(obj.x.begin(), obj.x.end());
                    std::reverse(obj.y.begin(), obj.y.end());
                }

                obj.arrays_loaded = true;

                if (obj.type == WB_REF)          out.sed_list.push_back(std::move(obj));
                else if (obj.type == MONO_SENSOR || obj.type == OSC_SENSOR) out.sensor_list.push_back(std::move(obj));
                else out.filter_list.push_back(std::move(obj));
                
                totalObjects++;
            }
            loadedFiles++;
        }
    }

    qCInfo(lcSPCC) << "Loaded TStar database from" << dbPath << ":" 
                   << loadedFiles << "files," << totalObjects << "objects.";
    return totalObjects > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Name-list helpers
// ─────────────────────────────────────────────────────────────────────────────
QStringList SPCC::availableSEDs(const SPCCDataStore& store) {
    QStringList r;
    for (const SPCCObject& o : store.sed_list) r << o.name;
    r.sort();
    return r;
}

QStringList SPCC::availableFilters(const SPCCDataStore& store) {
    QStringList r;
    for (const SPCCObject& o : store.filter_list) r << o.name;
    r.sort();
    return r;
}

QStringList SPCC::availableSensors(const SPCCDataStore& store) {
    QStringList r;
    for (const SPCCObject& o : store.sensor_list) r << o.name;
    r.sort();
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::interpolateToGrid
// Linear interpolation of (wl_aa, vals) onto the common 3000–11000 Å grid.
// Points outside the input range are filled with 0.
// Mirrors Python: np.interp(wl_grid, wl_o, tp_o, left=0.0, right=0.0)
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::interpolateToGrid(const std::vector<double>& wl_aa,
                              const std::vector<double>& vals,
                              double out[WL_GRID_LEN]) {
    const int n_src = (int)wl_aa.size();
    for (int i = 0; i < WL_GRID_LEN; ++i) {
        const double wl = WL_GRID_MIN_AA + i;   // 1 Å step

        if (n_src == 0 || wl < wl_aa.front() || wl > wl_aa.back()) {
            out[i] = 0.0;
            continue;
        }

        // Binary search for bracketing index
        auto it = std::lower_bound(wl_aa.begin(), wl_aa.end(), wl);
        if (it == wl_aa.end()) { out[i] = vals.back(); continue; }
        if (it == wl_aa.begin()) { out[i] = vals.front(); continue; }

        const int i2 = (int)(it - wl_aa.begin());
        const int i1 = i2 - 1;
        const double dw = wl_aa[i2] - wl_aa[i1];
        if (std::fabs(dw) < 1e-15) { out[i] = vals[i1]; continue; }
        const double t = (wl - wl_aa[i1]) / dw;
        out[i] = std::max(0.0, vals[i1] * (1.0 - t) + vals[i2] * t);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::trapz
// Trapezoidal integral of f[i]*g[i] on the 1-Å common grid.
// Mirrors Python _trapz(f * T_sys, x=wl_grid).
// ─────────────────────────────────────────────────────────────────────────────
double SPCC::trapz(const double f[WL_GRID_LEN], const double g[WL_GRID_LEN]) {
    // Step is 1 Å everywhere → simplified rule.
    double sum = 0.0;
    for (int i = 0; i < WL_GRID_LEN - 1; ++i)
        sum += (f[i] * g[i] + f[i+1] * g[i+1]);
    return sum * 0.5;   // h = 1 Å
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::buildSystemThroughput
// T_sys = T_filter * T_QE * T_LP1 * T_LP2
// Any name == "(None)" produces a flat 1 array for that component.
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::buildSystemThroughput(const SPCCDataStore& store,
                                  const QString& filterName,
                                  const QString& sensorName,
                                  const QString& lp1Name,
                                  const QString& lp2Name,
                                  const QString& channel,
                                  double T_sys[WL_GRID_LEN]) {
    // Initialize to 1.0
    for (int i = 0; i < WL_GRID_LEN; ++i) T_sys[i] = 1.0;

    auto applyComponent = [&](const std::vector<SPCCObject>& list,
                               const QString& name) {
        if (name == "(None)" || name.isEmpty()) return;
        
        const SPCCObject* obj = nullptr;
        if (!channel.isEmpty()) {
            // Priority 1: "Name Red", "Name Green", "Name Blue"
            obj = findCurve(list, name + " " + channel);
            // Priority 2: "NameRed", "NameGreen", "NameBlue"
            if (!obj) obj = findCurve(list, name + channel);
        }
        
        // Priority 3: Exact match or Mono curve
        if (!obj) obj = findCurve(list, name);

        if (!obj || !obj->arrays_loaded) {
            if (name != "(None)") {
                qCWarning(lcSPCC) << "Curve not found in database:" << name << "channel:" << channel;
            }
            return;
        }
        double curve[WL_GRID_LEN];
        interpolateToGrid(obj->x, obj->y, curve);
        for (int i = 0; i < WL_GRID_LEN; ++i) T_sys[i] *= curve[i];
    };

    applyComponent(store.filter_list, filterName);
    applyComponent(store.sensor_list, sensorName);
    applyComponent(store.filter_list, lp1Name);
    applyComponent(store.filter_list, lp2Name);
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::picklesMatchForSimbad
// Maps a SIMBAD spectral type string to a ranked list of Pickles SED names.
// Mirrors Python pickles_match_for_simbad().
//
// Matching rules (in priority order):
//   1. Same letter class + same digit + same luminosity class
//   2. Same letter class + same digit (any luminosity)
//   3. Same letter class + same luminosity (nearest digit)
//   4. Same letter class (any sub-type)
// ─────────────────────────────────────────────────────────────────────────────
QStringList SPCC::picklesMatchForSimbad(const QString& simbadSp,
                                         const QStringList& availSEDs) {
    if (simbadSp.isEmpty() || availSEDs.isEmpty()) return {};

    const QString sp = simbadSp.trimmed().toUpper();

    // Parse: letter class, optional digit, optional luminosity
    static const QRegularExpression re("^([OBAFGKMLT])(\\d?)((I{1,3}|IV|V)?)");
    const QRegularExpressionMatch m = re.match(sp);
    if (!m.hasMatch()) return {};

    const QString letterClass = m.captured(1);
    const QString digitPart   = m.captured(2);
    const QString lumPart     = m.captured(3);
    const bool hasDigit = !digitPart.isEmpty();
    const bool hasLum   = !lumPart.isEmpty();
    const int  digitVal = hasDigit ? digitPart.toInt() : -1;

    // Parse each available SED EXTNAME
    struct ParsedSED {
        QString extname, letter, digit, lum;
        int digitVal = -1;
    };

    static const QRegularExpression sedRe("^([OBAFGKMLT])(\\d?)((I{1,3}|IV|V)?)");
    std::vector<ParsedSED> parsed;
    for (const QString& ext : availSEDs) {
        const QRegularExpressionMatch sm = sedRe.match(ext.toUpper());
        if (!sm.hasMatch()) continue;
        ParsedSED p;
        p.extname = ext;
        p.letter  = sm.captured(1);
        p.digit   = sm.captured(2);
        p.lum     = sm.captured(3);
        p.digitVal = p.digit.isEmpty() ? -1 : p.digit.toInt();
        parsed.push_back(p);
    }

    // Filter helpers
    auto sameClass = [&](const ParsedSED& p){ return p.letter == letterClass; };
    auto sameLum   = [&](const ParsedSED& p){ return p.lum   == lumPart; };
    auto sameDigit = [&](const ParsedSED& p){ return p.digitVal == digitVal; };

    // Pick nearest digit
    auto pickNearest = [&](const std::vector<ParsedSED>& candidates) -> QStringList {
        if (candidates.empty()) return {};
        if (!hasDigit) {
            QStringList r;
            for (const ParsedSED& p : candidates) r << p.extname;
            r.sort();
            return r;
        }
        int bestDist = INT_MAX;
        for (const ParsedSED& p : candidates) {
            if (p.digitVal < 0) continue;
            bestDist = std::min(bestDist, std::abs(p.digitVal - digitVal));
        }
        QStringList r;
        for (const ParsedSED& p : candidates)
            if (std::abs(p.digitVal - digitVal) == bestDist) r << p.extname;
        r.sort();
        return r;
    };

    // Priority 1: letter + digit + luminosity
    if (hasDigit && hasLum) {
        std::vector<ParsedSED> c1;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p) && sameDigit(p)) c1.push_back(p);
        if (!c1.empty()) {
            QStringList r;
            for (const ParsedSED& p : c1) r << p.extname;
            return r;
        }
        // Priority 2: letter + digit (any lum)
        std::vector<ParsedSED> c2;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameDigit(p)) c2.push_back(p);
        if (!c2.empty()) return pickNearest(c2);
        // Priority 3: letter + lum (nearest digit)
        std::vector<ParsedSED> c3;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p)) c3.push_back(p);
        if (!c3.empty()) return pickNearest(c3);
    } else if (hasDigit && !hasLum) {
        std::vector<ParsedSED> c;
        for (const ParsedSED& p : parsed)
            if (sameClass(p)) c.push_back(p);
        return pickNearest(c);
    } else if (!hasDigit && hasLum) {
        std::vector<ParsedSED> c;
        for (const ParsedSED& p : parsed)
            if (sameClass(p) && sameLum(p)) c.push_back(p);
        if (!c.empty()) {
            QStringList r;
            for (const ParsedSED& p : c) r << p.extname;
            return r;
        }
    }

    // Fallback: same letter class only
    QStringList r;
    for (const ParsedSED& p : parsed)
        if (sameClass(p)) r << p.extname;
    r.sort();
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::inferTypeFromBpRp
// Maps Gaia BP-RP to an approximate Pickles Main Sequence (V) type.
// Conversion to B-V (~proxy): 0.393 + 0.475*(BP-RP) - 0.055*(BP-RP)^2
// ─────────────────────────────────────────────────────────────────────────────
QString SPCC::inferTypeFromBpRp(double bp_rp) {
    if (!std::isfinite(bp_rp)) return QString();
    
    // Improved table for more granular matching (Main Sequence V)
    if (bp_rp < -0.35) return "O5V";
    if (bp_rp < -0.25) return "B0V";
    if (bp_rp < -0.10) return "B5V";
    if (bp_rp <  0.10) return "A0V";
    if (bp_rp <  0.25) return "A5V";
    if (bp_rp <  0.40) return "F0V";
    if (bp_rp <  0.55) return "F5V";
    if (bp_rp <  0.65) return "G0V";
    if (bp_rp <  0.75) return "G2V"; // Sun-like
    if (bp_rp <  0.85) return "G5V";
    if (bp_rp <  1.00) return "K0V";
    if (bp_rp <  1.30) return "K5V";
    if (bp_rp <  1.60) return "M0V";
    return "M5V";
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::aperturePhotometry
// Background-subtracted aperture photometry on an RGB float32 image.
// Mirrors Python measure_star_rgb_photometry():
//   mu_bg = ann_sum / ann_area
//   star_sum = raw_sum - mu_bg * ap_area
// img_float layout: R G B R G B … (packed, row-major, values in [0,1]).
// ─────────────────────────────────────────────────────────────────────────────
PhotometryResult SPCC::aperturePhotometry(const float* img_float,
                                           int width, int height,
                                           double cx, double cy,
                                           double r, double r_in, double r_out) {
    PhotometryResult res;

    const double r2    = r * r;
    const double rin2  = r_in  * r_in;
    const double rout2 = r_out * r_out;

    // Pixel bounding boxes
    const int x0_ap  = std::max(0,     (int)std::floor(cx - r));
    const int x1_ap  = std::min(width, (int)std::ceil(cx + r) + 1);
    const int y0_ap  = std::max(0,     (int)std::floor(cy - r));
    const int y1_ap  = std::min(height,(int)std::ceil(cy + r) + 1);

    const int x0_ann = std::max(0,     (int)std::floor(cx - r_out));
    const int x1_ann = std::min(width, (int)std::ceil(cx + r_out) + 1);
    const int y0_ann = std::max(0,     (int)std::floor(cy - r_out));
    const int y1_ann = std::min(height,(int)std::ceil(cy + r_out) + 1);

    // Accumulate annulus sums for background estimate
    double ann_R = 0.0, ann_G = 0.0, ann_B = 0.0;
    double ann_area = 0.0;

    for (int y = y0_ann; y < y1_ann; ++y) {
        for (int x = x0_ann; x < x1_ann; ++x) {
            const double dx = x - cx, dy = y - cy;
            const double d2 = dx*dx + dy*dy;
            if (d2 >= rin2 && d2 <= rout2) {
                const size_t idx = ((size_t)y * width + x) * 3;
                ann_R += img_float[idx];
                ann_G += img_float[idx + 1];
                ann_B += img_float[idx + 2];
                ann_area += 1.0;
            }
        }
    }

    if (ann_area < 1.0) return res;   // no background pixels

    const double mu_R = ann_R / ann_area;
    const double mu_G = ann_G / ann_area;
    const double mu_B = ann_B / ann_area;

    // Accumulate aperture sums
    double raw_R = 0.0, raw_G = 0.0, raw_B = 0.0;
    double ap_area = 0.0;
    bool   any_invalid = false;

    for (int y = y0_ap; y < y1_ap && !any_invalid; ++y) {
        for (int x = x0_ap; x < x1_ap && !any_invalid; ++x) {
            const double dx = x - cx, dy = y - cy;
            if (dx*dx + dy*dy <= r2) {
                const size_t idx = ((size_t)y * width + x) * 3;
                raw_R += img_float[idx];
                raw_G += img_float[idx + 1];
                raw_B += img_float[idx + 2];
                ap_area += 1.0;
            }
        }
    }

    if (ap_area < 1.0 || any_invalid) return res;

    // Star-only flux = raw_sum - mu_bg * ap_area
    res.R_star = raw_R - mu_R * ap_area;
    res.G_star = raw_G - mu_G * ap_area;
    res.B_star = raw_B - mu_B * ap_area;
    res.R_bg   = mu_R;
    res.G_bg   = mu_G;
    res.B_bg   = mu_B;

    // Validity: all channels positive and finite
    res.valid = std::isfinite(res.R_star) && std::isfinite(res.G_star) &&
                std::isfinite(res.B_star) &&
                res.R_star > 0.0 && res.G_star > 0.0 && res.B_star > 0.0;

    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::rmsFrac
// sqrt( mean( ((pred_i / exp_i) - 1)^2 ) )
// ─────────────────────────────────────────────────────────────────────────────
double SPCC::rmsFrac(const std::vector<double>& pred,
                      const std::vector<double>& exp_vals) {
    const size_t n = pred.size();
    if (n == 0) return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (std::fabs(exp_vals[i]) < 1e-30) continue;
        const double v = pred[i] / exp_vals[i] - 1.0;
        sum += v * v;
    }
    return std::sqrt(sum / n);
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::polyFit2
// Fit y = a*x^2 + b*x + c by least squares.
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::polyFit2(const std::vector<double>& x,
                     const std::vector<double>& y,
                     double& a, double& b, double& c) {
    const int n = (int)x.size();
    if (n < 3) return false;

    std::vector<std::vector<double>> A(n, std::vector<double>(3));
    for (int i = 0; i < n; ++i) {
        A[i][0] = x[i] * x[i];
        A[i][1] = x[i];
        A[i][2] = 1.0;
    }
    std::vector<double> c3 = leastSquares(A, y);
    a = c3[0]; b = c3[1]; c = c3[2];
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::fitColorModel
// Evaluate three models and return the one with lowest combined RMS residual.
// Mirrors the Python model-selection block in run_spcc().
//
// Model 0 (slope-only):   exp = m * meas
// Model 1 (affine):       exp = m * meas + b
// Model 2 (quadratic):    exp = a * meas^2 + b * meas + c   (needs >= 6 stars)
// ─────────────────────────────────────────────────────────────────────────────
CalibrationModel SPCC::fitColorModel(const std::vector<double>& meas_RG,
                                      const std::vector<double>& exp_RG,
                                      const std::vector<double>& meas_BG,
                                      const std::vector<double>& exp_BG) {
    CalibrationModel best;
    const int n = (int)meas_RG.size();
    if (n == 0) return best;

    const double eps = 1e-12;

    // ── Model 0: slope-only ──────────────────────────────────────────────────
    // Fit MeasRatio = m * ExpectedRatio
    auto slopeOnly = [&](const std::vector<double>& ex,
                          const std::vector<double>& my) -> double {
        double num = 0.0, den = 0.0;
        for (int i = 0; i < n; ++i) { num += ex[i] * my[i]; den += ex[i] * ex[i]; }
        return (den > eps) ? num / den : 1.0;
    };
    const double mR_s = slopeOnly(exp_RG, meas_RG);
    const double mB_s = slopeOnly(exp_BG, meas_BG);

    std::vector<double> predR_s(n), predB_s(n);
    for (int i = 0; i < n; ++i) { predR_s[i] = mR_s * exp_RG[i]; predB_s[i] = mB_s * exp_BG[i]; }
    const double rms_s = rmsFrac(predR_s, meas_RG) + rmsFrac(predB_s, meas_BG);

    // ── Model 1: affine ───────────────────────────────────────────────────────
    // Fit MeasRatio = m * ExpectedRatio + b
    auto affineFit = [&](const std::vector<double>& ex,
                          const std::vector<double>& my,
                          double& slope, double& intercept) {
        std::vector<std::vector<double>> A(n, std::vector<double>(2));
        for (int i = 0; i < n; ++i) { A[i][0] = ex[i]; A[i][1] = 1.0; }
        std::vector<double> c2 = leastSquares(A, my);
        slope = c2[0]; intercept = c2[1];
    };
    double mR_a, bR_a, mB_a, bB_a;
    affineFit(exp_RG, meas_RG, mR_a, bR_a);
    affineFit(exp_BG, meas_BG, mB_a, bB_a);

    std::vector<double> predR_a(n), predB_a(n);
    for (int i = 0; i < n; ++i) {
        predR_a[i] = mR_a * exp_RG[i] + bR_a;
        predB_a[i] = mB_a * exp_BG[i] + bB_a;
    }
    const double rms_a = rmsFrac(predR_a, meas_RG) + rmsFrac(predB_a, meas_BG);

    // ── Model 2: quadratic (needs >= 6 data points) ───────────────────────────
    double aR_q = 0.0, bR_q = 1.0, cR_q = 0.0;
    double aB_q = 0.0, bB_q = 1.0, cB_q = 0.0;
    double rms_q = std::numeric_limits<double>::infinity();

    if (n >= 6) {
        bool ok = polyFit2(exp_RG, meas_RG, aR_q, bR_q, cR_q) &&
                  polyFit2(exp_BG, meas_BG, aB_q, bB_q, cB_q);
        if (ok) {
            std::vector<double> predR_q(n), predB_q(n);
            for (int i = 0; i < n; ++i) {
                predR_q[i] = aR_q * exp_RG[i]*exp_RG[i] + bR_q * exp_RG[i] + cR_q;
                predB_q[i] = aB_q * exp_BG[i]*exp_BG[i] + bB_q * exp_BG[i] + cB_q;
            }
            rms_q = rmsFrac(predR_q, meas_RG) + rmsFrac(predB_q, meas_BG);
        }
    }

    // ── Pick best ────────────────────────────────────────────────────────────
    int idx = 0;
    double best_rms = rms_s;
    if (rms_a < best_rms) { best_rms = rms_a; idx = 1; }
    if (rms_q < best_rms) { best_rms = rms_q; idx = 2; }

    CalibrationModel model;
    model.rms_total = best_rms;

    // We fit Measured = f(Expected). 
    // To transform from Measured to Expected, we multiply Measured by (Expected / Measured).
    // But since applyColorModel anchors to Green, it effectively treats G as having gain 1.
    // The coefficients stored here are used in applyColorModel to calculate the multipliers.
    
    if (idx == 0) {
        model.kind       = MODEL_SLOPE_ONLY;
        model.coeff_R[0] = 0.0; model.coeff_R[1] = mR_s; model.coeff_R[2] = 0.0;
        model.coeff_B[0] = 0.0; model.coeff_B[1] = mB_s; model.coeff_B[2] = 0.0;
    } else if (idx == 1) {
        model.kind       = MODEL_AFFINE;
        model.coeff_R[0] = 0.0; model.coeff_R[1] = mR_a; model.coeff_R[2] = bR_a;
        model.coeff_B[0] = 0.0; model.coeff_B[1] = mB_a; model.coeff_B[2] = bB_a;
    } else {
        model.kind       = MODEL_QUADRATIC;
        model.coeff_R[0] = aR_q; model.coeff_R[1] = bR_q; model.coeff_R[2] = cR_q;
        model.coeff_B[0] = aB_q; model.coeff_B[1] = bB_q; model.coeff_B[2] = cB_q;
    }
    return model;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::applyColorModel
// Applies the chosen polynomial colour model to every pixel.
// Mirrors Python:
//   RG = R / max(G, eps)
//   mR = poly_R(RG)   (clipped to [0.25, 4.0])
//   R' = pivot_R + (R - pivot_R) * mR
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::applyColorModel(float* img_float,
                            int width, int height,
                            const CalibrationModel& model,
                            double pivot_R, double pivot_G, double pivot_B) {
    const size_t n_pixels = (size_t)width * height;

    // Neutralize background: anchor everyone to the average background level
    // to remove any pre-existing color tint in the background pixels.
    const double pivot_avg = (pivot_R + pivot_G + pivot_B) / 3.0;

    // Model fits Measured (R/G) = f(Expected R/G).
    // To correct, we need Expected R/G = f_inv(Measured).
    // For Linear/Affine, we calculate the multiplier directly.
    // For simplicity in the poly application, we invert the prediction:
    // Expected = (Measured - intercept) / slope
    
    double predR_for_expected = polyEval(model.coeff_R, 1.0); // Ratio for Expected=1.0
    double predB_for_expected = polyEval(model.coeff_B, 1.0);

    // Apply corrective multiplier: expected / predicted
    // Since we fit Measured = f(Expected), a measured ratio RG corresponds 
    // roughly to Expected = RG / slope (ignoring intercept for a moment).
    // More accurately, for any measured RG, the multiplier to get back to Exp=1.0 
    // when measuring predR_for_expected is 1/predR_for_expected.
    
    double mR = (std::abs(predR_for_expected) > 1e-9) ? (1.0 / predR_for_expected) : 1.0;
    double mB = (std::abs(predB_for_expected) > 1e-9) ? (1.0 / predB_for_expected) : 1.0;

    // Clip multipliers
    mR = std::max(0.25, std::min(4.0, mR));
    mB = std::max(0.25, std::min(4.0, mB));

    for (size_t i = 0; i < n_pixels; ++i) {
        const size_t base = i * 3;
        const double R = img_float[base];
        const double G = img_float[base + 1];
        const double B = img_float[base + 2];

        const double R_new = pivot_avg + (R - pivot_R) * mR;
        const double G_new = pivot_avg + (G - pivot_G);
        const double B_new = pivot_avg + (B - pivot_B) * mB;

        img_float[base]     = (float)std::max(0.0, std::min(1.0, R_new));
        img_float[base + 1] = (float)std::max(0.0, std::min(1.0, G_new));
        img_float[base + 2] = (float)std::max(0.0, std::min(1.0, B_new));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::fitPoly2Surface
// Least-squares fit of z = c0 + c1*x + c2*y + c3*x^2 + c4*x*y + c5*y^2
// evaluated on the full width x height grid.
// Mirrors Python compute_gradient_map(..., method="poly2").
// ─────────────────────────────────────────────────────────────────────────────
std::vector<double> SPCC::fitPoly2Surface(
        const std::vector<std::array<double,2>>& pts,
        const std::vector<double>& vals,
        int width, int height) {
    const int n = (int)pts.size();
    std::vector<std::vector<double>> A(n, std::vector<double>(6));
    for (int i = 0; i < n; ++i) {
        const double x = pts[i][0], y = pts[i][1];
        A[i] = {1.0, x, y, x*x, x*y, y*y};
    }
    const std::vector<double> c = leastSquares(A, vals);

    std::vector<double> surf((size_t)width * height);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const double x = col, y = row;
            surf[(size_t)row * width + col] =
                c[0] + c[1]*x + c[2]*y + c[3]*x*x + c[4]*x*y + c[5]*y*y;
        }
    }
    return surf;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::fitPoly3Surface
// Third-degree polynomial surface fit.
// Mirrors Python compute_gradient_map(..., method="poly3").
// ─────────────────────────────────────────────────────────────────────────────
std::vector<double> SPCC::fitPoly3Surface(
        const std::vector<std::array<double,2>>& pts,
        const std::vector<double>& vals,
        int width, int height) {
    const int n = (int)pts.size();
    std::vector<std::vector<double>> A(n, std::vector<double>(10));
    for (int i = 0; i < n; ++i) {
        const double x = pts[i][0], y = pts[i][1];
        A[i] = {1.0, x, y, x*x, x*y, y*y, x*x*x, x*x*y, x*y*y, y*y*y};
    }
    const std::vector<double> c = leastSquares(A, vals);

    std::vector<double> surf((size_t)width * height);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const double x = col, y = row;
            surf[(size_t)row * width + col] =
                c[0] + c[1]*x + c[2]*y +
                c[3]*x*x + c[4]*x*y + c[5]*y*y +
                c[6]*x*x*x + c[7]*x*x*y + c[8]*x*y*y + c[9]*y*y*y;
        }
    }
    return surf;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::computeGradientSurface
// Differential-magnitude chromatic gradient removal.
// Mirrors Python run_gradient_extraction():
//   dm = measured_mag - expected_mag
//   surface fitted in dm space, then clamped to ±max_allowed_mag
//   scale = 10^(-0.4 * dm)    (divide image by this to remove residual)
// ─────────────────────────────────────────────────────────────────────────────
GradientSurface SPCC::computeGradientSurface(
        const float* /*img_float*/,
        int width, int height,
        const std::vector<EnrichedMatch>& matches,
        const double /*T_sys_R*/[WL_GRID_LEN],
        const double /*T_sys_G*/[WL_GRID_LEN],
        const double /*T_sys_B*/[WL_GRID_LEN],
        const QString& method,
        double max_allowed_mag) {
    GradientSurface surf;
    surf.width = width; surf.height = height;

    if (matches.size() < 6) return surf;  // need enough data points

    const int n = (int)matches.size();
    const double eps = 1e-30;

    // Compute measured and expected magnitudes for each matched star
    std::vector<std::array<double,2>> pts(n);
    std::vector<double> dmR(n), dmG(n), dmB(n);

    for (int i = 0; i < n; ++i) {
        const EnrichedMatch& em = matches[i];
        pts[i] = {em.x_img, em.y_img};

        // Measured instrumental magnitude (log of star-only flux)
        const double mag_R_meas = (em.R_meas > eps) ? -2.5 * std::log10(em.R_meas) : 0.0;
        const double mag_G_meas = (em.G_meas > eps) ? -2.5 * std::log10(em.G_meas) : 0.0;
        const double mag_B_meas = (em.B_meas > eps) ? -2.5 * std::log10(em.B_meas) : 0.0;

        // Expected magnitude from SED integral
        const double mag_R_exp  = (em.S_star_R > eps) ? -2.5 * std::log10(em.S_star_R) : 0.0;
        const double mag_G_exp  = (em.S_star_G > eps) ? -2.5 * std::log10(em.S_star_G) : 0.0;
        const double mag_B_exp  = (em.S_star_B > eps) ? -2.5 * std::log10(em.S_star_B) : 0.0;

        dmR[i] = mag_R_meas - mag_R_exp;
        dmG[i] = mag_G_meas - mag_G_exp;
        dmB[i] = mag_B_meas - mag_B_exp;
    }

    // Center each dm vector (subtract median so the surface has zero mean)
    auto center = [&](std::vector<double>& dm) {
        const double med = vectorMedianImpl(dm);
        for (double& v : dm) v -= med;
    };
    center(dmR); center(dmG); center(dmB);

    // Fit the surface
    auto fitSurface = [&](const std::vector<double>& dm) -> std::vector<double> {
        if (method.toLower() == "poly2")
            return fitPoly2Surface(pts, dm, width, height);
        else   // default: poly3
            return fitPoly3Surface(pts, dm, width, height);
    };

    std::vector<double> sR = fitSurface(dmR);
    std::vector<double> sG = fitSurface(dmG);
    std::vector<double> sB = fitSurface(dmB);

    // Re-center fitted surfaces
    center(sR); center(sG); center(sB);

    // Clamp to ±max_allowed_mag
    auto clamp = [&](std::vector<double>& s) {
        double peak = 0.0;
        for (double v : s) peak = std::max(peak, std::fabs(v));
        if (peak > max_allowed_mag) {
            const double scale = max_allowed_mag / peak;
            for (double& v : s) v *= scale;
        }
    };
    clamp(sR); clamp(sG); clamp(sB);

    // Convert from differential magnitude to multiplicative scale:
    //   scale = 10^(-0.4 * dm)
    // Then divide image by scale to remove the residual.
    const size_t np = (size_t)width * height;
    surf.R.resize(np); surf.G.resize(np); surf.B.resize(np);
    for (size_t i = 0; i < np; ++i) {
        surf.R[i] = (float)std::pow(10.0, -0.4 * sR[i]);
        surf.G[i] = (float)std::pow(10.0, -0.4 * sG[i]);
        surf.B[i] = (float)std::pow(10.0, -0.4 * sB[i]);
    }
    surf.valid = true;
    return surf;
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::applyGradientSurface
// corrected[ch] = img[ch] / max(scale[ch], 1e-8), clamped to [0,1].
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::applyGradientSurface(float* img_float, int width, int height,
                                 const GradientSurface& surf) {
    if (!surf.valid) return;
    const size_t np = (size_t)width * height;
    const float  eps_f = 1e-8f;
    for (size_t i = 0; i < np; ++i) {
        const size_t base = i * 3;
        img_float[base]     = std::max(0.f, std::min(1.f, img_float[base]     / std::max(surf.R[i], eps_f)));
        img_float[base + 1] = std::max(0.f, std::min(1.f, img_float[base + 1] / std::max(surf.G[i], eps_f)));
        img_float[base + 2] = std::max(0.f, std::min(1.f, img_float[base + 2] / std::max(surf.B[i], eps_f)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC::calibrateWithStarList
// Full calibration pipeline.  Called from SPCCDialog via QtConcurrent::run().
// ─────────────────────────────────────────────────────────────────────────────
SPCCResult SPCC::calibrateWithStarList(const ImageBuffer& buf,
                                        const SPCCParams& params,
                                        const std::vector<StarRecord>& starRecords) {
    SPCCResult result;
    result.stars_found = (int)starRecords.size();

    auto progress = [&](int pct, const QString& msg) {
        if (params.progressCallback) params.progressCallback(pct, msg);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 1. Load spectral database (FITS + JSON)
    // The UI populates filter/sensor combos from both tstar_data.fits and the
    // TStar-spcc-database JSON files, so calibration must load both sources to
    // ensure every user-selected entry can be found by buildSystemThroughput().
    // ─────────────────────────────────────────────────────────────────────────
    progress(2, "Loading spectral database...");
    SPCCDataStore store;
    bool fitsOk = loadTStarFits(params.dataPath, store);
    // Always attempt to load the JSON database (complements and/or replaces FITS entries)
    bool dbOk = loadTStarDatabase(params.dataPath + "/TStar-spcc-database", store);
    if (!fitsOk && !dbOk) {
        result.error_msg = "Failed to load spectral database from: " + params.dataPath;
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Build system throughput for R, G, B
    // ─────────────────────────────────────────────────────────────────────────
    if (params.cancelFlag && params.cancelFlag->load()) { result.error_msg = "Cancelled"; return result; }
    progress(5, "Building system throughput curves...");

    double T_sys_R[WL_GRID_LEN], T_sys_G[WL_GRID_LEN], T_sys_B[WL_GRID_LEN];
    buildSystemThroughput(store, params.rFilter, params.sensor, params.lpFilter1, params.lpFilter2, "Red",   T_sys_R);
    buildSystemThroughput(store, params.gFilter, params.sensor, params.lpFilter1, params.lpFilter2, "Green", T_sys_G);
    buildSystemThroughput(store, params.bFilter, params.sensor, params.lpFilter1, params.lpFilter2, "Blue",  T_sys_B);

    QString log = "Built system throughput:\n";
    auto logChan = [&](const QString& name, const QString& f, const QString& s, const QString& lp1, const QString& lp2) {
        log += QString(" - %1: Filter=%2, Sensor=%3, LP=%4/%5\n").arg(name, f, s, lp1, lp2);
    };
    logChan("Red",   params.rFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    logChan("Green", params.gFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    logChan("Blue",  params.bFilter, params.sensor, params.lpFilter1, params.lpFilter2);
    result.log_msg += log;
    qCInfo(lcSPCC).noquote() << log;

    // ─────────────────────────────────────────────────────────────────────────
    // 3. Integrate the reference SED against each channel's T_sys
    // ─────────────────────────────────────────────────────────────────────────
    progress(8, "Integrating reference SED...");

    const SPCCObject* refSED = findCurve(store.sed_list, params.whiteRef);
    if (!refSED) {
        result.error_msg = "Reference SED not found: " + params.whiteRef;
        return result;
    }
    double refGrid[WL_GRID_LEN];
    interpolateToGrid(refSED->x, refSED->y, refGrid);
    const double S_ref_R = trapz(refGrid, T_sys_R);
    const double S_ref_G = trapz(refGrid, T_sys_G);
    const double S_ref_B = trapz(refGrid, T_sys_B);
    qCInfo(lcSPCC) << "Reference SED integrals: R=" << S_ref_R << "G=" << S_ref_G << "B=" << S_ref_B;

    // ─────────────────────────────────────────────────────────────────────────
    // 4. Pre-compute Pickles template integrals for all unique spectral types
    // ─────────────────────────────────────────────────────────────────────────
    if (params.cancelFlag && params.cancelFlag->load()) { result.error_msg = "Cancelled"; return result; }
    progress(10, "Pre-computing Pickles SED integrals...");

    const QStringList allSEDNames = availableSEDs(store);

    // ── Build Anchor Table for interpolation ──────────────────────────────────
    struct AnchorPoint {
        QString type;
        double bp_rp;
        double sumR, sumG, sumB;
    };
    std::vector<AnchorPoint> anchors = {
        {"O5V", -0.33, 0,0,0}, {"B0V", -0.24, 0,0,0}, {"B5V", -0.11, 0,0,0},
        {"A0V",  0.00, 0,0,0}, {"A5V",  0.14, 0,0,0}, {"F0V",  0.31, 0,0,0},
        {"F5V",  0.44, 0,0,0}, {"G0V",  0.59, 0,0,0}, {"G2V",  0.64, 0,0,0},
        {"G5V",  0.76, 0,0,0}, {"K0V",  0.93, 0,0,0}, {"K5V",  1.15, 0,0,0},
        {"M0V",  1.45, 0,0,0}, {"M5V",  1.84, 0,0,0}
    };

    auto findSedByPattern = [&](const QString& p) -> const SPCCObject* {
        for (const auto& s : store.sed_list) {
            if (s.name.contains(p, Qt::CaseInsensitive)) return &s;
        }
        return nullptr;
    };

    for (auto& a : anchors) {
        const SPCCObject* sed = findSedByPattern(a.type);
        if (sed && sed->arrays_loaded) {
            double grid[WL_GRID_LEN];
            interpolateToGrid(sed->x, sed->y, grid);
            a.sumR = trapz(grid, T_sys_R);
            a.sumG = trapz(grid, T_sys_G);
            a.sumB = trapz(grid, T_sys_B);
        } else {
            const SPCCObject* fallback = findSedByPattern("G2V");
            if (fallback) {
                double grid[WL_GRID_LEN];
                interpolateToGrid(fallback->x, fallback->y, grid);
                a.sumR = trapz(grid, T_sys_R);
                a.sumG = trapz(grid, T_sys_G);
                a.sumB = trapz(grid, T_sys_B);
            }
        }
    }
    std::sort(anchors.begin(), anchors.end(), [](const AnchorPoint& a, const AnchorPoint& b){
        return a.bp_rp < b.bp_rp;
    });

    // ─────────────────────────────────────────────────────────────────────────
    // 5. Extract pixel data as float32 RGB in [0,1]
    // ─────────────────────────────────────────────────────────────────────────
    progress(15, "Preparing image data...");

    const int W = buf.width();
    const int H = buf.height();
    const size_t n_pixels = (size_t)W * H;

    // Build a float32 working copy; ImageBuffer stores float in [0,1] or uint16.
    std::vector<float> img_f32(n_pixels * 3);
    {
        const std::vector<float>& srcData = buf.data();
        const float* src = srcData.data();
        // ImageBuffer is expected to provide float32 RGB; just copy.
        std::copy(src, src + n_pixels * 3, img_f32.data());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 6. Main matching loop: aperture photometry + ratio accumulation
    // ─────────────────────────────────────────────────────────────────────────
    if (params.cancelFlag && params.cancelFlag->load()) { result.error_msg = "Cancelled"; return result; }
    progress(20, "Performing aperture photometry on matched stars...");

    std::vector<double> meas_RG, meas_BG, exp_RG, exp_BG;
    std::vector<EnrichedMatch> enriched;

    int logCount = 0;
    int validColorCount = 0;
    int fallbackColorCount = 0;
    double minExpRG = 1e9, maxExpRG = -1e9;
    double minExpBG = 1e9, maxExpBG = -1e9;

    for (const StarRecord& sr : starRecords) {
        if (params.cancelFlag && params.cancelFlag->load()) { result.error_msg = "Cancelled"; return result; }
        
        // Determine aperture geometry mirroring Python:

        //   r  = clip(2.5 * a, 2.0, 12.0)
        //   rin  = clip(3.0 * r, 6.0, 40.0)
        //   rout = clip(5.0 * r, rin + 2.0, 60.0)
        const double a   = sr.semi_a;
        const double r   = std::max(2.0, std::min(12.0, 2.5 * a));
        const double rin  = std::max(6.0, std::min(40.0, 3.0 * r));
        const double rout = std::max(rin + 2.0, std::min(60.0, 5.0 * r));

        PhotometryResult phot = aperturePhotometry(img_f32.data(), W, H,
                                                    sr.x_img, sr.y_img,
                                                    r, rin, rout);
        if (!phot.valid) continue;

        // ── Catalog (Expected) ───────────────────────────────────────────────
        double bprp = sr.gaia_bp_rp;
        if (!std::isfinite(bprp)) {
            bprp = 0.64; // Default to G2V (Solar type)
            fallbackColorCount++;
        } else {
            validColorCount++;
        }

        double S_sr = 0, S_sg = 0, S_sb = 0;
        if (bprp <= anchors.front().bp_rp) {
            S_sr = anchors.front().sumR; S_sg = anchors.front().sumG; S_sb = anchors.front().sumB;
        } else if (bprp >= anchors.back().bp_rp) {
            S_sr = anchors.back().sumR; S_sg = anchors.back().sumG; S_sb = anchors.back().sumB;
        } else {
            for (size_t i = 0; i < anchors.size() - 1; ++i) {
                if (bprp >= anchors[i].bp_rp && bprp < anchors[i+1].bp_rp) {
                    double t = (bprp - anchors[i].bp_rp) / (anchors[i+1].bp_rp - anchors[i].bp_rp);
                    S_sr = anchors[i].sumR * (1.0 - t) + anchors[i+1].sumR * t;
                    S_sg = anchors[i].sumG * (1.0 - t) + anchors[i+1].sumG * t;
                    S_sb = anchors[i].sumB * (1.0 - t) + anchors[i+1].sumB * t;
                    break;
                }
            }
        }

        if (!std::isfinite(S_sr) || !std::isfinite(S_sg) || !std::isfinite(S_sb)) continue;
        if (S_sg <= 0.0 || S_sr <= 0.0 || S_sb <= 0.0) continue;

        // Normalize expected ratios by the white reference ratios.
        // This ensures that a star matching the white reference SED
        // will have eRG = 1.0 and eBG = 1.0.
        const double eRG = (S_sr / S_sg) / (S_ref_R / S_ref_G);
        const double eBG = (S_sb / S_sg) / (S_ref_B / S_ref_G);
        const double mRG = phot.R_star / phot.G_star;
        const double mBG = phot.B_star / phot.G_star;

        if (!std::isfinite(eRG) || !std::isfinite(eBG)) continue;
        if (!std::isfinite(mRG) || !std::isfinite(mBG)) continue;

        meas_RG.push_back(mRG); meas_BG.push_back(mBG);
        exp_RG .push_back(eRG); exp_BG .push_back(eBG);

        minExpRG = std::min(minExpRG, eRG); maxExpRG = std::max(maxExpRG, eRG);
        minExpBG = std::min(minExpBG, eBG); maxExpBG = std::max(maxExpBG, eBG);

        if (logCount < 20) {
            qCInfo(lcSPCC) << "[SPCC Diag] Star match:" << sr.pickles_match 
                           << "eRG:" << eRG << "eBG:" << eBG 
                           << "mRG:" << mRG << "mBG:" << mBG;
            logCount++;
        }

        EnrichedMatch em;
        em.x_img   = sr.x_img;  em.y_img = sr.y_img;
        em.R_meas  = phot.R_star; em.G_meas = phot.G_star; em.B_meas = phot.B_star;
        em.S_star_R = S_sr; em.S_star_G = S_sg; em.S_star_B = S_sb;
        em.exp_RG  = eRG;  em.exp_BG  = eBG;
        em.meas_RG = mRG;  em.meas_BG = mBG;
        em.r_ap    = r;    em.r_in    = rin;   em.r_out   = rout;
        enriched.push_back(em);
    }

    qCInfo(lcSPCC) << "[SPCC] Color data quality: " << validColorCount << "stars with catalog BP-RP, " 
                   << fallbackColorCount << "stars using default G2V.";
    qCInfo(lcSPCC) << "[SPCC] Expected Ratio Spread: RG [" << minExpRG << "," << maxExpRG << "], BG [" << minExpBG << "," << maxExpBG << "]";

    result.stars_used = (int)meas_RG.size();

    if (result.stars_used < 3) {
        result.error_msg = QString("Too few valid stars for calibration: %1 (need >= 3)")
                           .arg(result.stars_used);
        return result;
    }
    qCInfo(lcSPCC) << "[SPCC] Valid stars for fit:" << result.stars_used;

    // ─────────────────────────────────────────────────────────────────────────
    // 7. Fit colour model (slope-only / affine / quadratic)
    // ─────────────────────────────────────────────────────────────────────────
    progress(60, "Fitting colour calibration model...");

    CalibrationModel model = fitColorModel(meas_RG, exp_RG, meas_BG, exp_BG);
    result.model    = model;
    result.residual = model.rms_total;

    // Evaluate global multipliers (Linear Mode)
    // We fit Measured = k * Expected, so k = Measured/Expected.
    // The multiplier to get back to Expected from Measured is 1.0 / k.
    
    double mR_slope = 1.0, mB_slope = 1.0;
    if (!meas_RG.empty()) {
        double sum_m_e_R = 0, sum_e_e_R = 0;
        double sum_m_e_B = 0, sum_e_e_B = 0;
        for (size_t i = 0; i < meas_RG.size(); ++i) {
            sum_m_e_R += meas_RG[i] * exp_RG[i]; sum_e_e_R += exp_RG[i] * exp_RG[i];
            sum_m_e_B += meas_BG[i] * exp_BG[i]; sum_e_e_B += exp_BG[i] * exp_BG[i];
        }
        if (sum_e_e_R > 0) mR_slope = sum_m_e_R / sum_e_e_R; // k = sum(m*e)/sum(e^2)
        if (sum_e_e_B > 0) mB_slope = sum_m_e_B / sum_e_e_B;
    }

    // Factors for display and apply: multiplier = 1/k
    float kR = (mR_slope > 1e-9) ? (1.0f / (float)mR_slope) : 1.0f;
    float kB = (mB_slope > 1e-9) ? (1.0f / (float)mB_slope) : 1.0f;

    result.white_balance_k[0] = kR;
    result.white_balance_k[1] = 1.0;
    result.white_balance_k[2] = kB;
    
    result.scaleR = kR;
    result.scaleG = 1.0;
    result.scaleB = kB;

    result.corrMatrix[0][0] = kR;
    result.corrMatrix[1][1] = 1.0;
    result.corrMatrix[2][2] = kB;
    
    // If NOT linear mode, the displayed scale factors are just an approximation (at Ref point)
    if (!params.linearMode) {
        // Here we could evaluate the model at Exp=1.0 for display.
        // Since Meas = f(Exp), for Exp=1.0, Meas = f(1.0).
        // Gain factor = Exp/Meas = 1.0 / f(1.0).
        double pR = polyEval(model.coeff_R, 1.0);
        double pB = polyEval(model.coeff_B, 1.0);
        result.scaleR = (std::abs(pR) > 1e-9) ? (1.0 / pR) : 1.0;
        result.scaleB = (std::abs(pB) > 1e-9) ? (1.0 / pB) : 1.0;
    }

    // Populate diagnostics
    for (const EnrichedMatch& em : enriched) {
        SPCCResult::DiagStar ds;
        ds.x_img   = em.x_img;   ds.y_img   = em.y_img;
        ds.meas_RG = em.meas_RG; ds.meas_BG = em.meas_BG;
        ds.exp_RG  = em.exp_RG;  ds.exp_BG  = em.exp_BG;
        ds.is_inlier = true;
        result.diagnostics.push_back(ds);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 8. Apply colour model to the image
    // ─────────────────────────────────────────────────────────────────────────
    if (params.cancelFlag && params.cancelFlag->load()) { result.error_msg = "Cancelled"; return result; }
    progress(75, "Applying colour calibration...");

    // Compute pivot values (median of each channel over the whole image)
    std::vector<double> chanR(n_pixels), chanG(n_pixels), chanB(n_pixels);
    for (size_t i = 0; i < n_pixels; ++i) {
        chanR[i] = img_f32[i*3];
        chanG[i] = img_f32[i*3 + 1];
        chanB[i] = img_f32[i*3 + 2];
    }
    const double pivot_R = vectorMedianImpl(chanR);
    const double pivot_G = vectorMedianImpl(chanG);
    const double pivot_B = vectorMedianImpl(chanB);

    if (params.linearMode) {
        // Linear Application: simple per-channel multiplication
        // Note: we subtract background pivot then re-add a neutral pivot?
        // PCC formula: P' = (P - Bg) * K + Bg_Mean
        const float kr = (float)kR;
        const float kb = (float)kB;
        const float offsetR = (float)(pivot_G - pivot_R * kr);
        const float offsetB = (float)(pivot_G - pivot_B * kb);

        #pragma omp parallel for
        for (long i = 0; i < (long)n_pixels; ++i) {
            float r = img_f32[i*3 + 0];
            float g = img_f32[i*3 + 1];
            float b = img_f32[i*3 + 2];
            img_f32[i*3 + 0] = std::clamp(r * kr + offsetR, 0.0f, 1.0f);
            img_f32[i*3 + 1] = std::clamp(g, 0.0f, 1.0f);
            img_f32[i*3 + 2] = std::clamp(b * kb + offsetB, 0.0f, 1.0f);
        }
    } else {
        applyColorModel(img_f32.data(), W, H, model, pivot_R, pivot_G, pivot_B);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 9. Optional: chromatic gradient removal
    // ─────────────────────────────────────────────────────────────────────────
    if (params.runGradient && enriched.size() >= 6) {
        progress(82, "Computing chromatic gradient surface...");
        GradientSurface gsurf = computeGradientSurface(
            img_f32.data(), W, H, enriched,
            T_sys_R, T_sys_G, T_sys_B,
            params.gradientMethod);

        if (gsurf.valid) {
            progress(90, "Applying gradient correction...");
            applyGradientSurface(img_f32.data(), W, H, gsurf);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 10. Write back into a new ImageBuffer
    // ─────────────────────────────────────────────────────────────────────────
    progress(95, "Finalising output image...");

    auto out = std::make_shared<ImageBuffer>(buf);  // copy metadata + size
    std::vector<float>& outData = out->data();
    std::copy(img_f32.begin(), img_f32.end(), outData.begin());
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

double SPCC::bpRpFromType(const QString& spec) {
    QString s = spec.toUpper();
    if (s.startsWith("O")) return -0.4;
    if (s.startsWith("B0")) return -0.3;
    if (s.startsWith("B5")) return -0.14;
    if (s.startsWith("B"))  return -0.2;
    if (s.startsWith("A0")) return 0.0;
    if (s.startsWith("A5")) return 0.15;
    if (s.startsWith("A"))  return 0.1;
    if (s.startsWith("F0")) return 0.3;
    if (s.startsWith("F5")) return 0.46;
    if (s.startsWith("F"))  return 0.4;
    if (s.startsWith("G0")) return 0.58;
    if (s.startsWith("G2")) return 0.64;
    if (s.startsWith("G5")) return 0.78;
    if (s.startsWith("G"))  return 0.65;
    if (s.startsWith("K0")) return 0.96;
    if (s.startsWith("K5")) return 1.41;
    if (s.startsWith("K"))  return 1.1;
    if (s.startsWith("M0")) return 1.84;
    if (s.startsWith("M5")) return 2.80;
    if (s.startsWith("M"))  return 2.2;
    return 0.64; // Default G2V
}


SPCCResult SPCC::calibrateWithCatalog(const ImageBuffer& buf,
                                       const SPCCParams&  params,
                                       const std::vector<CatalogStar>& stars) {
    // Convert CatalogStar -> StarRecord
    // CatalogStar currently provides only astrometric/photometric fields.
    // Pixel coordinates and spectral type are not available in this type.
    std::vector<StarRecord> records;
    records.reserve(stars.size());

    SPCCDataStore store;
    loadTStarFits(params.dataPath, store);
    const QStringList allSEDs = availableSEDs(store);

    for (const CatalogStar& cs : stars) {
        StarRecord sr;
        sr.ra    = cs.ra;
        sr.dec   = cs.dec;
        sr.x_img = 0.0;
        sr.y_img = 0.0;
        sr.semi_a = 2.0;         // default when not provided
        sr.sp_type.clear();
        sr.gaia_bp_rp = cs.bp_rp;
        sr.gaia_gmag  = cs.magV > 0 ? cs.magV : cs.magB;

        if (!sr.sp_type.isEmpty()) {
            // Extract single-letter class for fallback matching
            if (!sr.sp_type.isEmpty())
                sr.sp_clean = sr.sp_type.left(1).toUpper();

            QStringList candidates = picklesMatchForSimbad(sr.sp_type, allSEDs);
            if (!candidates.isEmpty())
                sr.pickles_match = candidates.first();
        } else if (cs.teff > 1000.0) {
             // Map Teff to bp_rp for interpolation if direct bp_rp missing
             if (std::isnan(sr.gaia_bp_rp)) {
                 if (cs.teff > 30000)      sr.gaia_bp_rp = -0.4;
                 else if (cs.teff > 15000) sr.gaia_bp_rp = -0.15;
                 else if (cs.teff > 9000)  sr.gaia_bp_rp = 0.0;
                 else if (cs.teff > 7000)  sr.gaia_bp_rp = 0.3;
                 else if (cs.teff > 6000)  sr.gaia_bp_rp = 0.58;
                 else if (cs.teff > 5500)  sr.gaia_bp_rp = 0.65;
                 else if (cs.teff > 5000)  sr.gaia_bp_rp = 0.9;
                 else if (cs.teff > 4000)  sr.gaia_bp_rp = 1.3;
                 else                      sr.gaia_bp_rp = 2.0;
             }
        }
        records.push_back(sr);
    }

    return calibrateWithStarList(buf, params, records);
}