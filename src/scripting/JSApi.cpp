// =============================================================================
// JSApi.cpp
//
// Implementation of JSImage and JSView — the data layer of the scripting API.
// =============================================================================

#include "JSApi.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "widgets/CustomMdiSubWindow.h"

#include <QDebug>

namespace Scripting {

// =============================================================================
// JSImage — Construction
// =============================================================================

JSImage::JSImage(QObject* parent)
    : QObject(parent)
    , m_externalBuffer(nullptr)
    , m_ownsBuffer(true)
{
}

JSImage::JSImage(const ImageBuffer& buffer, QObject* parent)
    : QObject(parent)
    , m_ownedBuffer(buffer)
    , m_externalBuffer(nullptr)
    , m_ownsBuffer(true)
{
}

JSImage::JSImage(ImageBuffer* externalBuffer, QObject* parent)
    : QObject(parent)
    , m_externalBuffer(externalBuffer)
    , m_ownsBuffer(false)
{
}

JSImage::JSImage(ImageBuffer* externalBuffer, ImageViewer* viewer, QObject* parent)
    : QObject(parent)
    , m_externalBuffer(externalBuffer)
    , m_viewer(viewer)
    , m_ownsBuffer(false)
{
}

// =============================================================================
// JSImage — Internal API for Processes
// =============================================================================

void JSImage::pushUndo(const QString& name)
{
    if (m_viewer) {
        // Use a lambda to avoid requiring the method to be Q_INVOKABLE in the viewer.
        // BlockingQueuedConnection ensures the undo state is captured BEFORE modification.
        QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data(), name]() {
            if (v) v->pushUndo(name);
        }, Qt::BlockingQueuedConnection);
    }
}

void JSImage::refresh()
{
    if (m_viewer) {
        QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data()]() {
            if (v) v->refreshDisplay(true);
        }, Qt::QueuedConnection);
    }
}

void JSImage::setBuffer(const ImageBuffer& buffer)
{
    if (m_ownsBuffer) {
        m_ownedBuffer = buffer;
    } else {
        *m_externalBuffer = buffer;
    }
}

// =============================================================================
// JSImage — Property Accessors
// =============================================================================

int JSImage::width() const
{
    const ImageBuffer& buf = buffer();
    return buf.isValid() ? buf.width() : 0;
}

int JSImage::height() const
{
    const ImageBuffer& buf = buffer();
    return buf.isValid() ? buf.height() : 0;
}

int JSImage::channels() const
{
    const ImageBuffer& buf = buffer();
    return buf.isValid() ? buf.channels() : 0;
}

bool JSImage::isValid() const
{
    return buffer().isValid();
}

// =============================================================================
// JSImage — Buffer Access
// =============================================================================

ImageBuffer& JSImage::buffer()
{
    return m_ownsBuffer ? m_ownedBuffer : *m_externalBuffer;
}

const ImageBuffer& JSImage::buffer() const
{
    return m_ownsBuffer ? m_ownedBuffer : *m_externalBuffer;
}

// =============================================================================
// JSImage — Pixel Access
// =============================================================================

double JSImage::getPixel(int x, int y, int channel) const
{
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return 0.0;
    if (x < 0 || x >= buf.width() || y < 0 || y >= buf.height()) return 0.0;
    if (channel < 0 || channel >= buf.channels()) return 0.0;

    return static_cast<double>(buf.getPixelValue(x, y, channel));
}

void JSImage::setPixel(int x, int y, int channel, double value)
{
    ImageBuffer& buf = buffer();
    if (!buf.isValid()) return;
    if (x < 0 || x >= buf.width() || y < 0 || y >= buf.height()) return;
    if (channel < 0 || channel >= buf.channels()) return;

    buf.value(x, y, channel) = static_cast<float>(value);
}

// =============================================================================
// JSImage — File I/O
// =============================================================================

bool JSImage::load(const QString& filePath)
{
    // When loading, always use the owned buffer
    if (!m_ownsBuffer) {
        // Switch to owned mode — we copy the data into our own buffer
        m_ownedBuffer = ImageBuffer();
        m_externalBuffer = nullptr;
        m_ownsBuffer = true;
    }

    return m_ownedBuffer.loadStandard(filePath);
}

bool JSImage::save(const QString& filePath, const QString& format, int bitDepth) const
{
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return false;

    ImageBuffer::BitDepth depth;
    switch (bitDepth) {
        case 0:  depth = ImageBuffer::Depth_8Int;    break;
        case 1:  depth = ImageBuffer::Depth_16Int;   break;
        case 2:  depth = ImageBuffer::Depth_32Int;   break;
        default: depth = ImageBuffer::Depth_32Float; break;
    }

    QString fmt = format.toLower();
    if (fmt == "xisf") {
        return buf.saveXISF(filePath, depth);
    }

    QString errorMsg;
    return buf.save(filePath, fmt, depth, &errorMsg);
}

// =============================================================================
// JSImage — Operations
// =============================================================================

QObject* JSImage::clone() const
{
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return nullptr;

    return new JSImage(buf, nullptr);
}

// =============================================================================
// JSImage — Statistics
// =============================================================================

QVariantList JSImage::medians() const
{
    QVariantList result;
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return result;

    for (int c = 0; c < buf.channels(); ++c) {
        result << static_cast<double>(buf.getChannelMedian(c));
    }
    return result;
}

QVariantList JSImage::computeHistogram(int bins) const
{
    QVariantList result;
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return result;

    auto hist = buf.computeHistogram(bins);
    for (const auto& channelHist : hist) {
        QVariantList chList;
        for (int val : channelHist) {
            chList << val;
        }
        result << QVariant(chList);
    }
    return result;
}

QVariantMap JSImage::getStatistics() const
{
    QVariantMap result;
    const ImageBuffer& buf = buffer();
    if (!buf.isValid()) return result;

    QVariantList medianList;
    for (int c = 0; c < buf.channels(); ++c) {
        medianList << static_cast<double>(buf.getChannelMedian(c));
    }
    result["median"] = medianList;

    // Compute min/max/mean from the data
    QVariantList minList, maxList, meanList;
    const auto& data = buf.data();
    int w = buf.width(), h = buf.height(), ch = buf.channels();
    size_t pixelCount = static_cast<size_t>(w) * h;

    for (int c = 0; c < ch; ++c) {
        float minVal = 1.0f, maxVal = 0.0f;
        double sum = 0.0;
        for (size_t i = 0; i < pixelCount; ++i) {
            float v = data[i * ch + c];
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            sum += v;
        }
        minList << static_cast<double>(minVal);
        maxList << static_cast<double>(maxVal);
        meanList << (sum / pixelCount);
    }

    result["min"] = minList;
    result["max"] = maxList;
    result["mean"] = meanList;

    return result;
}

// =============================================================================
// JSImage — Metadata
// =============================================================================

QString JSImage::getHeaderValue(const QString& key) const
{
    return buffer().getHeaderValue(key);
}

QString JSImage::objectName() const
{
    return buffer().metadata().objectName;
}

QString JSImage::filePath() const
{
    return buffer().metadata().filePath;
}

// =============================================================================
// JSView
// =============================================================================

JSView::JSView(ImageViewer* viewer, MainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_viewer(viewer)
    , m_mainWindow(mainWindow)
{
    if (m_viewer) {
        m_image = new JSImage(&m_viewer->getBuffer(), m_viewer.data(), this);
    }
}

ImageViewer* JSView::viewer() const
{
    return m_viewer;
}

QString JSView::title() const
{
    if (!m_viewer) return QString();

    // Try to get the title from the parent subwindow
    QWidget* parent = m_viewer->parentWidget();
    if (parent) {
        auto* sub = qobject_cast<CustomMdiSubWindow*>(parent);
        if (sub) return sub->windowTitle();
    }
    return m_viewer->windowTitle();
}

QObject* JSView::image() const
{
    return m_image;
}

QString JSView::filePath() const
{
    if (!m_viewer) return QString();
    return m_viewer->getBuffer().metadata().filePath;
}

void JSView::refresh()
{
    if (m_viewer) {
        // Use a lambda for thread-safe UI update
        QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data()]() {
            if (v) v->refreshDisplay(true);
        }, Qt::QueuedConnection);
    }
}

void JSView::pushUndo(const QString& name)
{
    if (m_viewer) {
        // Imaging operations must happen on the main thread for history consistency
        QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data(), name]() {
            if (v) v->pushUndo(name);
        }, Qt::BlockingQueuedConnection);
    }
}

void JSView::setPreviewLUT(const QVariantList& luts)
{
    if (!m_viewer) return;

    // Convert JS array-of-arrays to C++ vector-of-vectors
    std::vector<std::vector<float>> cpuLuts;
    cpuLuts.reserve(static_cast<size_t>(luts.size()));

    for (const QVariant& channelVar : luts) {
        const QVariantList channelList = channelVar.toList();
        std::vector<float> channelData;
        channelData.reserve(static_cast<size_t>(channelList.size()));
        for (const QVariant& v : channelList) {
            channelData.push_back(static_cast<float>(v.toDouble()));
        }
        cpuLuts.push_back(std::move(channelData));
    }

    QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data(), cpuLuts = std::move(cpuLuts)]() {
        if (v) {
            v->setPreviewLUT(cpuLuts);
            v->refreshDisplay(false); // Refresh but don't recompute histogram
        }
    }, Qt::QueuedConnection);
}

void JSView::clearPreviewLUT()
{
    if (m_viewer) {
        QMetaObject::invokeMethod(m_viewer.data(), [v = m_viewer.data()]() {
            if (v) {
                v->clearPreviewLUT();
                v->refreshDisplay(false);
            }
        }, Qt::QueuedConnection);
    }
}

} // namespace Scripting