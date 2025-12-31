#ifndef MASKLAYER_H
#define MASKLAYER_H

#include <vector>
#include <QString>

struct MaskLayer {
    std::vector<float> data; // Normalized 0.0-1.0
    int width = 0;
    int height = 0;
    QString id;
    QString name;
    QString mode = "replace"; 
    bool inverted = false;
    bool visible = true;
    float opacity = 1.0f; // 0.0 - 1.0
    
    // Helper to check if valid
    bool isValid() const {
        return !data.empty() && width > 0 && height > 0 && data.size() == (size_t)(width * height);
    }
    
    // Access pixel safely
    float pixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0.0f;
        return data[y * width + x];
    }
};

#endif // MASKLAYER_H
