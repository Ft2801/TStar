// =============================================================================
// JSHistogramWidget.cpp
//
// Implementation of JSHistogramWidget — the scriptable proxy for HistogramWidget.
// =============================================================================

#include "JSHistogramWidget.h"
#include "widgets/HistogramWidget.h"   // Adjust path to match your project layout

#include <QCoreApplication>
#include <QThread>

namespace Scripting {

// =============================================================================
// Internal helper (duplicated from JSUI.cpp for self-containment)
// =============================================================================

namespace {

inline bool isGuiThread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread();
}

template <typename Fn>
void runOnGuiThreadSync(Fn&& fn)
{
    if (isGuiThread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            std::forward<Fn>(fn),
            Qt::BlockingQueuedConnection
        );
    }
}

template <typename Fn>
void postToGuiThread(Fn&& fn)
{
    if (isGuiThread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            std::forward<Fn>(fn),
            Qt::QueuedConnection
        );
    }
}

} // anonymous namespace

// =============================================================================
// Construction
// =============================================================================

JSHistogramWidget::JSHistogramWidget(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* hw = new HistogramWidget();
        hw->setMinimumHeight(m_minHeightHint);
        hw->setShowGrid(m_showGrid);
        hw->setShowCurve(m_showCurve);
        hw->setLogScale(m_logScale);
        m_widget = hw;
    });
}

// =============================================================================
// Property setters
// =============================================================================

void JSHistogramWidget::setLogScale(bool enabled)
{
    m_logScale = enabled;
    postToGuiThread([w = m_widget, enabled]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setLogScale(enabled);
    });
}

void JSHistogramWidget::setShowGrid(bool show)
{
    m_showGrid = show;
    postToGuiThread([w = m_widget, show]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setShowGrid(show);
    });
}

void JSHistogramWidget::setShowCurve(bool show)
{
    m_showCurve = show;
    postToGuiThread([w = m_widget, show]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setShowCurve(show);
    });
}

void JSHistogramWidget::setMinHeightHint(int h)
{
    m_minHeightHint = h;
    postToGuiThread([w = m_widget, h]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setMinimumHeight(h);
    });
}

// =============================================================================
// Data feeding
// =============================================================================

void JSHistogramWidget::setData(const QVariantList& data, int channels)
{
    auto bins = variantToHistogram(data, channels);
    postToGuiThread([w = m_widget, bins, channels]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setData(bins, channels);
    });
}

void JSHistogramWidget::setGhostData(const QVariantList& data, int channels)
{
    auto bins = variantToHistogram(data, channels);
    postToGuiThread([w = m_widget, bins, channels]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setGhostData(bins, channels);
    });
}

void JSHistogramWidget::setCurve(const QVariantList& lut)
{
    std::vector<float> lutVec;
    lutVec.reserve(static_cast<size_t>(lut.size()));
    for (const QVariant& v : lut) {
        lutVec.push_back(static_cast<float>(v.toDouble()));
    }

    postToGuiThread([w = m_widget, lutVec = std::move(lutVec)]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->setTransformCurve(lutVec);
    });
}

void JSHistogramWidget::clear()
{
    postToGuiThread([w = m_widget]() {
        if (auto* hw = qobject_cast<HistogramWidget*>(w.data()))
            hw->clear();
    });
}

// =============================================================================
// Static helper
// =============================================================================

std::vector<std::vector<int>> JSHistogramWidget::variantToHistogram(
    const QVariantList& data, int channels)
{
    std::vector<std::vector<int>> result;
    result.reserve(static_cast<size_t>(channels));

    for (int c = 0; c < channels && c < data.size(); ++c) {
        const QVariantList chList = data[c].toList();
        std::vector<int> ch;
        ch.reserve(static_cast<size_t>(chList.size()));
        for (const QVariant& v : chList) {
            ch.push_back(v.toInt());
        }
        result.push_back(std::move(ch));
    }

    return result;
}

} // namespace Scripting