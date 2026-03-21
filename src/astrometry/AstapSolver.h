#ifndef ASTAPSOLVER_H
#define ASTAPSOLVER_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMutex>
#include <atomic>
#include "../ImageBuffer.h"
#include "NativePlateSolver.h" // For NativeSolveResult

class AstapSolver : public QObject {
    Q_OBJECT
public:
    explicit AstapSolver(QObject* parent = nullptr);
    ~AstapSolver() override;

    // Matches NativePlateSolver API
    void solve(const ImageBuffer& image, double raHint, double decHint, double radiusDeg, double pixelScale);
    void cancelSolve();

    static QString getAstapExecutable();
    static QString getAstapDatabasePath(const QString& overridePath = QString());
    static bool containsAstapCatalogs(const QString& path);

signals:
    void logMessage(const QString& msg);
    void finished(const NativeSolveResult& result);

private:
    bool terminateAstapProcess(QProcess& process, int terminateWaitMs);
    bool parseAstapWCS(const QString& wcsFile, int imageHeight, NativeSolveResult& res);
    bool parseAstapIni(const QString& iniFile, int imageHeight, NativeSolveResult& res);

    QMutex m_processMutex;
    QProcess* m_runningProcess = nullptr;
    std::atomic<bool> m_cancelRequested{false};
};

#endif // ASTAPSOLVER_H
