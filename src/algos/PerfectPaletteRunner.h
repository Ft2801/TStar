#ifndef PERFECTPALETTERUNNER_H
#define PERFECTPALETTERUNNER_H

#include <QObject>
#include <vector>
#include <map>
#include "../ImageBuffer.h"

struct PerfectPaletteParams {
    QString paletteName = "SHO";
    float haFactor = 1.0f;
    float oiiiFactor = 1.0f;
    float siiFactor = 1.0f;
    bool applyStatisticalStretch = true;
};

class PerfectPaletteRunner : public QObject {
    Q_OBJECT
public:
    explicit PerfectPaletteRunner(QObject* parent = nullptr);

    bool run(const ImageBuffer* ha, const ImageBuffer* oiii, const ImageBuffer* sii,
             ImageBuffer& output, const PerfectPaletteParams& params, QString* errorMsg = nullptr);

    // Helper for statistical stretch (0.25 target median)
    static void applyStatisticalStretch(ImageBuffer& buffer, float targetMedian = 0.25f);

signals:
    void processOutput(const QString& msg);

private:
    void mapSHO(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapGeneric(const ImageBuffer& rCh, const ImageBuffer& gCh, const ImageBuffer& bCh, ImageBuffer& out);
    void mapForaxx(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapRealistic1(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
    void mapRealistic2(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out);
};

#endif // PERFECTPALETTERUNNER_H
