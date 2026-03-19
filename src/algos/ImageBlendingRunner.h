#ifndef IMAGEBLENDINGRUNNER_H
#define IMAGEBLENDINGRUNNER_H

#include <QObject>
#include "../ImageBuffer.h"

struct ImageBlendingParams {
    enum BlendMode {
        Normal,
        Multiply,
        Screen,
        Overlay,
        Add,
        Subtract,
        Difference,
        SoftLight,
        HardLight
    };

    BlendMode mode = Normal;
    float opacity = 1.0f;
    
    // Masking params
    float lowRange = 0.0f;
    float highRange = 1.0f;
    float feather = 0.0f;
    
    // Target channel for Color + Mono (0=R, 1=G, 2=B, 3=All)
    int targetChannel = 3; 
};

class ImageBlendingRunner : public QObject {
    Q_OBJECT
public:
    explicit ImageBlendingRunner(QObject* parent = nullptr);

    /**
     * @brief Blends top into base according to params.
     * @param base The bottom image (will be modified if result is not provided, or used as reference)
     * @param top The image to apply on top
     * @param result Output buffer
     */
    bool run(const ImageBuffer& base, const ImageBuffer& top, ImageBuffer& result, const ImageBlendingParams& params, QString* errorMsg = nullptr);

private:
    float blendPixel(float b, float t, ImageBlendingParams::BlendMode mode);
};

#endif // IMAGEBLENDINGRUNNER_H
