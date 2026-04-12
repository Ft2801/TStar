// =============================================================================
// JSApp.cpp
//
// Implementation of the App global object for the TStar JS scripting API.
// =============================================================================

#include "JSApp.h"
#include "JSApi.h"
#include "JSRuntime.h"
#include "MainWindow.h"
#include "ImageViewer.h"
#include "widgets/CustomMdiSubWindow.h"

#include <QMdiArea>
#include <QThread>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace Scripting {

JSApp::JSApp(MainWindow* mainWindow, JSRuntime* runtime, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_runtime(runtime)
{
}

QString JSApp::version() const
{
    return QCoreApplication::applicationVersion();
}

QString JSApp::appName() const
{
    return QStringLiteral("TStar");
}

QObject* JSApp::activeWindow()
{
    if (!m_mainWindow) return nullptr;

    ImageViewer* viewer = m_mainWindow->currentViewer();
    if (!viewer) return nullptr;

    // Create a JSView that references the viewer's buffer directly
    return new JSView(viewer, m_mainWindow, this);
}

QVariantList JSApp::windows()
{
    QVariantList result;
    if (!m_mainWindow) return result;

    // Find the MDI area by walking the children
    QMdiArea* mdiArea = m_mainWindow->findChild<QMdiArea*>();
    if (!mdiArea) return result;

    for (QMdiSubWindow* sub : mdiArea->subWindowList()) {
        auto* customSub = qobject_cast<CustomMdiSubWindow*>(sub);
        if (!customSub) continue;

        // Get the ImageViewer inside the subwindow
        ImageViewer* viewer = customSub->findChild<ImageViewer*>();
        if (!viewer) continue;

        auto* view = new JSView(viewer, m_mainWindow, this);
        result << QVariant::fromValue(static_cast<QObject*>(view));
    }

    return result;
}

QObject* JSApp::open(const QString& filePath)
{
    if (!m_mainWindow || filePath.isEmpty()) return nullptr;

    // Load the image into a buffer
    ImageBuffer buffer;
    if (!buffer.loadStandard(filePath)) {
        emit m_runtime->standardError(
            tr("Failed to open image: %1").arg(filePath));
        return nullptr;
    }

    // Create a new window for it
    QFileInfo fi(filePath);
    QString title = fi.fileName();
    CustomMdiSubWindow* sub = nullptr;

    QMetaObject::invokeMethod(m_mainWindow, [this, &sub, buffer, title]() {
        sub = m_mainWindow->createNewImageWindow(buffer, title);
    }, Qt::BlockingQueuedConnection);

    if (!sub) return nullptr;

    // Find the viewer in the new subwindow
    ImageViewer* viewer = sub->findChild<ImageViewer*>();
    if (!viewer) return nullptr;

    return new JSView(viewer, m_mainWindow, this);
}

void JSApp::sleep(int milliseconds)
{
    if (milliseconds > 0 && milliseconds <= 60000) {
        QThread::msleep(milliseconds);
    }
}

void JSApp::undo()
{
    if (!m_mainWindow) return;
    QMetaObject::invokeMethod(m_mainWindow, "undo", Qt::QueuedConnection);
}

void JSApp::redo()
{
    if (!m_mainWindow) return;
    QMetaObject::invokeMethod(m_mainWindow, "redo", Qt::QueuedConnection);
}

void JSApp::log(const QString& message, int type)
{
    if (!m_mainWindow) return;

    MainWindow::LogType logType;
    switch (type) {
        case 1:  logType = MainWindow::Log_Success; break;
        case 2:  logType = MainWindow::Log_Warning; break;
        case 3:  logType = MainWindow::Log_Error;   break;
        default: logType = MainWindow::Log_Info;    break;
    }

    QMetaObject::invokeMethod(m_mainWindow, [this, message, logType]() {
        m_mainWindow->log(message, logType);
    }, Qt::QueuedConnection);
}

} // namespace Scripting
