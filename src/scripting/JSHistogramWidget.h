// =============================================================================
// JSHistogramWidget.h
//
// Scriptable proxy for HistogramWidget.
//
// Exposes the HistogramWidget to the TStar JavaScript scripting environment,
// allowing scripts to embed a live histogram display inside a Dialog.
//
// The proxy follows the same Proxy Pattern used by all JSUI controls:
//   - The actual HistogramWidget lives on the main GUI thread.
//   - This proxy object lives on the script worker thread.
//   - All cross-thread calls use QMetaObject::invokeMethod.
//
// Usage from JavaScript:
//   var hist = new ScriptHistogram();
//   hist.logScale = true;
//   hist.showGrid = true;
//   sizer.add(hist);
//
//   // Feed histogram data from the active image
//   var data = view.image.computeHistogram(256);
//   hist.setData(data, view.image.channels);
//
//   // Optionally show a curve overlay
//   hist.setCurve([0.0, 0.05, 0.5, 0.95, 1.0]); // LUT as JS array
// =============================================================================

#ifndef JSHISTOGRAMWIDGET_H
#define JSHISTOGRAMWIDGET_H

#include "JSUI.h"

// Forward declaration — keeps this header from pulling in HistogramWidget.h
// into every scripting translation unit.
class HistogramWidget;

namespace Scripting {

class JSHistogramWidget : public JSUIControl {
    Q_OBJECT

    Q_PROPERTY(bool logScale  READ logScale  WRITE setLogScale )
    Q_PROPERTY(bool showGrid  READ showGrid  WRITE setShowGrid )
    Q_PROPERTY(bool showCurve READ showCurve WRITE setShowCurve)
    Q_PROPERTY(int  minHeight READ minHeightHint WRITE setMinHeightHint)

public:
    explicit JSHistogramWidget(QObject* parent = nullptr);
    ~JSHistogramWidget() override = default;

    // -- Cached property accessors (safe to read from any thread) ------------

    bool logScale()  const { return m_logScale;  }
    bool showGrid()  const { return m_showGrid;  }
    bool showCurve() const { return m_showCurve; }
    int  minHeightHint() const { return m_minHeightHint; }

    // -- Setters (dispatched to GUI thread) ----------------------------------

    Q_INVOKABLE void setLogScale (bool enabled);
    Q_INVOKABLE void setShowGrid (bool show);
    Q_INVOKABLE void setShowCurve(bool show);
    Q_INVOKABLE void setMinHeightHint(int h);

    // -- Data feeding (dispatched to GUI thread) -----------------------------

    /**
     * @brief Feed histogram bin data to the widget.
     *
     * @param data      Per-channel bin arrays as returned by JSImage::computeHistogram().
     *                  In JavaScript: an Array of Arrays (one per channel).
     * @param channels  Number of channels (1 for mono, 3 for RGB).
     */
    Q_INVOKABLE void setData(const QVariantList& data, int channels);

    /**
     * @brief Feed a ghost / reference histogram (shown as a faint overlay).
     *
     * Useful for displaying the pre-stretch histogram alongside the current one.
     *
     * @param data     Same format as setData().
     * @param channels Number of channels.
     */
    Q_INVOKABLE void setGhostData(const QVariantList& data, int channels);

    /**
     * @brief Set the transform curve shown as an overlay line.
     *
     * @param lut  A JS Array of float values in [0..1], uniformly sampled
     *             across the input range.  The array length determines sampling
     *             density (256 is typical for an MTF table, more for GHS).
     */
    Q_INVOKABLE void setCurve(const QVariantList& lut);

    /**
     * @brief Remove all histogram data and the curve overlay.
     */
    Q_INVOKABLE void clear();

private:
    /** @brief Convert a JS QVariantList-of-QVariantList to C++ vector format. */
    static std::vector<std::vector<int>> variantToHistogram(const QVariantList& data,
                                                            int channels);

    // Cached state
    bool m_logScale      = false;
    bool m_showGrid      = true;
    bool m_showCurve     = true;
    int  m_minHeightHint = 120;
};

} // namespace Scripting

#endif // JSHISTOGRAMWIDGET_H