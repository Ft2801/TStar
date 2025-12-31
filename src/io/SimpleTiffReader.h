#ifndef SIMPLETIFFREADER_H
#define SIMPLETIFFREADER_H

#include <QString>
#include <vector>

class SimpleTiffReader {
public:
    static bool readFloat32(const QString& path, int& width, int& height, int& channels, std::vector<float>& data, QString* errorMsg = nullptr, QString* debugInfo = nullptr);
};

#endif // SIMPLETIFFREADER_H
