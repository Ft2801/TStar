#include "FitsHeaderUtils.h"
#include <QRegularExpression>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>
#include <QDebug>

// Static member initialization
const QStringList FitsHeaderUtils::structuralKeywords = {
    "SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "NAXIS3",
    "EXTEND", "BZERO", "BSCALE", "END"
};

bool FitsHeaderUtils::isValidKeyword(const QString& key) {
    if (key.isEmpty()) return false;
    
    // HIERARCH allows longer keywords
    if (key.startsWith("HIERARCH ", Qt::CaseInsensitive)) {
        return true;
    }
    
    // Standard FITS keywords: max 8 chars, uppercase letters, digits, - _
    if (key.length() > 8) return false;
    
    static QRegularExpression validChars("^[A-Z0-9_-]+$", QRegularExpression::CaseInsensitiveOption);
    return validChars.match(key).hasMatch();
}

std::vector<FitsHeaderUtils::HeaderCard> FitsHeaderUtils::dropInvalidCards(
        const std::vector<HeaderCard>& cards) {
    std::vector<HeaderCard> result;
    result.reserve(cards.size());
    
    for (const auto& card : cards) {
        QString key = card.key.trimmed().toUpper();
        
        // Skip empty keys
        if (key.isEmpty()) continue;
        
        // Skip structural keywords (handled separately)
        if (structuralKeywords.contains(key)) continue;
        
        // Skip invalid keywords
        if (!isValidKeyword(key)) {
            qWarning() << "Dropping invalid FITS keyword:" << key;
            continue;
        }
        
        // Skip if value is too long for FITS (68 chars normally, more with CONTINUE)
        // For now, just warn but include
        if (card.value.length() > 68) {
            qWarning() << "FITS keyword value too long, may cause issues:" << key;
        }
        
        result.push_back(card);
    }
    
    return result;
}

bool FitsHeaderUtils::hasValidWCS(const Metadata& meta) {
    // Need reference pixel, coordinates, and non-singular CD matrix
    bool hasCoords = (meta.ra != 0 || meta.dec != 0);
    
    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    bool hasMatrix = std::abs(det) > 1e-20;
    
    return hasCoords && hasMatrix;
}

std::vector<FitsHeaderUtils::HeaderCard> FitsHeaderUtils::buildWCSHeader(const Metadata& meta) {
    std::vector<HeaderCard> result;
    
    if (!hasValidWCS(meta)) {
        return result;
    }
    
    // CTYPE
    QString ctype1 = meta.ctype1.isEmpty() ? "RA---TAN" : meta.ctype1;
    QString ctype2 = meta.ctype2.isEmpty() ? "DEC--TAN" : meta.ctype2;
    
    // Add SIP suffix if we have SIP coefficients
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0) {
        if (!ctype1.endsWith("-SIP")) ctype1 += "-SIP";
        if (!ctype2.endsWith("-SIP")) ctype2 += "-SIP";
    }
    
    result.push_back({"CTYPE1", QString("'%1'").arg(ctype1), "Coordinate type"});
    result.push_back({"CTYPE2", QString("'%1'").arg(ctype2), "Coordinate type"});
    result.push_back({"EQUINOX", QString::number(meta.equinox, 'f', 1), "Equinox of coordinates"});
    
    // Reference point
    result.push_back({"CRVAL1", QString::number(meta.ra, 'f', 10), "RA at reference pixel"});
    result.push_back({"CRVAL2", QString::number(meta.dec, 'f', 10), "Dec at reference pixel"});
    result.push_back({"CRPIX1", QString::number(meta.crpix1, 'f', 4), "Reference pixel X"});
    result.push_back({"CRPIX2", QString::number(meta.crpix2, 'f', 4), "Reference pixel Y"});
    
    // CD matrix
    result.push_back({"CD1_1", QString::number(meta.cd1_1, 'e', 12), ""});
    result.push_back({"CD1_2", QString::number(meta.cd1_2, 'e', 12), ""});
    result.push_back({"CD2_1", QString::number(meta.cd2_1, 'e', 12), ""});
    result.push_back({"CD2_2", QString::number(meta.cd2_2, 'e', 12), ""});
    
    // LONPOLE/LATPOLE if non-default
    if (std::abs(meta.lonpole - 180.0) > 0.001) {
        result.push_back({"LONPOLE", QString::number(meta.lonpole, 'f', 6), ""});
    }
    if (std::abs(meta.latpole) > 0.001) {
        result.push_back({"LATPOLE", QString::number(meta.latpole, 'f', 6), ""});
    }
    
    // SIP coefficients
    if (meta.sipOrderA > 0) {
        result.push_back({"A_ORDER", QString::number(meta.sipOrderA), "SIP polynomial order"});
    }
    if (meta.sipOrderB > 0) {
        result.push_back({"B_ORDER", QString::number(meta.sipOrderB), "SIP polynomial order"});
    }
    if (meta.sipOrderAP > 0) {
        result.push_back({"AP_ORDER", QString::number(meta.sipOrderAP), "SIP inverse polynomial order"});
    }
    if (meta.sipOrderBP > 0) {
        result.push_back({"BP_ORDER", QString::number(meta.sipOrderBP), "SIP inverse polynomial order"});
    }
    
    // Write all SIP coefficients
    for (auto it = meta.sipCoeffs.constBegin(); it != meta.sipCoeffs.constEnd(); ++it) {
        result.push_back({it.key(), QString::number(it.value(), 'e', 15), ""});
    }
    
    return result;
}

std::vector<FitsHeaderUtils::HeaderCard> FitsHeaderUtils::parseXISFFitsKeywords(
        const QMap<QString, QVariant>& fitsKeywords) {
    std::vector<HeaderCard> result;
    
    for (auto it = fitsKeywords.constBegin(); it != fitsKeywords.constEnd(); ++it) {
        QString key = it.key();
        QVariant val = it.value();
        
        QString value;
        QString comment;
        
        // XISF format: key -> [{value: "...", comment: "..."}] or key -> "value"
        if (val.typeId() == QMetaType::QVariantList) {
            QVariantList list = val.toList();
            if (!list.isEmpty()) {
                QVariant first = list.first();
                if (first.typeId() == QMetaType::QVariantMap) {
                    QVariantMap map = first.toMap();
                    value = map.value("value").toString();
                    comment = map.value("comment").toString();
                } else {
                    value = first.toString();
                }
            }
        } else if (val.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = val.toMap();
            value = map.value("value").toString();
            comment = map.value("comment").toString();
        } else {
            value = val.toString();
        }
        
        result.push_back({key, value, comment});
    }
    
    return result;
}

void FitsHeaderUtils::applyXISFProperties(const QVariantMap& props, Metadata& meta) {
    
    // Reference image coordinates (CRPIX)
    if (props.contains("PCL:AstrometricSolution:ReferenceImageCoordinates")) {
        QVariant val = props.value("PCL:AstrometricSolution:ReferenceImageCoordinates");
        QVariantList coords = val.toMap().value("value").toList();
        if (coords.isEmpty()) coords = val.toList();
        if (coords.size() >= 2) {
            meta.crpix1 = coords[0].toDouble();
            meta.crpix2 = coords[1].toDouble();
        }
    }
    
    // Reference celestial coordinates (CRVAL)
    if (props.contains("PCL:AstrometricSolution:ReferenceCelestialCoordinates")) {
        QVariant val = props.value("PCL:AstrometricSolution:ReferenceCelestialCoordinates");
        QVariantList coords = val.toMap().value("value").toList();
        if (coords.isEmpty()) coords = val.toList();
        if (coords.size() >= 2) {
            meta.ra = coords[0].toDouble();
            meta.dec = coords[1].toDouble();
        }
    }
    
    // Linear transformation matrix (CD matrix)
    if (props.contains("PCL:AstrometricSolution:LinearTransformationMatrix")) {
        QVariant val = props.value("PCL:AstrometricSolution:LinearTransformationMatrix");
        QVariantList matrix = val.toMap().value("value").toList();
        if (matrix.isEmpty()) matrix = val.toList();
        if (matrix.size() >= 2) {
            QVariantList row0 = matrix[0].toList();
            QVariantList row1 = matrix[1].toList();
            if (row0.size() >= 2 && row1.size() >= 2) {
                meta.cd1_1 = row0[0].toDouble();
                meta.cd1_2 = row0[1].toDouble();
                meta.cd2_1 = row1[0].toDouble();
                meta.cd2_2 = row1[1].toDouble();
            }
        }
    }
    
    // Pixel scale
    if (props.contains("PCL:AstrometricSolution:PixelSize")) {
        QVariant val = props.value("PCL:AstrometricSolution:PixelSize");
        double pixScale = val.toMap().value("value").toDouble();
        if (pixScale == 0) pixScale = val.toDouble();
        if (pixScale > 0) {
            meta.pixelSize = pixScale;
        }
    }
    
    // Focal length
    if (props.contains("Instrument:Telescope:FocalLength")) {
        QVariant val = props.value("Instrument:Telescope:FocalLength");
        double fl = val.toMap().value("value").toDouble();
        if (fl == 0) fl = val.toDouble();
        if (fl > 0) {
            meta.focalLength = fl * 1000.0;  // Convert m to mm
        }
    }
    
    // Object name
    if (props.contains("Observation:Object:Name")) {
        QVariant val = props.value("Observation:Object:Name");
        QString name = val.toMap().value("value").toString();
        if (name.isEmpty()) name = val.toString();
        if (!name.isEmpty()) {
            meta.objectName = name;
        }
    }
    
    // Observation time
    if (props.contains("Observation:Time:Start")) {
        QVariant val = props.value("Observation:Time:Start");
        QString time = val.toMap().value("value").toString();
        if (time.isEmpty()) time = val.toString();
        if (!time.isEmpty()) {
            meta.dateObs = time;
        }
    }
    
    // Store all properties for later use
    meta.xisfProperties = props;
}

QVariant FitsHeaderUtils::coerceFitsValue(const QString& value) {
    if (value.isEmpty()) return QVariant();
    
    QString trimmed = value.trimmed();
    
    // Boolean
    if (trimmed.compare("T", Qt::CaseInsensitive) == 0 ||
        trimmed.compare("true", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmed.compare("F", Qt::CaseInsensitive) == 0 ||
        trimmed.compare("false", Qt::CaseInsensitive) == 0) {
        return false;
    }
    
    // Remove quotes from string values
    if (trimmed.startsWith("'") && trimmed.endsWith("'")) {
        return trimmed.mid(1, trimmed.length() - 2).trimmed();
    }
    
    // Try integer
    bool ok;
    qlonglong intVal = trimmed.toLongLong(&ok);
    if (ok && !trimmed.contains('.') && !trimmed.contains('e', Qt::CaseInsensitive)) {
        return intVal;
    }
    
    // Try double
    double dblVal = trimmed.toDouble(&ok);
    if (ok) {
        return dblVal;
    }
    
    // String
    return value;
}

double FitsHeaderUtils::parseHMSComponents(double h, double m, double s, double sign) {
    return sign * (std::abs(h) + m / 60.0 + s / 3600.0);
}

double FitsHeaderUtils::parseRA(const QString& str, bool* ok) {
    if (ok) *ok = false;
    QString trimmed = str.trimmed();
    
    // Try decimal degrees first
    bool parseOk;
    double val = trimmed.toDouble(&parseOk);
    if (parseOk) {
        if (ok) *ok = true;
        return val;
    }
    
    // HMS format: "12 34 56.78" or "12:34:56.78" or "12h34m56.78s"
    static QRegularExpression hmsPattern(
        R"(^\s*(\d{1,2})[:\s]+(\d{1,2})[:\s]+(\d+\.?\d*)\s*$)"
    );
    static QRegularExpression hmsUnits(
        R"(^\s*(\d{1,2})h\s*(\d{1,2})m\s*(\d+\.?\d*)s?\s*$)",
        QRegularExpression::CaseInsensitiveOption
    );
    
    QRegularExpressionMatch match = hmsPattern.match(trimmed);
    if (!match.hasMatch()) {
        match = hmsUnits.match(trimmed);
    }
    
    if (match.hasMatch()) {
        double h = match.captured(1).toDouble();
        double m = match.captured(2).toDouble();
        double s = match.captured(3).toDouble();
        
        if (ok) *ok = true;
        // RA: hours to degrees (multiply by 15)
        return parseHMSComponents(h, m, s, 1.0) * 15.0;
    }
    
    return 0.0;
}

double FitsHeaderUtils::parseDec(const QString& str, bool* ok) {
    if (ok) *ok = false;
    QString trimmed = str.trimmed();
    
    // Try decimal degrees first
    bool parseOk;
    double val = trimmed.toDouble(&parseOk);
    if (parseOk) {
        if (ok) *ok = true;
        return val;
    }
    
    // DMS format: "-45 30 15.6" or "-45:30:15.6" or "-45d30m15.6s"
    static QRegularExpression dmsPattern(
        R"(^\s*([+-]?\d{1,3})[:\s]+(\d{1,2})[:\s]+(\d+\.?\d*)\s*$)"
    );
    static QRegularExpression dmsUnits(
        R"(^\s*([+-]?\d{1,3})d\s*(\d{1,2})m\s*(\d+\.?\d*)s?\s*$)",
        QRegularExpression::CaseInsensitiveOption
    );
    
    QRegularExpressionMatch match = dmsPattern.match(trimmed);
    if (!match.hasMatch()) {
        match = dmsUnits.match(trimmed);
    }
    
    if (match.hasMatch()) {
        QString dStr = match.captured(1);
        double d = std::abs(dStr.toDouble());
        double m = match.captured(2).toDouble();
        double s = match.captured(3).toDouble();
        double sign = dStr.startsWith('-') ? -1.0 : 1.0;
        
        if (ok) *ok = true;
        return parseHMSComponents(d, m, s, sign);
    }
    
    return 0.0;
}

QString FitsHeaderUtils::formatRAToHMS(double ra, int precision) {
    // Normalize to 0-360
    while (ra < 0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
    
    double hours = ra / 15.0;  // degrees to hours
    int h = static_cast<int>(hours);
    double minPart = (hours - h) * 60.0;
    int m = static_cast<int>(minPart);
    double s = (minPart - m) * 60.0;
    
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 5 + precision, 'f', precision, QChar('0'));
}

QString FitsHeaderUtils::formatDecToDMS(double dec, int precision) {
    char sign = dec >= 0 ? '+' : '-';
    dec = std::abs(dec);
    
    int d = static_cast<int>(dec);
    double minPart = (dec - d) * 60.0;
    int m = static_cast<int>(minPart);
    double s = (minPart - m) * 60.0;
    
    return QString("%1%2:%3:%4")
        .arg(sign)
        .arg(d, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 5 + precision, 'f', precision, QChar('0'));
}

std::vector<FitsHeaderUtils::HeaderCard> FitsHeaderUtils::ensureMinimalHeader(
        const std::vector<HeaderCard>& cards, const QString& filePath) {
    std::vector<HeaderCard> result = cards;
    
    // Check if SIMPLE is present
    bool hasSimple = false;
    bool hasDateObs = false;
    
    for (const auto& card : cards) {
        if (card.key.compare("SIMPLE", Qt::CaseInsensitive) == 0) hasSimple = true;
        if (card.key.compare("DATE-OBS", Qt::CaseInsensitive) == 0) hasDateObs = true;
    }
    
    // If no DATE-OBS, try to get from file modification time
    if (!hasDateObs && !filePath.isEmpty()) {
        QFileInfo fi(filePath);
        if (fi.exists()) {
            QDateTime modified = fi.lastModified();
            if (modified.isValid()) {
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
