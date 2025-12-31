#ifndef COSMICCLARITYRUNNER_H
#define COSMICCLARITYRUNNER_H

#include <QString>
#include <QObject>
#include "ImageBuffer.h"
#include <atomic>

struct CosmicClarityParams {
    enum Mode { Mode_Sharpen, Mode_Denoise, Mode_Both, Mode_SuperRes };
    Mode mode = Mode_Sharpen;
    
    // Sharpen
    QString sharpenMode = "Both"; // Both, Stellar Only, Non-Stellar Only
    float stellarAmount = 0.5f;
    float nonStellarAmount = 0.5f;
    float nonStellarPSF = 3.0f;
    bool separateChannelsSharpen = false;
    bool autoPSF = true;

    // Denoise
    float denoiseLum = 0.5f;
    float denoiseColor = 0.5f;
    QString denoiseMode = "full"; // full, luminance
    bool separateChannelsDenoise = false;

    // SuperRes
    QString scaleFactor = "2x";

    bool useGpu = true;
};

class CosmicClarityRunner : public QObject {
    Q_OBJECT
public:
    explicit CosmicClarityRunner(QObject* parent = nullptr);
    bool run(const ImageBuffer& input, ImageBuffer& output, const CosmicClarityParams& params, QString* errorMsg);

signals:
    void processOutput(const QString& text);

public slots:
    void cancel() { m_stop = true; }

private:
    QString getCosmicFolder();
    QString getExecutableName(CosmicClarityParams::Mode mode);
    std::atomic<bool> m_stop{false};
};

#endif // COSMICCLARITYRUNNER_H
