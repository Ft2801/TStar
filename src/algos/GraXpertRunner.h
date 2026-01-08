#ifndef GRAXPERTRUNNER_H
#define GRAXPERTRUNNER_H

#include <QString>
#include <QObject>
#include <QPointer>
#include <QThread>
#include "ImageBuffer.h"
#include <atomic>

struct GraXpertParams {
    bool isDenoise = false; // false = background
    float smoothing = 0.1f; // for BG (0.0 - 1.0)
    float strength = 0.5f; // for Denoise (0.0 - 1.0)
    QString aiVersion = "3.0.1"; 
    bool useGpu = true;
};

// Worker class runs in separate thread
class GraXpertWorker : public QObject {
    Q_OBJECT
public:
    explicit GraXpertWorker(QObject* parent = nullptr);
    
public slots:
    void process(const ImageBuffer& input, const GraXpertParams& params);
    void cancel() { m_stop = true; }

signals:
    void finished(const ImageBuffer& output, const QString& errorMsg);
    void processOutput(const QString& text);

private:
    QString getExecutablePath();
    std::atomic<bool> m_stop{false};
};

class GraXpertRunner : public QObject {
    Q_OBJECT
public:
    explicit GraXpertRunner(QObject* parent = nullptr);
    ~GraXpertRunner();  // Proper cleanup
    
    // Thread-safe run method
    bool run(const ImageBuffer& input, ImageBuffer& output, const GraXpertParams& params, QString* errorMsg);

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
    QPointer<QThread> m_thread;          // Safe pointer to thread
    QPointer<GraXpertWorker> m_worker;   // Safe pointer to worker
    ImageBuffer m_output;                // Temporary storage for result
    QString m_errorMsg;                  // Temporary storage for error
    bool m_finished = false;             // Synchronization flag
};

#endif // GRAXPERTRUNNER_H
