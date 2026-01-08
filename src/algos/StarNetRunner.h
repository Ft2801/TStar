#ifndef STARNETRUNNER_H
#define STARNETRUNNER_H

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <vector>
#include <atomic>
#include "ImageBuffer.h"

struct StarNetParams {
    bool isLinear = false;
    bool generateMask = false;
    int stride = 256;
    float upsample = 1.0f;
    bool useGpu = true;
};

// Worker class runs in separate thread
class StarNetWorker : public QObject {
    Q_OBJECT
public:
    explicit StarNetWorker(QObject* parent = nullptr);
    
public slots:
    void process(const ImageBuffer& input, const StarNetParams& params);
    void cancel() { m_stop = true; }

signals:
    void finished(const ImageBuffer& output, const QString& errorMsg);
    void processOutput(const QString& text);

private:
    struct MTFParams {
        float s[3];
        float m[3];
        float h[3];
    };

    MTFParams computeMtfParams(const ImageBuffer& img);
    void applyMtf(ImageBuffer& img, const MTFParams& p);
    void invertMtf(ImageBuffer& img, const MTFParams& p);
    
    std::atomic<bool> m_stop{false};
    QString getExecutablePath();
};

class StarNetRunner : public QObject {
    Q_OBJECT
public:
    explicit StarNetRunner(QObject* parent = nullptr);
    ~StarNetRunner();  // Proper cleanup
    
    // Thread-safe run method
    bool run(const ImageBuffer& input, ImageBuffer& output, const StarNetParams& params, QString* errorMsg);

signals:
    void processOutput(const QString& text);
    void finished();

public slots:
    void cancel() { 
        if (m_worker) {
            m_worker->cancel();
        }
    }

private slots:
    void onWorkerFinished(const ImageBuffer& output, const QString& errorMsg);

private:
    QPointer<QThread> m_thread;         // Safe pointer to thread
    QPointer<StarNetWorker> m_worker;   // Safe pointer to worker
    ImageBuffer m_output;               // Temporary storage for result
    QString m_errorMsg;                 // Temporary storage for error
    bool m_finished = false;            // Synchronization flag
};

#endif // STARNETRUNNER_H
