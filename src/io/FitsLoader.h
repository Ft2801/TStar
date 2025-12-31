#ifndef FITSLOADER_H
#define FITSLOADER_H

#include <QString>
#include "ImageBuffer.h"

class FitsLoader {
public:
    static bool load(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);
};

#endif // FITSLOADER_H
