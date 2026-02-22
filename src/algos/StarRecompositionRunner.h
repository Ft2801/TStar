#ifndef STARRECOMPOSITIONRUNNER_H
#define STARRECOMPOSITIONRUNNER_H

#include <QObject>
#include "../ImageBuffer.h"

struct StarRecompositionParams {
    ImageBuffer::GHSParams ghs;
};

class StarRecompositionRunner : public QObject {
    Q_OBJECT
public:
    explicit StarRecompositionRunner(QObject* parent = nullptr);

    bool run(const ImageBuffer& starless, const ImageBuffer& stars, ImageBuffer& output, const StarRecompositionParams& params, QString* errorMsg = nullptr);

signals:
    void processOutput(const QString& msg);
};

#endif // STARRECOMPOSITIONRUNNER_H
