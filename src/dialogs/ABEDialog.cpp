/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

#include "ABEDialog.h"
#include "MainWindowCallbacks.h"
#include "algos/AbeMath.h"
#include "ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QIcon>
#include <QCloseEvent>
#include <QProgressDialog>
#include <QApplication>

#include <cfloat>
#include <cmath>
#include <algorithm>

// =============================================================================
// Constructor / Destructor
// =============================================================================

ABEDialog::ABEDialog(QWidget* parent,
                     ImageViewer* viewer,
                     const ImageBuffer& buffer,
                     [[maybe_unused]] bool initialStretch)
    : DialogBase(parent, tr("Auto Background Extraction"))
    , m_viewer(viewer)
    , m_applied(false)
{
    if (m_viewer)
        m_originalBuffer = buffer;

    setWindowTitle(tr("Auto Background Extraction"));
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowIcon(QIcon(":/images/Logo.png"));

    // Activate ABE interaction mode on the viewer.
    if (m_viewer)
        m_viewer->setAbeMode(true);

    // -------------------------------------------------------------------------
    // Main layout
    // -------------------------------------------------------------------------
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -------------------------------------------------------------------------
    // Parameter form
    // -------------------------------------------------------------------------
    QFormLayout* form = new QFormLayout();

    m_spinDegree = new QSpinBox();
    m_spinDegree->setRange(0, 6);
    m_spinDegree->setValue(2);

    m_spinSamples = new QSpinBox();
    m_spinSamples->setRange(10, 10000);
    m_spinSamples->setValue(100);

    m_spinDown = new QSpinBox();
    m_spinDown->setRange(1, 16);
    m_spinDown->setValue(4);

    m_spinPatch = new QSpinBox();
    m_spinPatch->setRange(5, 151);
    m_spinPatch->setSingleStep(2);
    m_spinPatch->setValue(15);

    m_checkRBF = new QCheckBox(tr("Enable RBF"));
    m_checkRBF->setChecked(true);

    m_spinSmooth = new QDoubleSpinBox();
    m_spinSmooth->setRange(0.01, 10.0);
    m_spinSmooth->setSingleStep(0.1);
    m_spinSmooth->setValue(1.0);

    m_checkShowBG = new QCheckBox(tr("Result = Background Model"));

    m_checkNormalize = new QCheckBox(tr("Normalize Background (Undo Color Calib)"));
    m_checkNormalize->setChecked(true);
    m_checkNormalize->setToolTip(tr(
        "If checked, aligns channel backgrounds to the same level.\n"
        "Uncheck if you have already performed Photometric Color Calibration (PCC)."));

    form->addRow(tr("Degree:"),      m_spinDegree);
    form->addRow(tr("Samples:"),     m_spinSamples);
    form->addRow(tr("Downsample:"),  m_spinDown);
    form->addRow(tr("Patch Size:"),  m_spinPatch);
    form->addRow(m_checkRBF);
    form->addRow(tr("RBF Smooth:"),  m_spinSmooth);
    form->addRow(m_checkNormalize);
    form->addRow(m_checkShowBG);

    mainLayout->addLayout(form);

    // -------------------------------------------------------------------------
    // Bottom button bar
    // -------------------------------------------------------------------------
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    QPushButton* btnClear = new QPushButton(tr("Clear Selections"));
    connect(btnClear, &QPushButton::clicked, this, &ABEDialog::clearPolys);
    bottomLayout->addWidget(btnClear);

    QLabel* copyLabel = new QLabel(tr("(C) 2026 SetiAstro"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 10px; margin-right: 10px;");
    bottomLayout->addWidget(copyLabel);

    bottomLayout->addStretch();

    QPushButton* btnClose = new QPushButton(tr("Close"));
    connect(btnClose, &QPushButton::clicked, this, &ABEDialog::close);

    QPushButton* btnApply = new QPushButton(tr("Apply"));
    connect(btnApply, &QPushButton::clicked, this, &ABEDialog::onApply);

    bottomLayout->addWidget(btnClose);
    bottomLayout->addWidget(btnApply);

    mainLayout->addLayout(bottomLayout);

    // Prevent the dialog from being resized larger than its content requires.
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
}

ABEDialog::~ABEDialog()
{
    setAbeMode(false);
}

// =============================================================================
// Public interface
// =============================================================================

void ABEDialog::setAbeMode(bool enabled)
{
    if (m_viewer)
        m_viewer->setAbeMode(enabled);
}

void ABEDialog::setViewer(ImageViewer* viewer)
{
    if (m_viewer == viewer)
        return;

    // Disable ABE mode on the outgoing viewer before switching.
    if (m_viewer)
        m_viewer->setAbeMode(false);

    m_viewer         = viewer;
    m_applied        = false;
    m_originalBuffer = ImageBuffer();

    if (m_viewer && m_viewer->getBuffer().isValid())
    {
        m_originalBuffer = m_viewer->getBuffer();
        m_viewer->setAbeMode(true);
    }
}

// =============================================================================
// Protected overrides
// =============================================================================

void ABEDialog::closeEvent(QCloseEvent* event)
{
    if (m_viewer)
        m_viewer->setAbeMode(false);

    QDialog::closeEvent(event);
}

// =============================================================================
// Private slots
// =============================================================================

void ABEDialog::clearPolys()
{
    if (m_viewer)
        m_viewer->clearAbePolygons();
}

void ABEDialog::onApply()
{
    if (!m_originalBuffer.isValid())
        return;

    // Validate that the buffer contains meaningful (non-zero, finite) data
    // by sampling the first 1000 elements.
    const auto& data     = m_originalBuffer.data();
    bool        hasValidData = false;

    for (size_t i = 0; i < std::min(data.size(), static_cast<size_t>(1000)); ++i)
    {
        float v = data[i];
        if (std::isfinite(v) && v != 0.0f)
        {
            hasValidData = true;
            break;
        }
    }

    if (!hasValidData)
    {
        emit progressMsg(tr("Error: Image data appears to be empty or invalid."));
        return;
    }

    // Show a modal progress dialog for the duration of the operation.
    QProgressDialog progressDlg(tr("Running ABE..."), QString(), 0, 0, this);
    progressDlg.setWindowTitle(tr("Auto Background Extraction"));
    progressDlg.setWindowModality(Qt::WindowModal);
    progressDlg.setCancelButton(nullptr);
    progressDlg.setMinimumDuration(0);
    progressDlg.show();
    QApplication::processEvents();

    // Route progress messages from generateModel into the dialog label.
    QMetaObject::Connection conn = connect(
        this, &ABEDialog::progressMsg,
        [&progressDlg](const QString& msg)
        {
            progressDlg.setLabelText(msg);
            QApplication::processEvents();
        });

    QApplication::setOverrideCursor(Qt::WaitCursor);

    try
    {
        ImageBuffer result = m_originalBuffer;
        generateModel(result);
        clearPolys();
        emit applyResult(result);
    }
    catch (const std::exception& e)
    {
        emit progressMsg(tr("ABE Error: %1").arg(QString::fromStdString(e.what())));
    }
    catch (...)
    {
        emit progressMsg(tr("ABE Error: Unknown exception occurred."));
    }

    disconnect(conn);
    QApplication::restoreOverrideCursor();
}

// =============================================================================
// Private helpers
// =============================================================================

void ABEDialog::generateModel(ImageBuffer& output)
{
    emit progressMsg(tr("Starting ABE model generation..."));

    // -------------------------------------------------------------------------
    // Retrieve processing parameters from the UI controls.
    // -------------------------------------------------------------------------
    const int ds       = m_spinDown->value();
    const int w        = output.width();
    const int h        = output.height();
    const int channels = output.channels();

    // -------------------------------------------------------------------------
    // Collect user-drawn exclusion polygons from the viewer.
    // -------------------------------------------------------------------------
    std::vector<QPolygonF> polygons;
    if (m_viewer)
        polygons = m_viewer->getAbePolygons();

    // -------------------------------------------------------------------------
    // Build an exclusion mask at downsampled resolution.
    // Each element is true when the pixel is allowed as a background sample.
    // -------------------------------------------------------------------------
    const int dw = std::max(1, w / ds);
    const int dh = std::max(1, h / ds);

    std::vector<bool> exMask(dw * dh, true);

    if (!polygons.empty())
    {
        for (int y = 0; y < dh; ++y)
        {
            for (int x = 0; x < dw; ++x)
            {
                QPointF p(static_cast<float>(x * ds), static_cast<float>(y * ds));

                for (const auto& poly : polygons)
                {
                    if (poly.containsPoint(p, Qt::OddEvenFill))
                    {
                        exMask[y * dw + x] = false;
                        break;
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Downsample the full-resolution image using box-average interpolation.
    // -------------------------------------------------------------------------
    emit progressMsg(tr("Downsampling image (factor: %1)...").arg(ds));

    std::vector<float>  smallData(dw * dh * channels);
    const auto&         fullData = output.data();

    for (int y = 0; y < dh; ++y)
    {
        for (int x = 0; x < dw; ++x)
        {
            const int sy_start = y * ds;
            const int sy_end   = std::min(h, (y + 1) * ds);
            const int sx_start = x * ds;
            const int sx_end   = std::min(w, (x + 1) * ds);

            // Stack-allocated accumulators avoid heap allocation per pixel.
            float sum[3]         = { 0.0f, 0.0f, 0.0f };
            int   validCounts[3] = { 0, 0, 0 };

            for (int sy = sy_start; sy < sy_end; ++sy)
            {
                for (int sx = sx_start; sx < sx_end; ++sx)
                {
                    const size_t src_idx = (static_cast<size_t>(sy) * w + sx) * channels;

                    if (src_idx + channels > fullData.size())
                        continue;

                    for (int c = 0; c < channels; ++c)
                    {
                        float val = fullData[src_idx + c];
                        if (std::isfinite(val))
                        {
                            sum[c] += val;
                            validCounts[c]++;
                        }
                    }
                }
            }

            const size_t dst_idx = (static_cast<size_t>(y) * dw + x) * channels;

            for (int c = 0; c < channels; ++c)
            {
                smallData[dst_idx + c] = (validCounts[c] > 0)
                    ? sum[c] / static_cast<float>(validCounts[c])
                    : 0.0f;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Retrieve model-fitting parameters.
    // -------------------------------------------------------------------------
    const int   degree     = m_spinDegree->value();
    const bool  useRbf     = m_checkRBF->isChecked();
    const float smooth     = static_cast<float>(m_spinSmooth->value());
    const int   numSamples = m_spinSamples->value();
    const int   patchSize  = m_spinPatch->value();

    // -------------------------------------------------------------------------
    // Compute per-channel medians of the original buffer (used for normalisation).
    // -------------------------------------------------------------------------
    qDebug() << "[ABE] Computing per-channel medians...";
    std::vector<float> origMedians(channels, 0.0f);
    for (int c = 0; c < channels; ++c)
        origMedians[c] = m_originalBuffer.getChannelMedian(c);

    // Allocate the full-resolution background model buffer.
    qDebug() << "[ABE] Allocating background model buffer...";
    std::vector<float> totalBg(static_cast<size_t>(w) * h * channels, 0.0f);
    qDebug() << "[ABE] Background buffer allocated, size =" << totalBg.size();

    // Track the minimum background value per channel for normalisation.
    std::vector<float> bgMins(channels, FLT_MAX);

    // -------------------------------------------------------------------------
    // Generate a common set of sample points from the luminance image.
    // Using luminance ensures that sample locations are consistent across
    // all colour channels.
    // -------------------------------------------------------------------------
    std::vector<float> grayData(dw * dh);
    for (int i = 0; i < dw * dh; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += smallData[i * channels + c];
        grayData[i] = sum / static_cast<float>(channels);
    }

    emit progressMsg(tr("Generating sample points..."));
    auto commonPoints = AbeMath::generateSamples(grayData, dw, dh, numSamples, patchSize, exMask);

    // If no valid samples were found, fall back to a uniform grid.
    if (commonPoints.empty())
    {
        emit progressMsg(tr("No valid sample points found. Falling back to uniform grid..."));

        const int   gridN = std::max(3, static_cast<int>(std::sqrt(numSamples)));
        const float stepX = static_cast<float>(dw) / gridN;
        const float stepY = static_cast<float>(dh) / gridN;

        for (int gy = 0; gy < gridN; ++gy)
        {
            for (int gx = 0; gx < gridN; ++gx)
            {
                float x = stepX * (gx + 0.5f);
                float y = stepY * (gy + 0.5f);

                if (x >= 0 && x < dw && y >= 0 && y < dh)
                    commonPoints.push_back({ x, y });
            }
        }
    }

    emit progressMsg(tr("Found %1 valid sample points.").arg(commonPoints.size()));

    // -------------------------------------------------------------------------
    // Fit and evaluate the background model for each channel independently,
    // using the same spatial sample positions determined from luminance above.
    // -------------------------------------------------------------------------
    for (int c = 0; c < channels; ++c)
    {
        // Extract the channel plane from the interleaved downsampled data.
        std::vector<float> chData(dw * dh);
        for (int i = 0; i < dw * dh; ++i)
            chData[i] = smallData[i * channels + c];

        // Build the sample list by taking the median of a patch around each
        // sample location for robustness against outliers.
        std::vector<AbeMath::Sample> samples;
        samples.reserve(commonPoints.size());

        for (const auto& p : commonPoints)
        {
            float zVal = AbeMath::getMedianBox(chData, dw, dh,
                                               static_cast<int>(p.x),
                                               static_cast<int>(p.y),
                                               patchSize);
            samples.push_back({ p.x, p.y, zVal });
        }

        // --- Polynomial fit ----------------------------------------------
        std::vector<float>   polyCoeffs;
        AbeMath::RbfModel    rbfModel;

        if (degree > 0 && !samples.empty())
        {
            emit progressMsg(tr("Fitting polynomial (degree %1, channel %2)...").arg(degree).arg(c));
            polyCoeffs = AbeMath::fitPolynomial(samples, degree);
        }

        // --- Subtract polynomial residual before RBF fitting ------------
        // The RBF then only models the low-frequency residual not captured
        // by the polynomial, avoiding double-fitting of large-scale gradients.
        if (useRbf && degree > 0)
        {
            for (auto& s : samples)
            {
                float nx = s.x / static_cast<float>(dw - 1);
                float ny = s.y / static_cast<float>(dh - 1);
                float pv = AbeMath::evalPolynomial(nx, ny, polyCoeffs, degree);
                s.z -= pv;
            }
        }

        // --- RBF fit -----------------------------------------------------
        if (useRbf && !samples.empty())
        {
            emit progressMsg(tr("Fitting RBF (smooth: %1, channel %2)...").arg(smooth).arg(c));
            rbfModel = AbeMath::fitRbf(samples, smooth);
        }

        // --- Evaluate model on the downsampled grid ---------------------
        emit progressMsg(tr("Evaluating model (channel %1)...").arg(c));

        std::vector<float> smallBg(dw * dh);

        for (int i = 0; i < dw * dh; ++i)
        {
            const int   iy  = i / dw;
            const int   ix  = i % dw;
            float       v   = 0.0f;

            // Coordinates normalised to [0, 1] for polynomial evaluation.
            const float nx = static_cast<float>(ix) / static_cast<float>(dw - 1);
            const float ny = static_cast<float>(iy) / static_cast<float>(dh - 1);

            if (degree > 0)
                v += AbeMath::evalPolynomial(nx, ny, polyCoeffs, degree);

            // RBF is evaluated using raw (pixel) coordinates to match fitting.
            if (useRbf)
                v += AbeMath::evalRbf(static_cast<float>(ix), static_cast<float>(iy), rbfModel);

            smallBg[i] = v;
        }

        // --- Bilinear upscale to full resolution ------------------------
        float chMin = FLT_MAX;

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                // Map full-resolution coordinate back into downsampled space.
                const float sx = static_cast<float>(x) / ds;
                const float sy = static_cast<float>(y) / ds;

                int x0 = static_cast<int>(sx);
                int y0 = static_cast<int>(sy);
                int x1 = std::min(x0 + 1, dw - 1);
                int y1 = std::min(y0 + 1, dh - 1);

                x0 = std::max(0, std::min(x0, dw - 1));
                x1 = std::max(0, std::min(x1, dw - 1));
                y0 = std::max(0, std::min(y0, dh - 1));
                y1 = std::max(0, std::min(y1, dh - 1));

                const float fx = sx - x0;
                const float fy = sy - y0;

                const size_t idx00 = static_cast<size_t>(y0) * dw + x0;
                const size_t idx01 = static_cast<size_t>(y0) * dw + x1;
                const size_t idx10 = static_cast<size_t>(y1) * dw + x0;
                const size_t idx11 = static_cast<size_t>(y1) * dw + x1;

                if (idx00 >= smallBg.size() || idx01 >= smallBg.size() ||
                    idx10 >= smallBg.size() || idx11 >= smallBg.size())
                    continue;

                const float top = smallBg[idx00] * (1.0f - fx) + smallBg[idx01] * fx;
                const float bot = smallBg[idx10] * (1.0f - fx) + smallBg[idx11] * fx;
                const float val = top * (1.0f - fy) + bot * fy;

                const size_t dstIdx = (static_cast<size_t>(y) * w + x) * channels + c;

                if (dstIdx >= totalBg.size())
                    continue;

                totalBg[dstIdx] = val;

                if (val < chMin)
                    chMin = val;
            }
        }

        bgMins[c] = chMin;

        // Allow the event loop to process paint events during long operations.
        QApplication::processEvents();
    }

    // -------------------------------------------------------------------------
    // Apply the background correction to the output buffer.
    // -------------------------------------------------------------------------
    emit progressMsg(tr("Applying background correction..."));

    const bool showBg    = m_checkShowBG->isChecked();
    const bool normalize = m_checkNormalize->isChecked();

    // Determine the global minimum background level across all channels.
    // When normalisation is enabled this value is added back after subtraction
    // so that all channels share the same absolute floor.
    float targetFloor = FLT_MAX;
    for (float m : bgMins)
    {
        if (m < targetFloor)
            targetFloor = m;
    }

    std::vector<float>& outData = output.data();

    for (size_t i = 0; i < outData.size(); ++i)
    {
        const int c = static_cast<int>(i) % channels;

        if (showBg)
        {
            // Replace the image with the raw background model for inspection.
            outData[i] = totalBg[i];
        }
        else
        {
            // Normalise mode:  src - BG + targetFloor  (equalises channel floors)
            // Preserve mode:   src - BG + channelMin   (keeps PCC colour ratios)
            const float shift = normalize ? targetFloor : bgMins[c];
            outData[i] = outData[i] - totalBg[i] + shift;

            // Clamp result to the valid normalised range.
            outData[i] = std::max(0.0f, std::min(1.0f, outData[i]));
        }
    }
}