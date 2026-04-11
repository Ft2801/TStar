/**
 * @file    ImageStatsWidget.cpp
 * @brief   Implementation of ImageStatsWidget.
 *
 * The analysis pipeline is:
 *   1. Retrieve the ImageBuffer from the active ImageViewer.
 *   2. Compute global pixel statistics (mean, median, std-dev, MAD, noise…).
 *   3. Run StarDetector to obtain a list of detected / fitted stars.
 *   4. Aggregate per-star metrics (FWHM, roundness, flux, background…).
 *   5. Store everything in the per-window cache and refresh the labels.
 */

#include "ImageStatsWidget.h"
#include "CustomMdiSubWindow.h"
#include "ImageViewer.h"
#include "../photometry/PsfFitter.h"
#include "../photometry/StarDetector.h"
#include "../stacking/Statistics.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPointer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

ImageStatsWidget::ImageStatsWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background-color: transparent;");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Top header bar ────────────────────────────────────────────────────────
    {
        QWidget* headerBar = new QWidget(this);
        headerBar->setFixedHeight(26);
        headerBar->setStyleSheet(
            "background-color: #202020;"
            "border-bottom: 1px solid #1a1a1a;"
            "border-left: none; border-right: none; border-top: none;"
        );

        auto* headerLayout = new QHBoxLayout(headerBar);
        headerLayout->setContentsMargins(8, 0, 8, 0);
        headerLayout->setSpacing(0);

        QLabel* titleLabel = new QLabel(tr("Image Statistics"), headerBar);
        titleLabel->setStyleSheet(
            "color: #ccc; font-weight: bold; background: transparent; font-size: 11px;"
        );
        headerLayout->addWidget(titleLabel);

        mainLayout->addWidget(headerBar);
    }

    // ── Scrollable content area ───────────────────────────────────────────────
    {
        QWidget* contentWidget = new QWidget(this);
        contentWidget->setAttribute(Qt::WA_StyledBackground, true);
        contentWidget->setStyleSheet(
            "background-color: transparent; color: #ccc; font-size: 11px; border: none;"
        );

        QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
        contentLayout->setContentsMargins(8, 8, 8, 8);
        contentLayout->setSpacing(4);

        // Status row
        m_statusLabel = new QLabel(tr("Status: Ready"), this);
        contentLayout->addWidget(m_statusLabel);

        // ── Label factory helpers (capture listLayout by reference) ───────────

        QVBoxLayout* listLayout = new QVBoxLayout();
        listLayout->setSpacing(4);

        // Creates a bold section title label and appends it to listLayout.
        auto createTitle = [this, listLayout](const QString& title) {
            QLabel* label = new QLabel(title, this);
            label->setStyleSheet(
                "font-weight: bold; font-size: 11px; color: #fff; margin-top: 8px;"
            );
            listLayout->addWidget(label);
        };

        // Creates a regular value label, appends it to listLayout, and returns it.
        auto createLabel = [this, listLayout](const QString& text) -> QLabel* {
            QLabel* label = new QLabel(text, this);
            label->setStyleSheet("color: #ccc; margin-left: 6px;");
            listLayout->addWidget(label);
            return label;
        };

        // ── Section: Global Statistics ────────────────────────────────────────
        createTitle(tr("Global Statistics"));
        m_globalMedianLabel = createLabel(tr("Median: N/A"));
        m_globalMeanLabel   = createLabel(tr("Mean: N/A"));
        m_globalMinLabel    = createLabel(tr("Min: N/A"));
        m_globalMaxLabel    = createLabel(tr("Max: N/A"));
        m_globalStdDevLabel = createLabel(tr("StdDev: N/A"));
        m_globalMadLabel    = createLabel(tr("MAD: N/A"));
        m_globalAvgDevLabel = createLabel(tr("AvgDev: N/A"));
        m_globalNoiseLabel  = createLabel(tr("Noise Estimate: N/A"));

        // ── Section: Stellar Profile (FWHM) ──────────────────────────────────
        createTitle(tr("Stellar Profile"));
        m_starsCountLabel     = createLabel(tr("Stars: N/A"));
        m_medianFwhmLabel     = createLabel(tr("Median FWHM: N/A"));
        m_meanFwhmLabel       = createLabel(tr("Mean FWHM: N/A"));
        m_minFwhmLabel        = createLabel(tr("Min FWHM: N/A"));
        m_maxFwhmLabel        = createLabel(tr("Max FWHM: N/A"));
        m_stdDevFwhmLabel     = createLabel(tr("StdDev FWHM: N/A"));
        m_bestPsfProfileLabel = createLabel(tr("Best Fit Profile: N/A"));
        m_bestFwhmLabel       = createLabel(tr("Best FWHM: N/A"));

        // ── Section: Stellar Shape (roundness) ───────────────────────────────
        createTitle(tr("Stellar Shape"));
        m_meanRoundnessLabel   = createLabel(tr("Mean Roundness: N/A"));
        m_minRoundnessLabel    = createLabel(tr("Min Roundness: N/A"));
        m_maxRoundnessLabel    = createLabel(tr("Max Roundness: N/A"));
        m_stdDevRoundnessLabel = createLabel(tr("StdDev Roundness: N/A"));

        // ── Section: Photometry & Background ─────────────────────────────────
        createTitle(tr("Photometry & Background"));
        m_meanBackgroundLabel = createLabel(tr("Mean Background: N/A"));
        m_meanFluxLabel       = createLabel(tr("Mean Flux: N/A"));
        m_maxPeakLabel        = createLabel(tr("Max Peak: N/A"));
        m_saturatedCountLabel = createLabel(tr("Saturated Stars: N/A"));
        m_meanRmseLabel       = createLabel(tr("Mean RMSE: N/A"));
        m_bestFluxLabel       = createLabel(tr("Best Flux: N/A"));

        contentLayout->addLayout(listLayout);

        mainLayout->addWidget(contentWidget);
    }

    mainLayout->addStretch();

    // ── Action button (pinned to bottom) ─────────────────────────────────────
    m_calculateBtn = new QPushButton(tr("Calculate Statistics"), this);
    m_calculateBtn->setCursor(Qt::PointingHandCursor);
    m_calculateBtn->setStyleSheet(
        "QPushButton         { background-color: #333; color: #ccc; border: 1px solid #555;"
        "                      border-radius: 2px; padding: 4px; font-size: 11px;"
        "                      margin-left: 8px; margin-right: 8px; margin-bottom: 8px; }"
        "QPushButton:hover   { background-color: #444; color: #fff; border-color: #777; }"
        "QPushButton:pressed { background-color: #222; border-color: #555; }"
        "QPushButton:disabled{ background-color: #1a1a1a; color: #555; border-color: #333; }"
    );
    connect(m_calculateBtn, &QPushButton::clicked,
            this, &ImageStatsWidget::calculateStatistics);
    mainLayout->addWidget(m_calculateBtn);
}

ImageStatsWidget::~ImageStatsWidget() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Public slots
// ─────────────────────────────────────────────────────────────────────────────

void ImageStatsWidget::setActiveWindow(CustomMdiSubWindow* subWindow)
{
    m_activeSubWindow = subWindow;
    updateHeader();

    // ── Restore from cache if statistics have already been computed ───────────
    if (m_activeSubWindow && m_statsCache.contains(m_activeSubWindow))
    {
        const ImageStats& stats = m_statsCache[m_activeSubWindow];

        if (stats.calculated)
        {
            m_statusLabel->setText(tr("Status: Cached"));

            // Global statistics
            m_globalMedianLabel->setText(tr("Median: %1").arg(stats.globalMedian, 0, 'f', 4));
            m_globalMeanLabel  ->setText(tr("Mean: %1")  .arg(stats.globalMean,   0, 'f', 4));
            m_globalMinLabel   ->setText(tr("Min: %1")   .arg(stats.globalMin,    0, 'f', 4));
            m_globalMaxLabel   ->setText(tr("Max: %1")   .arg(stats.globalMax,    0, 'f', 4));
            m_globalStdDevLabel->setText(tr("StdDev: %1").arg(stats.globalStdDev, 0, 'f', 4));
            m_globalMadLabel   ->setText(tr("MAD: %1")   .arg(stats.globalMad,    0, 'f', 4));
            m_globalAvgDevLabel->setText(tr("AvgDev: %1").arg(stats.globalAvgDev, 0, 'f', 4));
            m_globalNoiseLabel ->setText(tr("Noise Estimate: %1").arg(stats.globalNoise, 0, 'e', 3));

            // Stellar profile – FWHM
            m_starsCountLabel ->setText(tr("Stars: %1")           .arg(stats.starsCount));
            m_meanFwhmLabel   ->setText(tr("Mean FWHM: %1 px")    .arg(stats.meanFwhm,    0, 'f', 2));
            m_medianFwhmLabel ->setText(tr("Median FWHM: %1 px")  .arg(stats.medianFwhm,  0, 'f', 2));
            m_minFwhmLabel    ->setText(tr("Min FWHM: %1 px")     .arg(stats.minFwhm,     0, 'f', 2));
            m_maxFwhmLabel    ->setText(tr("Max FWHM: %1 px")     .arg(stats.maxFwhm,     0, 'f', 2));
            m_stdDevFwhmLabel ->setText(tr("StdDev FWHM: %1 px")  .arg(stats.stdDevFwhm,  0, 'f', 2));

            // Stellar shape – roundness
            m_meanRoundnessLabel  ->setText(tr("Mean Roundness: %1")   .arg(stats.meanRoundness,   0, 'f', 2));
            m_minRoundnessLabel   ->setText(tr("Min Roundness: %1")    .arg(stats.minRoundness,    0, 'f', 2));
            m_maxRoundnessLabel   ->setText(tr("Max Roundness: %1")    .arg(stats.maxRoundness,    0, 'f', 2));
            m_stdDevRoundnessLabel->setText(tr("StdDev Roundness: %1") .arg(stats.stdDevRoundness, 0, 'f', 2));

            // Photometry & background
            m_meanRmseLabel      ->setText(tr("Mean RMSE: %1")       .arg(stats.meanRmse,       0, 'f', 4));
            m_saturatedCountLabel->setText(tr("Saturated Stars: %1") .arg(stats.saturatedCount));
            m_meanBackgroundLabel->setText(tr("Mean Background: %1") .arg(stats.meanBackground, 0, 'f', 4));
            m_meanFluxLabel      ->setText(tr("Mean Flux: %1")       .arg(stats.meanFlux,       0, 'f', 2));
            m_maxPeakLabel       ->setText(tr("Max Peak: %1")        .arg(stats.maxPeak,        0, 'f', 4));

            // Best-fit star
            m_bestPsfProfileLabel->setText(tr("Best Fit Profile: %1").arg(stats.bestProfile));
            m_bestFwhmLabel      ->setText(tr("Best FWHM: %1 px")    .arg(stats.bestFwhm, 0, 'f', 2));
            m_bestFluxLabel      ->setText(tr("Best Flux: %1")       .arg(stats.bestFlux, 0, 'e', 2));

            m_starsCountLabel->setText(tr("Stars: %1").arg(stats.starsCount));

            m_calculateBtn->setEnabled(true);
            return;
        }
    }

    // ── No cached data: reset labels and enable/disable the button ────────────
    clearStats();
    m_calculateBtn->setEnabled(m_activeSubWindow != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private slots
// ─────────────────────────────────────────────────────────────────────────────

void ImageStatsWidget::updateHeader()
{
    // The enclosing tab already provides contextual information, so no
    // additional in-widget header label is needed at this time.
}

void ImageStatsWidget::calculateStatistics()
{
    if (!m_activeSubWindow)
        return;

    ImageViewer* viewer = m_activeSubWindow->viewer();
    if (!viewer)
        return;

    // Signal to the user that processing has started
    m_statusLabel->setText(tr("Status: Analysing…"));
    m_calculateBtn->setEnabled(false);
    QApplication::processEvents();  // Ensure the UI updates before blocking work

    // ── Detect stars ──────────────────────────────────────────────────────────

    const ImageBuffer& img = viewer->getBuffer();

    ImageStats stats;
    stats.calculated = true;

    // ── Compute global pixel statistics ───────────────────────────────────────

    img.lockRead();

    const size_t npix     = img.width() * img.height();
    const int    channels = img.channels();
    const float* rawData  = img.data().data();

    // Build a luminance vector (single channel or average of multiple channels)
    std::vector<float> lum(npix);
    if (channels == 1)
    {
        std::copy(rawData, rawData + npix, lum.begin());
    }
    else
    {
        for (size_t i = 0; i < npix; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c)
                sum += rawData[i * channels + c];
            lum[i] = sum / channels;
        }
    }

    img.unlock();

    if (npix > 0)
    {
        // Min / max
        float minVal = 1.0f, maxVal = 0.0f;
        Stacking::Statistics::minMax(lum.data(), npix, minVal, maxVal);
        stats.globalMin = minVal;
        stats.globalMax = maxVal;

        // Mean & standard deviation
        Stacking::Statistics::meanAndStdDev(
            lum.data(), npix, stats.globalMean, stats.globalStdDev);

        // Median (quickMedian re-orders the vector, so use a copy)
        std::vector<float> lumCopy = lum;
        stats.globalMedian = Stacking::Statistics::quickMedian(lumCopy);

        // Median absolute deviation
        stats.globalMad = Stacking::Statistics::mad(lum, stats.globalMedian);

        // Mean absolute deviation (from median)
        double avgDev = 0.0;
        for (float v : lum)
            avgDev += std::abs(v - stats.globalMedian);
        stats.globalAvgDev = avgDev / npix;

        // Noise estimate
        stats.globalNoise = Stacking::Statistics::computeNoise(
            lum.data(), img.width(), img.height());
    }

    // ── AstroSpike-style detection for COUNT ONLY ────────────────────────────
    int fastCount = 0;
    {
        img.lockRead();
        int w = img.width();
        int h = img.height();
        int ch = img.channels();
        if (w >= 10 && h >= 10) {
            std::vector<float> blurred(w * h);
            StarDetector::gaussianBlur(img.data().data(), blurred.data(), w, h, ch, 0, 1.5f);
            
            const float bgMedian = static_cast<float>(stats.globalMedian);
            const float bgNoise  = static_cast<float>(stats.globalNoise);
            float threshold = bgMedian + 5.0f * bgNoise;
            if (threshold < 1e-4f) threshold = 0.01f;

            const int r = 3;
            for (int y = r; y < h - r; y += 1) {
                const float* row = &blurred[y * w];
                for (int x = r; x < w - r; x += 1) {
                    float val = row[x];
                    if (val <= threshold) continue;

                    // Strict local maximum check (7x7)
                    bool isMax = true;
                    for (int dy = -r; dy <= r && isMax; ++dy) {
                        const float* nrow = &blurred[(y + dy) * w];
                        for (int dx = -r; dx <= r && isMax; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            if (nrow[x + dx] > val) isMax = false;
                        }
                    }
                    if (isMax) {
                        fastCount++;
                        x += r; // Skip ahead
                    }
                }
            }
        }
        img.unlock();
    }

    // ── Professional StarDetector for METRICS ─────────────────────────────────
    
    StarDetector detector;
    detector.setMaxStars(4000); 
    StarFinderParams params;
    params.profile = PsfProfile::Gaussian;
    detector.setParams(params);

    std::vector<DetectedStar> stars = detector.detect(img, 0);

    // ── Aggregate result metrics ──────────────────────────────────────────────

    stats.starsCount = fastCount; // The total count comes from the fast pass

    if (stars.empty())
    {
        if (fastCount == 0)
            m_statusLabel->setText(tr("Status: No stars detected"));
        else
            m_statusLabel->setText(tr("Status: No stars suitable for professional analysis"));
            
        m_statsCache[m_activeSubWindow] = stats;
        setActiveWindow(m_activeSubWindow);   // Refresh labels from the cache
        return;
    }

    // ── Aggregate per-star metrics ────────────────────────────────────────────

    double sumFwhm        = 0.0;
    double sumSqFwhm      = 0.0;
    double sumRoundness   = 0.0;
    double sumSqRoundness = 0.0;
    double sumRmse        = 0.0;
    double sumBackground  = 0.0;
    double sumFlux        = 0.0;

    double bestRmse        =  1e9;
    double minFwhmVal      =  1e9;
    double maxFwhmVal      = -1.0;
    double minRoundnessVal =  1e9;
    double maxRoundnessVal = -1.0;
    double maxPeakVal      = -1.0;

    int saturatedCount = 0;

    std::vector<double>    validFwhms;   // Only stars with a successful PSF fit
    const DetectedStar*    bestStar = nullptr;

    for (const DetectedStar& s : stars)
    {
        // Saturation & peak
        if (s.saturated)
            ++saturatedCount;
        if (s.peak > maxPeakVal)
            maxPeakVal = s.peak;

        // Background & flux (all stars, regardless of PSF fit)
        sumBackground += s.background;
        sumFlux       += s.flux;

        // Metrics that require a successful PSF fit
        if (!s.psf)
            continue;

        // FWHM
        sumFwhm   += s.fwhm;
        sumSqFwhm += s.fwhm * s.fwhm;
        validFwhms.push_back(s.fwhm);

        if (s.fwhm < minFwhmVal) minFwhmVal = s.fwhm;
        if (s.fwhm > maxFwhmVal) maxFwhmVal = s.fwhm;

        // Roundness = min(fwhmx, fwhmy) / max(fwhmx, fwhmy)
        double roundness = (s.fwhmx > 0.0) ? (s.fwhmy / s.fwhmx) : 0.0;
        if (roundness > 1.0)
            roundness = 1.0 / roundness;

        sumRoundness   += roundness;
        sumSqRoundness += roundness * roundness;

        if (roundness < minRoundnessVal) minRoundnessVal = roundness;
        if (roundness > maxRoundnessVal) maxRoundnessVal = roundness;

        // RMSE (PSF residual) – track the star with the lowest residual
        sumRmse += s.rmse;
        if (s.rmse > 0.0 && s.rmse < bestRmse)
        {
            bestRmse = s.rmse;
            bestStar = &s;
        }
    }

    // ── Compute derived statistics for fitted stars ───────────────────────────

    const size_t validCount = validFwhms.size();
    if (validCount > 0)
    {
        // Median FWHM (requires sorted data)
        std::sort(validFwhms.begin(), validFwhms.end());
        stats.medianFwhm = (validCount % 2 == 0)
            ? (validFwhms[validCount / 2 - 1] + validFwhms[validCount / 2]) / 2.0
            :  validFwhms[validCount / 2];

        stats.meanFwhm   = sumFwhm / validCount;
        stats.stdDevFwhm = std::sqrt(
            std::max(0.0, sumSqFwhm / validCount - stats.meanFwhm * stats.meanFwhm));
        stats.minFwhm = minFwhmVal;
        stats.maxFwhm = maxFwhmVal;

        stats.meanRoundness   = sumRoundness / validCount;
        stats.stdDevRoundness = std::sqrt(
            std::max(0.0, sumSqRoundness / validCount - stats.meanRoundness * stats.meanRoundness));
        stats.minRoundness = minRoundnessVal;
        stats.maxRoundness = maxRoundnessVal;

        stats.meanRmse = sumRmse / validCount;
    }

    // ── Compute derived statistics for all detected stars ─────────────────────

    const size_t totalCount = stars.size();
    if (totalCount > 0)
    {
        stats.saturatedCount  = saturatedCount;
        stats.meanBackground  = sumBackground / totalCount;
        stats.meanFlux        = sumFlux       / totalCount;
        stats.maxPeak         = maxPeakVal;
    }

    // ── Best-fit star info ────────────────────────────────────────────────────

    if (bestStar && bestStar->psf)
    {
        stats.bestProfile = (bestStar->psf->profile == PsfProfile::Gaussian)
            ? tr("Gaussian")
            : tr("Moffat");
        stats.bestFwhm = bestStar->fwhm;
        stats.bestFlux = bestStar->flux;
    }
    else
    {
        stats.bestProfile = tr("N/A");
    }

    // ── Store in cache and refresh the UI ────────────────────────────────────

    m_statsCache[m_activeSubWindow] = stats;
    m_statusLabel->setText(tr("Status: Complete"));
    setActiveWindow(m_activeSubWindow);   // Repopulates labels from the cache
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void ImageStatsWidget::clearStats()
{
    m_statusLabel->setText(tr("Status: Ready"));

    // Global statistics
    m_globalMedianLabel->setText(tr("Median: N/A"));
    m_globalMeanLabel  ->setText(tr("Mean: N/A"));
    m_globalMinLabel   ->setText(tr("Min: N/A"));
    m_globalMaxLabel   ->setText(tr("Max: N/A"));
    m_globalStdDevLabel->setText(tr("StdDev: N/A"));
    m_globalMadLabel   ->setText(tr("MAD: N/A"));
    m_globalAvgDevLabel->setText(tr("AvgDev: N/A"));
    m_globalNoiseLabel ->setText(tr("Noise Estimate: N/A"));

    // Stellar profile – FWHM
    m_starsCountLabel ->setText(tr("Stars: N/A"));
    m_meanFwhmLabel   ->setText(tr("Mean FWHM: N/A"));
    m_medianFwhmLabel ->setText(tr("Median FWHM: N/A"));
    m_minFwhmLabel    ->setText(tr("Min FWHM: N/A"));
    m_maxFwhmLabel    ->setText(tr("Max FWHM: N/A"));
    m_stdDevFwhmLabel ->setText(tr("StdDev FWHM: N/A"));

    // Stellar shape – roundness
    m_meanRoundnessLabel  ->setText(tr("Mean Roundness: N/A"));
    m_minRoundnessLabel   ->setText(tr("Min Roundness: N/A"));
    m_maxRoundnessLabel   ->setText(tr("Max Roundness: N/A"));
    m_stdDevRoundnessLabel->setText(tr("StdDev Roundness: N/A"));

    // Photometry & background
    m_meanRmseLabel      ->setText(tr("Mean RMSE: N/A"));
    m_saturatedCountLabel->setText(tr("Saturated Stars: N/A"));
    m_meanBackgroundLabel->setText(tr("Mean Background: N/A"));
    m_meanFluxLabel      ->setText(tr("Mean Flux: N/A"));
    m_maxPeakLabel       ->setText(tr("Max Peak: N/A"));

    // Best-fit star
    m_bestPsfProfileLabel->setText(tr("Best Fit Profile: N/A"));
    m_bestFwhmLabel      ->setText(tr("Best FWHM: N/A"));
    m_bestFluxLabel      ->setText(tr("Best Flux: N/A"));
}