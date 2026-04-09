#include "FitsHeaderUtils.h"

#include <QRegularExpression>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <cmath>


// =============================================================================
// Static member initialisation
// =============================================================================

const QStringList FitsHeaderUtils::structuralKeywords = {
    "SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "NAXIS3",
    "EXTEND", "BZERO",  "BSCALE", "END"
};


// =============================================================================
// Header card validation and filtering
// =============================================================================

bool FitsHeaderUtils::isValidKeyword(const QString& key)
{
    if (key.isEmpty())
        return false;

    // HIERARCH keywords may exceed the standard 8-character limit.
    if (key.startsWith("HIERARCH ", Qt::CaseInsensitive))
        return true;

    if (key.length() > 8)
        return false;

    static const QRegularExpression validChars(
        "^[A-Z0-9_-]+$",
        QRegularExpression::CaseInsensitiveOption);

    return validChars.match(key).hasMatch();
}

std::vector<FitsHeaderUtils::HeaderCard>
FitsHeaderUtils::dropInvalidCards(const std::vector<HeaderCard>& cards)
{
    std::vector<HeaderCard> result;
    result.reserve(cards.size());

    for (const auto& card : cards)
    {
        const QString key = card.key.trimmed().toUpper();

        // Discard empty keywords.
        if (key.isEmpty())
            continue;

        // Structural keywords are emitted separately by the writer.
        if (structuralKeywords.contains(key))
            continue;

        // Discard syntactically invalid keywords.
        if (!isValidKeyword(key))
        {
            qWarning() << "Dropping invalid FITS keyword:" << key;
            continue;
        }

        // Warn about values that exceed the standard field width.
        // The card is retained for maximum compatibility (CONTINUE convention).
        if (card.value.length() > 68)
            qWarning() << "FITS keyword value too long, may cause issues:" << key;

        result.push_back(card);
    }

    return result;
}


// =============================================================================
// WCS validation and construction
// =============================================================================

bool FitsHeaderUtils::hasValidWCS(const Metadata& meta)
{
    // A non-degenerate CD matrix is required (determinant must be non-zero).
    // RA = 0 and Dec = 0 are valid sky coordinates and must not be rejected.
    // CRPIX at (0,0) is also valid (e.g. edge-referenced or 0-indexed solutions).
    const double det     = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    return std::abs(det) > 1e-20;
}

std::vector<FitsHeaderUtils::HeaderCard>
FitsHeaderUtils::buildWCSHeader(const Metadata& meta)
{
    std::vector<HeaderCard> result;

    if (!hasValidWCS(meta))
        return result;

    // Derive CTYPE strings, defaulting to TAN projection when not specified.
    QString ctype1 = meta.ctype1.isEmpty() ? "RA---TAN" : meta.ctype1;
    QString ctype2 = meta.ctype2.isEmpty() ? "DEC--TAN" : meta.ctype2;

    // Append the SIP suffix when distortion coefficients are present.
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0)
    {
        if (!ctype1.endsWith("-SIP")) ctype1 += "-SIP";
        if (!ctype2.endsWith("-SIP")) ctype2 += "-SIP";
    }

    // Projection type and equinox.
    result.push_back({"CTYPE1",  QString("'%1'").arg(ctype1),                     "Coordinate type"});
    result.push_back({"CTYPE2",  QString("'%1'").arg(ctype2),                     "Coordinate type"});
    result.push_back({"EQUINOX", QString::number(meta.equinox, 'f', 1),           "Equinox of coordinates"});

    // Reference pixel and sky coordinates.
    result.push_back({"CRVAL1",  QString::number(meta.ra,     'f', 10),           "RA at reference pixel"});
    result.push_back({"CRVAL2",  QString::number(meta.dec,    'f', 10),           "Dec at reference pixel"});
    result.push_back({"CRPIX1",  QString::number(meta.crpix1, 'f', 4),            "Reference pixel X"});
    result.push_back({"CRPIX2",  QString::number(meta.crpix2, 'f', 4),            "Reference pixel Y"});

    // CD transformation matrix.
    result.push_back({"CD1_1",   QString::number(meta.cd1_1,  'e', 12),           ""});
    result.push_back({"CD1_2",   QString::number(meta.cd1_2,  'e', 12),           ""});
    result.push_back({"CD2_1",   QString::number(meta.cd2_1,  'e', 12),           ""});
    result.push_back({"CD2_2",   QString::number(meta.cd2_2,  'e', 12),           ""});

    // Non-default native longitude/latitude poles.
    if (std::abs(meta.lonpole - 180.0) > 0.001)
        result.push_back({"LONPOLE", QString::number(meta.lonpole, 'f', 6),       ""});

    if (std::abs(meta.latpole) > 0.001)
        result.push_back({"LATPOLE", QString::number(meta.latpole, 'f', 6),       ""});

    // SIP polynomial orders.
    if (meta.sipOrderA  > 0) result.push_back({"A_ORDER",  QString::number(meta.sipOrderA),  "SIP polynomial order"});
    if (meta.sipOrderB  > 0) result.push_back({"B_ORDER",  QString::number(meta.sipOrderB),  "SIP polynomial order"});
    if (meta.sipOrderAP > 0) result.push_back({"AP_ORDER", QString::number(meta.sipOrderAP), "SIP inverse polynomial order"});
    if (meta.sipOrderBP > 0) result.push_back({"BP_ORDER", QString::number(meta.sipOrderBP), "SIP inverse polynomial order"});

    // All SIP coefficients (A_i_j, B_i_j, AP_i_j, BP_i_j).
    for (auto it = meta.sipCoeffs.constBegin(); it != meta.sipCoeffs.constEnd(); ++it)
        result.push_back({it.key(), QString::number(it.value(), 'e', 15), ""});

    return result;
}


// =============================================================================
// XISF keyword and property ingestion
// =============================================================================

std::vector<FitsHeaderUtils::HeaderCard>
FitsHeaderUtils::parseXISFFitsKeywords(const QMap<QString, QVariant>& fitsKeywords)
{
    std::vector<HeaderCard> result;

    for (auto it = fitsKeywords.constBegin(); it != fitsKeywords.constEnd(); ++it)
    {
        const QString   key = it.key();
        const QVariant  val = it.value();

        QString value;
        QString comment;

        // XISF stores keywords in several structural forms; handle all of them.
        if (val.typeId() == QMetaType::QVariantList)
        {
            const QVariantList list = val.toList();
            if (!list.isEmpty())
            {
                const QVariant first = list.first();
                if (first.typeId() == QMetaType::QVariantMap)
                {
                    const QVariantMap map = first.toMap();
                    value   = map.value("value").toString();
                    comment = map.value("comment").toString();
                }
                else
                {
                    value = first.toString();
                }
            }
        }
        else if (val.typeId() == QMetaType::QVariantMap)
        {
            const QVariantMap map = val.toMap();
            value   = map.value("value").toString();
            comment = map.value("comment").toString();
        }
        else
        {
            value = val.toString();
        }

        result.push_back({key, value, comment});
    }

    return result;
}

void FitsHeaderUtils::applyXISFProperties(const QVariantMap& props, Metadata& meta)
{
    // Reference image coordinates (CRPIX equivalent).
    if (props.contains("PCL:AstrometricSolution:ReferenceImageCoordinates"))
    {
        const QVariant val    = props.value("PCL:AstrometricSolution:ReferenceImageCoordinates");
        QVariantList   coords = val.toMap().value("value").toList();
        if (coords.isEmpty()) coords = val.toList();
        if (coords.size() >= 2)
        {
            meta.crpix1 = coords[0].toDouble();
            meta.crpix2 = coords[1].toDouble();
        }
    }

    // Reference celestial coordinates (CRVAL equivalent).
    if (props.contains("PCL:AstrometricSolution:ReferenceCelestialCoordinates"))
    {
        const QVariant val    = props.value("PCL:AstrometricSolution:ReferenceCelestialCoordinates");
        QVariantList   coords = val.toMap().value("value").toList();
        if (coords.isEmpty()) coords = val.toList();
        if (coords.size() >= 2)
        {
            meta.ra  = coords[0].toDouble();
            meta.dec = coords[1].toDouble();
        }
    }

    // Linear transformation matrix (CD matrix).
    if (props.contains("PCL:AstrometricSolution:LinearTransformationMatrix"))
    {
        const QVariant val    = props.value("PCL:AstrometricSolution:LinearTransformationMatrix");
        QVariantList   matrix = val.toMap().value("value").toList();
        if (matrix.isEmpty()) matrix = val.toList();
        if (matrix.size() >= 2)
        {
            const QVariantList row0 = matrix[0].toList();
            const QVariantList row1 = matrix[1].toList();
            if (row0.size() >= 2 && row1.size() >= 2)
            {
                meta.cd1_1 = row0[0].toDouble();
                meta.cd1_2 = row0[1].toDouble();
                meta.cd2_1 = row1[0].toDouble();
                meta.cd2_2 = row1[1].toDouble();
            }
        }
    }

    // Pixel scale in arcseconds per pixel.
    if (props.contains("PCL:AstrometricSolution:PixelSize"))
    {
        const QVariant val      = props.value("PCL:AstrometricSolution:PixelSize");
        double         pixScale = val.toMap().value("value").toDouble();
        if (pixScale == 0.0) pixScale = val.toDouble();
        if (pixScale > 0.0)
            meta.pixelSize = pixScale;
    }

    // Telescope focal length (stored in metres; convert to millimetres).
    if (props.contains("Instrument:Telescope:FocalLength"))
    {
        const QVariant val = props.value("Instrument:Telescope:FocalLength");
        double         fl  = val.toMap().value("value").toDouble();
        if (fl == 0.0) fl  = val.toDouble();
        if (fl > 0.0)
            meta.focalLength = fl * 1000.0;
    }

    // Target object name.
    if (props.contains("Observation:Object:Name"))
    {
        const QVariant val  = props.value("Observation:Object:Name");
        QString        name = val.toMap().value("value").toString();
        if (name.isEmpty()) name = val.toString();
        if (!name.isEmpty())
            meta.objectName = name;
    }

    // Observation start time.
    if (props.contains("Observation:Time:Start"))
    {
        const QVariant val  = props.value("Observation:Time:Start");
        QString        time = val.toMap().value("value").toString();
        if (time.isEmpty()) time = val.toString();
        if (!time.isEmpty())
            meta.dateObs = time;
    }

    // Persist all properties for downstream consumers.
    meta.xisfProperties = props;
}


// =============================================================================
// FITS value coercion
// =============================================================================

QVariant FitsHeaderUtils::coerceFitsValue(const QString& value)
{
    if (value.isEmpty())
        return QVariant();

    const QString trimmed = value.trimmed();

    // Boolean literals.
    if (trimmed.compare("T",    Qt::CaseInsensitive) == 0 ||
        trimmed.compare("true", Qt::CaseInsensitive) == 0)
        return true;

    if (trimmed.compare("F",     Qt::CaseInsensitive) == 0 ||
        trimmed.compare("false", Qt::CaseInsensitive) == 0)
        return false;

    // Quoted string: strip surrounding single-quotes.
    if (trimmed.startsWith('\'') && trimmed.endsWith('\''))
        return trimmed.mid(1, trimmed.length() - 2).trimmed();

    // Integer (must have no decimal point or exponent character).
    bool ok = false;
    const qlonglong intVal = trimmed.toLongLong(&ok);
    if (ok && !trimmed.contains('.') && !trimmed.contains('e', Qt::CaseInsensitive))
        return intVal;

    // Floating-point.
    const double dblVal = trimmed.toDouble(&ok);
    if (ok)
        return dblVal;

    // Fall back to the raw string.
    return value;
}


// =============================================================================
// Coordinate parsing helpers
// =============================================================================

double FitsHeaderUtils::parseHMSComponents(double h, double m, double s, double sign)
{
    return sign * (std::abs(h) + m / 60.0 + s / 3600.0);
}

double FitsHeaderUtils::parseRA(const QString& str, bool* ok)
{
    if (ok) *ok = false;

    const QString trimmed = str.trimmed();

    // Try decimal degrees first.
    bool parseOk = false;
    const double val = trimmed.toDouble(&parseOk);
    if (parseOk)
    {
        if (ok) *ok = true;
        return val;
    }

    // Sexagesimal HMS patterns: "HH MM SS.ss", "HH:MM:SS.ss", "HHhMMmSS.sss".
    static const QRegularExpression hmsPattern(
        R"(^\s*(\d{1,2})[:\s]+(\d{1,2})[:\s]+(\d+\.?\d*)\s*$)");

    static const QRegularExpression hmsUnits(
        R"(^\s*(\d{1,2})h\s*(\d{1,2})m\s*(\d+\.?\d*)s?\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = hmsPattern.match(trimmed);
    if (!match.hasMatch())
        match = hmsUnits.match(trimmed);

    if (match.hasMatch())
    {
        const double h = match.captured(1).toDouble();
        const double m = match.captured(2).toDouble();
        const double s = match.captured(3).toDouble();

        if (ok) *ok = true;
        // Convert from hours to degrees.
        return parseHMSComponents(h, m, s, 1.0) * 15.0;
    }

    return 0.0;
}

double FitsHeaderUtils::parseDec(const QString& str, bool* ok)
{
    if (ok) *ok = false;

    const QString trimmed = str.trimmed();

    // Try decimal degrees first.
    bool parseOk = false;
    const double val = trimmed.toDouble(&parseOk);
    if (parseOk)
    {
        if (ok) *ok = true;
        return val;
    }

    // Sexagesimal DMS patterns: "-DD MM SS.ss", "-DD:MM:SS.ss", "-DDdMMmSS.sss".
    static const QRegularExpression dmsPattern(
        R"(^\s*([+-]?\d{1,3})[:\s]+(\d{1,2})[:\s]+(\d+\.?\d*)\s*$)");

    static const QRegularExpression dmsUnits(
        R"(^\s*([+-]?\d{1,3})d\s*(\d{1,2})m\s*(\d+\.?\d*)s?\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = dmsPattern.match(trimmed);
    if (!match.hasMatch())
        match = dmsUnits.match(trimmed);

    if (match.hasMatch())
    {
        const QString dStr  = match.captured(1);
        const double  d     = std::abs(dStr.toDouble());
        const double  m     = match.captured(2).toDouble();
        const double  s     = match.captured(3).toDouble();
        const double  sign  = dStr.startsWith('-') ? -1.0 : 1.0;

        if (ok) *ok = true;
        return parseHMSComponents(d, m, s, sign);
    }

    return 0.0;
}


// =============================================================================
// Coordinate formatting
// =============================================================================

QString FitsHeaderUtils::formatRAToHMS(double ra, int precision)
{
    // Normalise to [0, 360).
    while (ra <    0.0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;

    const double hours   = ra / 15.0;
    const int    h       = static_cast<int>(hours);
    const double minPart = (hours - h) * 60.0;
    const int    m       = static_cast<int>(minPart);
    const double s       = (minPart - m) * 60.0;

    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 5 + precision, 'f', precision, QChar('0'));
}

QString FitsHeaderUtils::formatDecToDMS(double dec, int precision)
{
    const char sign = (dec >= 0.0) ? '+' : '-';
    dec = std::abs(dec);

    const int    d       = static_cast<int>(dec);
    const double minPart = (dec - d) * 60.0;
    const int    m       = static_cast<int>(minPart);
    const double s       = (minPart - m) * 60.0;

    return QString("%1%2:%3:%4")
        .arg(sign)
        .arg(d, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 5 + precision, 'f', precision, QChar('0'));
}


// =============================================================================
// Minimal header enforcement
// =============================================================================

std::vector<FitsHeaderUtils::HeaderCard>
FitsHeaderUtils::ensureMinimalHeader(const std::vector<HeaderCard>& cards,
                                      const QString&                 filePath)
{
    std::vector<HeaderCard> result = cards;

    [[maybe_unused]] bool hasSimple  = false;
    bool                  hasDateObs = false;

    for (const auto& card : cards)
    {
        if (card.key.compare("SIMPLE",   Qt::CaseInsensitive) == 0) hasSimple  = true;
        if (card.key.compare("DATE-OBS", Qt::CaseInsensitive) == 0) hasDateObs = true;
    }

    // Append a DATE-OBS fallback derived from the file modification time.
    if (!hasDateObs && !filePath.isEmpty())
    {
        const QFileInfo fi(filePath);
        if (fi.exists())
        {
            const QDateTime modified = fi.lastModified();
            if (modified.isValid())
            {
                result.push_back({
                    "DATE-OBS",
                    QString("'%1'").arg(modified.toUTC().toString(Qt::ISODate)),
                    "File modification time (fallback)"
                });
            }
        }
    }

    return result;
}