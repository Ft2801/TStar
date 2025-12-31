#ifndef STARSTRETCHRUNNER_H
#define STARSTRETCHRUNNER_H

#include "ImageBuffer.h"
#include <QString>
#include <QObject>

struct StarStretchParams {
    float stretchAmount = 5.0f; // 0.0 to 8.0
    float colorBoost = 1.0f;    // 0.0 to 2.0
    bool scnr = false;
};

class StarStretchRunner : public QObject {
    Q_OBJECT
public:
    explicit StarStretchRunner(QObject* parent = nullptr);

    // Runs the processing in-place or to output
    bool run(const ImageBuffer& input, ImageBuffer& output, const StarStretchParams& params, QString* errorMsg = nullptr);

signals:
    void processOutput(const QString& msg);
    void progressValue(int percent);
};

#endif // STARSTRETCHRUNNER_H
