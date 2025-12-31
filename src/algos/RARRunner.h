#ifndef RARRUNNER_H
#define RARRUNNER_H

#include <QObject>
#include <QString>
#include "ImageBuffer.h"
#include <atomic>

struct RARParams {
    QString modelPath;
    int patchSize = 512;
    int overlap = 64;
    QString provider = "CPU"; // CPU, CUDA, DirectML
};

class RARRunner : public QObject {
    Q_OBJECT
public:
    explicit RARRunner(QObject* parent = nullptr);
    bool run(const ImageBuffer& input, ImageBuffer& output, const RARParams& params, QString* errorMsg);

signals:
    void processOutput(const QString& text);

public slots:
    void cancel() { m_stop = true; }

private:
    std::atomic<bool> m_stop{false};
};

#endif // RARRUNNER_H
