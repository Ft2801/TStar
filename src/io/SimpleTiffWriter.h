#ifndef SIMPLETIFFWRITER_H
#define SIMPLETIFFWRITER_H

#include <QString>
#include <vector>

class SimpleTiffWriter {
public:
    enum Format {
        Format_uint8,
        Format_uint16,
        Format_uint32,
        Format_float32
    };

    static bool write(const QString& filename, int width, int height, int channels, Format fmt, const std::vector<float>& data, QString* errorMsg = nullptr);
};

#endif // SIMPLETIFFWRITER_H
