#ifndef COSMICCLARITYRUNNER_H
#define COSMICCLARITYRUNNER_H

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>
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

// Worker class runs in separate thread
class CosmicClarityWorker : public QObject {
    Q_OBJECT
public:
    explicit CosmicClarityWorker(QObject* parent = nullptr);
    
public slots:
    void process(const ImageBuffer& input, const CosmicClarityParams& params);
    void cancel() { m_stop = true; }

signals:
    void finished(const ImageBuffer& output, const QString& errorMsg);
    void processOutput(const QString& text);

private:
    std::atomic<bool> m_stop{false};
};

class CosmicClarityRunner : public QObject {
    Q_OBJECT
public:
    explicit CosmicClarityRunner(QObject* parent = nullptr);
    ~CosmicClarityRunner();  // Proper cleanup
    
    // Thread-safe run method
    bool run(const ImageBuffer& input, ImageBuffer& output, const CosmicClarityParams& params, QString* errorMsg);

signals:
    void processOutput(const QString& text);
    void workerDone();

public slots:
    void cancel() { 
        if (m_worker) {
            m_worker->cancel();
        }
    }

private slots:
    void onWorkerFinished(const ImageBuffer& output, const QString& errorMsg);

private:
    QPointer<QThread> m_thread;          // Safe pointer to thread
    QPointer<CosmicClarityWorker> m_worker; // Safe pointer to worker
    ImageBuffer m_output;                // Temporary storage for result
    QString m_errorMsg;                  // Temporary storage for error
    bool m_finished = false;             // Synchronization flag
};

#endif // COSMICCLARITYRUNNER_H // COSMICCLARITYRUNNER_H
