#ifndef FITSHEADERUTILS_H
#define FITSHEADERUTILS_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>
#include <vector>
#include "../ImageBuffer.h"

class FitsHeaderUtils {
public:
    using HeaderCard = ImageBuffer::Metadata::HeaderCard;
    using Metadata = ImageBuffer::Metadata;
    
    static const QStringList structuralKeywords;
    
    static std::vector<HeaderCard> dropInvalidCards(const std::vector<HeaderCard>& cards);
    
    static std::vector<HeaderCard> buildWCSHeader(const Metadata& meta);
    
    static bool hasValidWCS(const Metadata& meta);
    
    static std::vector<HeaderCard> parseXISFFitsKeywords(const QMap<QString, QVariant>& fitsKeywords);
    
    static void applyXISFProperties(const QVariantMap& props, Metadata& meta);
    
    static QVariant coerceFitsValue(const QString& value);
    
    static double parseRA(const QString& str, bool* ok = nullptr);
    
    static double parseDec(const QString& str, bool* ok = nullptr);
    
    static QString formatRAToHMS(double ra, int precision = 2);
    
    static QString formatDecToDMS(double dec, int precision = 2);
    
    static std::vector<HeaderCard> ensureMinimalHeader(const std::vector<HeaderCard>& cards,
                                                       const QString& filePath = QString());

private:
    // Helper to check if a key is valid FITS keyword
    static bool isValidKeyword(const QString& key);
    
    // Helper to parse HMS/DMS components
    static double parseHMSComponents(double h, double m, double s, double sign);
};

#endif // FITSHEADERUTILS_H
