#ifndef GRAXPERTRUNNER_H
#define GRAXPERTRUNNER_H

#include <QString>
#include <QObject>
#include "ImageBuffer.h"
#include <atomic>

struct GraXpertParams {
    bool isDenoise = false; // false = background
    float smoothing = 0.1f; // for BG (0.0 - 1.0)
    float strength = 0.5f; // for Denoise (0.0 - 1.0)
    QString aiVersion = "3.0.1"; 
    bool useGpu = true;
};

class GraXpertRunner : public QObject {
    Q_OBJECT
public:
    explicit GraXpertRunner(QObject* parent = nullptr);
    bool run(const ImageBuffer& input, ImageBuffer& output, const GraXpertParams& params, QString* errorMsg);

signals:
    void processOutput(const QString& text);

public slots:
    void cancel() { m_stop = true; }

private:
    QString getExecutablePath();
    std::atomic<bool> m_stop{false};
};

#endif // GRAXPERTRUNNER_H
