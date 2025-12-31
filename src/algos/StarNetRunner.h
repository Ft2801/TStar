#ifndef STARNETRUNNER_H
#define STARNETRUNNER_H

#include <QString>
#include <QObject>
#include <vector>
#include <atomic>
#include "ImageBuffer.h"

struct StarNetParams {
    bool isLinear = false;
    bool generateMask = false;
    int stride = 256;
    float upsample = 1.0f; // not really used by native Starnet, but good to have
    bool useGpu = true;
};

class StarNetRunner : public QObject {
    Q_OBJECT
public:
    explicit StarNetRunner(QObject* parent = nullptr);
    bool run(const ImageBuffer& input, ImageBuffer& output, const StarNetParams& params, QString* errorMsg);

private:
    // MTF Utils (standard formula)
    struct MTFParams {
        float s[3];
        float m[3];
        float h[3];
    };

    MTFParams computeMtfParams(const ImageBuffer& img);
    void applyMtf(ImageBuffer& img, const MTFParams& p);
    void invertMtf(ImageBuffer& img, const MTFParams& p);

signals:
    void processOutput(const QString& text);

public slots:
    void cancel() { m_stop = true; }

private:
    std::atomic<bool> m_stop{false};
    QString getExecutablePath();
};

#endif // STARNETRUNNER_H
