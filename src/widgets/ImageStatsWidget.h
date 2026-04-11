/**
 * @file    ImageStatsWidget.h
 * @brief   Panel widget that computes and displays per-image statistics,
 *          including global pixel statistics and stellar-profile metrics.
 *
 * The widget is designed to be hosted inside a docked panel or an MDI tab.
 * Results are cached per sub-window so that switching between images does not
 * require re-running the (potentially expensive) analysis.
 */

#ifndef IMAGESTATSWIDGET_H
#define IMAGESTATSWIDGET_H

#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QWidget>

class CustomMdiSubWindow;

/**
 * @class ImageStatsWidget
 * @brief Displays global pixel statistics and stellar-profile metrics for the
 *        currently active MDI sub-window.
 *
 * Usage:
 *  1. Call setActiveWindow() whenever the active sub-window changes.
 *  2. Press "Calculate Statistics" to run the analysis on the current image.
 *  3. Results are cached: revisiting an already-analysed image shows cached
 *     values immediately without re-running the algorithm.
 */
class ImageStatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImageStatsWidget(QWidget* parent = nullptr);
    ~ImageStatsWidget() override;

public slots:
    /**
     * @brief Updates the widget to reflect a newly activated sub-window.
     *
     * If statistics for @p subWindow have already been computed they are
     * restored from the internal cache; otherwise all labels are reset and
     * the "Calculate" button is enabled.
     *
     * @param subWindow  The sub-window that just became active, or nullptr.
     */
    void setActiveWindow(CustomMdiSubWindow* subWindow);

private slots:
    // Runs global pixel analysis + star detection on the current image.
    void calculateStatistics();

    // Reserved for future header-label updates (currently a no-op).
    void updateHeader();

private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    // Resets every statistic label to its default "N/A" placeholder.
    void clearStats();

    // ── State ────────────────────────────────────────────────────────────────

    // Weak reference to the currently active sub-window.
    QPointer<CustomMdiSubWindow> m_activeSubWindow;

    // ── UI elements – Status ─────────────────────────────────────────────────

    QLabel* m_statusLabel;           // Shows "Ready / Analysing… / Complete".

    // ── UI elements – Global image statistics ────────────────────────────────

    QLabel* m_globalMeanLabel;       // Arithmetic mean of all pixel values.
    QLabel* m_globalMedianLabel;     // Median pixel value.
    QLabel* m_globalMinLabel;        // Minimum pixel value.
    QLabel* m_globalMaxLabel;        // Maximum pixel value.
    QLabel* m_globalStdDevLabel;     // Standard deviation of pixel values.
    QLabel* m_globalMadLabel;        // Median absolute deviation.
    QLabel* m_globalAvgDevLabel;     // Mean absolute deviation from the median.
    QLabel* m_globalNoiseLabel;      // Estimated per-pixel noise level.

    // ── UI elements – Stellar profile (FWHM) ────────────────────────────────

    QLabel* m_starsCountLabel;       // Total number of detected stars.
    QLabel* m_meanFwhmLabel;         // Mean FWHM across fitted stars (px).
    QLabel* m_medianFwhmLabel;       // Median FWHM across fitted stars (px).
    QLabel* m_minFwhmLabel;          // Minimum FWHM found (px).
    QLabel* m_maxFwhmLabel;          // Maximum FWHM found (px).
    QLabel* m_stdDevFwhmLabel;       // Standard deviation of FWHM values.

    // ── UI elements – Stellar shape (roundness) ──────────────────────────────

    QLabel* m_meanRoundnessLabel;    // Mean roundness (minor/major axis ratio).
    QLabel* m_minRoundnessLabel;     // Minimum roundness value.
    QLabel* m_maxRoundnessLabel;     // Maximum roundness value.
    QLabel* m_stdDevRoundnessLabel;  // Standard deviation of roundness.

    // ── UI elements – Photometry & background ────────────────────────────────

    QLabel* m_meanRmseLabel;         // Mean PSF-fit residual (RMSE).
    QLabel* m_saturatedCountLabel;   // Number of saturated stars.
    QLabel* m_meanBackgroundLabel;   // Mean local background level.
    QLabel* m_meanFluxLabel;         // Mean integrated stellar flux.
    QLabel* m_maxPeakLabel;          // Peak value of the brightest star.

    // ── UI elements – Best-fit star ──────────────────────────────────────────

    QLabel* m_bestPsfProfileLabel;   // PSF model used for the best-fit star.
    QLabel* m_bestFwhmLabel;         // FWHM of the best-fit star (px).
    QLabel* m_bestFluxLabel;         // Flux of the best-fit star.

    // ── UI elements – Actions ────────────────────────────────────────────────

    QPushButton* m_calculateBtn;     // Triggers the statistics calculation.

    // ── Cached statistics ────────────────────────────────────────────────────

    /**
     * @struct ImageStats
     * @brief  Plain-old-data structure that holds all computed statistics for
     *         a single image.  Stored in m_statsCache keyed by sub-window.
     */
    struct ImageStats
    {
        bool calculated = false;    //< True once analysis has been run.

        // Global pixel statistics
        double globalMean    = 0.0; // Arithmetic mean.
        double globalMedian  = 0.0; // Median.
        double globalMin     = 0.0; // Minimum value.
        double globalMax     = 0.0; // Maximum value.
        double globalStdDev  = 0.0; // Standard deviation.
        double globalMad     = 0.0; // Median absolute deviation.
        double globalAvgDev  = 0.0; // Mean absolute deviation.
        double globalNoise   = 0.0; // Estimated noise level.

        // Star detection / FWHM
        int    starsCount    = 0;   // Total detected stars.
        double meanFwhm      = 0.0;
        double medianFwhm    = 0.0;
        double minFwhm       = 0.0;
        double maxFwhm       = 0.0;
        double stdDevFwhm    = 0.0;

        // Roundness
        double meanRoundness    = 0.0;
        double minRoundness     = 0.0;
        double maxRoundness     = 0.0;
        double stdDevRoundness  = 0.0;

        // Photometry & background
        double meanRmse       = 0.0;
        int    saturatedCount = 0;
        double meanBackground = 0.0;
        double meanFlux       = 0.0;
        double maxPeak        = 0.0;

        // Best-fit star
        QString bestProfile;         // "Gaussian" or "Moffat".
        double  bestFwhm     = 0.0;
        double  bestFlux     = 0.0;
    };

    // Cache mapping each sub-window to its previously computed statistics.
    QHash<CustomMdiSubWindow*, ImageStats> m_statsCache;
};

#endif // IMAGESTATSWIDGET_H