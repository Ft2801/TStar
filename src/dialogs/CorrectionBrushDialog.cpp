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

#include "CorrectionBrushDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../ImageViewer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <optional>

#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/photo.hpp>

// ============================================================================
// CorrectionWorker
// ============================================================================

CorrectionWorker::CorrectionWorker(const ImageBuffer&      img,
                                   int                     x,
                                   int                     y,
                                   int                     radius,
                                   float                   feather,
                                   float                   opacity,
                                   const std::vector<int>& channels,
                                   CorrectionMethod        method)
    : m_image(img)
    , m_x(x)
    , m_y(y)
    , m_radius(radius)
    , m_feather(feather)
    , m_opacity(opacity)
    , m_channels(channels)
    , m_method(method)
{
    setAutoDelete(true);
}

void CorrectionWorker::run()
{
    ImageBuffer result = removeBlemish(
        m_image, m_x, m_y, m_radius, m_feather, m_opacity, m_channels, m_method);
    emit finished(result);
}

// ----------------------------------------------------------------------------
// medianCircle
// ----------------------------------------------------------------------------

float CorrectionWorker::medianCircle(const ImageBuffer&      img,
                                     int                     cx,
                                     int                     cy,
                                     int                     radius,
                                     const std::vector<int>& channels)
{
    const int    w    = img.width();
    const int    h    = img.height();
    const int    c    = img.channels();
    const float* data = img.data().data();

    const int x0 = std::max(0, cx - radius);
    const int x1 = std::min(w, cx + radius + 1);
    const int y0 = std::max(0, cy - radius);
    const int y1 = std::min(h, cy + radius + 1);

    if (x0 >= x1 || y0 >= y1) return 0.0f;

    std::vector<float> vals;
    vals.reserve(static_cast<size_t>((x1 - x0) * (y1 - y0) * channels.size()));

    for (int ch : channels)
    {
        if (ch >= c) continue;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius)
                    vals.push_back(data[(y * w + x) * c + ch]);
            }
        }
    }

    if (vals.empty()) return 0.0f;

    std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
    return vals[vals.size() / 2];
}

// ----------------------------------------------------------------------------
// removeBlemish
// ----------------------------------------------------------------------------

ImageBuffer CorrectionWorker::removeBlemish(const ImageBuffer&      img,
                                            int                     x,
                                            int                     y,
                                            int                     radius,
                                            float                   feather,
                                            float                   opacity,
                                            const std::vector<int>& channels,
                                            CorrectionMethod        method)
{
    // -------------------------------------------------------------------------
    // Content-Aware path
    // Performs masked template matching to find the best matching patch within
    // a search radius, then blends it in using OpenCV seamlessClone.
    // Falls back to the Standard path on boundary violations or CV errors.
    // -------------------------------------------------------------------------
    if (method == CorrectionMethod::ContentAware)
    {
        auto caResult = [&]() -> std::optional<ImageBuffer>
        {
            const int w = img.width();
            const int h = img.height();
            const int c = img.channels();

            // Search area extends 4x the blemish radius plus safety padding.
            const int searchRad = radius * 4;
            const int pad       = searchRad + 20;

            const int x0   = std::max(0, x - pad);
            const int y0   = std::max(0, y - pad);
            const int x1   = std::min(w, x + pad + 1);
            const int y1   = std::min(h, y + pad + 1);
            const int roiW = x1 - x0;
            const int roiH = y1 - y0;

            if (roiW <= 0 || roiH <= 0) return std::nullopt;

            // Convert ROI to 8-bit CV_8UC3 (required by seamlessClone).
            cv::Mat roiMat(roiH, roiW, CV_8UC3);
            const float* srcData = img.data().data();

            for (int i = 0; i < roiH; ++i)
            {
                for (int j = 0; j < roiW; ++j)
                {
                    const int sx = x0 + j;
                    const int sy = y0 + i;
                    uchar b, g, r;

                    if (c >= 3)
                    {
                        r = (uchar)std::clamp(srcData[(sy*w+sx)*c+0] * 255.0f, 0.0f, 255.0f);
                        g = (uchar)std::clamp(srcData[(sy*w+sx)*c+1] * 255.0f, 0.0f, 255.0f);
                        b = (uchar)std::clamp(srcData[(sy*w+sx)*c+2] * 255.0f, 0.0f, 255.0f);
                    }
                    else
                    {
                        const uchar v =
                            (uchar)std::clamp(srcData[(sy*w+sx)*c] * 255.0f, 0.0f, 255.0f);
                        r = g = b = v;
                    }
                    roiMat.at<cv::Vec3b>(i, j) = cv::Vec3b(b, g, r);
                }
            }

            const int tRad  = radius + 2;
            const int tSize = 2 * tRad + 1;
            const int cx    = x - x0;
            const int cy    = y - y0;

            // Abort if the template window would extend outside the ROI.
            if (cx - tRad < 0 || cy - tRad < 0 ||
                cx + tRad >= roiW || cy + tRad >= roiH)
                return std::nullopt;

            // Build a mask that excludes the blemish hole from template matching.
            cv::Mat tMask = cv::Mat::ones(tSize, tSize, CV_8UC1);
            cv::circle(tMask, cv::Point(tRad, tRad), radius, cv::Scalar(0), -1);

            cv::Rect tRect(cx - tRad, cy - tRad, tSize, tSize);
            cv::Mat  templ = roiMat(tRect);

            // Template matching using squared-difference; masked version preferred.
            cv::Mat matchResult;
            try   { cv::matchTemplate(roiMat, templ, matchResult, cv::TM_SQDIFF, tMask); }
            catch (...) { cv::matchTemplate(roiMat, templ, matchResult, cv::TM_SQDIFF); }

            // Suppress the self-match location so we never pick the blemish itself.
            const int excludeRad = radius + 5;
            cv::circle(matchResult,
                       cv::Point(cx - tRad, cy - tRad),
                       excludeRad,
                       cv::Scalar(FLT_MAX), -1);

            double    minVal;
            cv::Point minLoc;
            cv::minMaxLoc(matchResult, &minVal, nullptr, &minLoc, nullptr);

            const cv::Point bestTL = minLoc;
            cv::Rect        srcRect(bestTL.x, bestTL.y, tSize, tSize);

            if (srcRect.x < 0 || srcRect.y < 0 ||
                srcRect.x + srcRect.width  > roiW ||
                srcRect.y + srcRect.height > roiH)
                return std::nullopt;

            cv::Mat sourcePatch = roiMat(srcRect);

            // Build the circular clone mask.
            cv::Mat cloneMask = cv::Mat::zeros(tSize, tSize, CV_8UC1);
            cv::circle(cloneMask, cv::Point(tRad, tRad), radius, cv::Scalar(255), -1);

            // Seamless clone places sourcePatch into roiMat at the blemish centre.
            cv::Mat blendedROI;
            try
            {
                cv::seamlessClone(sourcePatch, roiMat, cloneMask,
                                  cv::Point(cx, cy), blendedROI, cv::NORMAL_CLONE);
            }
            catch (const cv::Exception&)
            {
                // Boundary failure fallback: simple masked copy.
                blendedROI = roiMat.clone();
                sourcePatch.copyTo(blendedROI(
                    cv::Rect(cx - tRad, cy - tRad, tSize, tSize)), cloneMask);
            }

            // Write the blended result back into a copy of the original buffer.
            ImageBuffer result   = img;
            float*      dstData  = result.data().data();

            for (int i = 0; i < roiH; ++i)
            {
                for (int j = 0; j < roiW; ++j)
                {
                    const float dist = std::hypot(x0 + j - x, y0 + i - y);
                    if (dist > radius + 2) continue;

                    const float weight = (dist > radius) ? 0.0f : 1.0f;
                    const float wOp    = weight * opacity;
                    if (wOp <= 0.001f) continue;

                    const int sx = x0 + j;
                    const int sy = y0 + i;

                    const cv::Vec3b px = blendedROI.at<cv::Vec3b>(i, j);
                    const float     r  = px[2] / 255.0f;
                    const float     g  = px[1] / 255.0f;
                    const float     b  = px[0] / 255.0f;

                    if (c == 3)
                    {
                        dstData[(sy*w+sx)*c+0] =
                            dstData[(sy*w+sx)*c+0] * (1.0f - wOp) + r * wOp;
                        dstData[(sy*w+sx)*c+1] =
                            dstData[(sy*w+sx)*c+1] * (1.0f - wOp) + g * wOp;
                        dstData[(sy*w+sx)*c+2] =
                            dstData[(sy*w+sx)*c+2] * (1.0f - wOp) + b * wOp;
                    }
                    else
                    {
                        const float gray = (r + g + b) / 3.0f;
                        dstData[(sy*w+sx)*c] =
                            dstData[(sy*w+sx)*c] * (1.0f - wOp) + gray * wOp;
                    }
                }
            }

            return result;
        }();

        if (caResult.has_value()) return caResult.value();
        // If Content-Aware failed (template out of bounds), fall through to Standard.
    }

    // -------------------------------------------------------------------------
    // Standard median path
    // Samples candidate source centres arranged at 60-degree intervals and
    // picks the three whose median is closest to the blemish median.
    // -------------------------------------------------------------------------
    ImageBuffer  result  = img;
    float*       outData = result.data().data();
    const float* inData  = img.data().data();
    const int    w       = img.width();
    const int    h       = img.height();
    const int    c       = img.channels();

    // Generate six candidate centres at 1.5x radius around the blemish.
    std::vector<std::pair<int, int>> centers;
    const int angles[] = { 0, 60, 120, 180, 240, 300 };
    for (int ang : angles)
    {
        const float rrad = ang * 3.14159f / 180.0f;
        centers.push_back({
            x + (int)(std::cos(rrad) * (radius * 1.5f)),
            y + (int)(std::sin(rrad) * (radius * 1.5f))
        });
    }

    const float tgtMedian = medianCircle(img, x, y, radius, channels);

    // Rank candidates by similarity to the blemish median.
    std::vector<std::pair<float, int>> diffs;
    for (size_t i = 0; i < centers.size(); ++i)
    {
        const float m = medianCircle(
            img, centers[i].first, centers[i].second, radius, channels);
        diffs.push_back({ std::abs(m - tgtMedian), (int)i });
    }
    std::sort(diffs.begin(), diffs.end());

    std::vector<std::pair<int, int>> selCenters;
    for (int i = 0; i < (int)std::min((size_t)3, diffs.size()); ++i)
        selCenters.push_back(centers[diffs[i].second]);

    // Apply median replacement per channel within the blemish circle.
    const int x0 = std::max(0, x - radius);
    const int x1 = std::min(w, x + radius + 1);
    const int y0 = std::max(0, y - radius);
    const int y1 = std::min(h, y + radius + 1);

    for (int ch : channels)
    {
        if (ch >= c) continue;

        for (int i = y0; i < y1; ++i)
        {
            for (int j = x0; j < x1; ++j)
            {
                const float dist = std::hypot(j - x, i - y);
                if (dist > radius) continue;

                float weight = 1.0f;
                if (feather > 0.0f)
                    weight = std::clamp((radius - dist) / (radius * feather), 0.0f, 1.0f);

                // Collect source samples from selected neighbouring centres.
                std::vector<float> samples;
                for (const auto& center : selCenters)
                {
                    const int sj = j + (center.first  - x);
                    const int si = i + (center.second - y);
                    if (sj >= 0 && sj < w && si >= 0 && si < h)
                        samples.push_back(inData[(si * w + sj) * c + ch]);
                }

                float replVal = inData[(i * w + j) * c + ch];
                if (!samples.empty())
                {
                    std::nth_element(
                        samples.begin(),
                        samples.begin() + samples.size() / 2,
                        samples.end());
                    replVal = samples[samples.size() / 2];
                }

                const float orig     = inData[(i * w + j) * c + ch];
                outData[(i * w + j) * c + ch] =
                    (1.0f - opacity * weight) * orig + (opacity * weight) * replVal;
            }
        }
    }

    return result;
}

// ============================================================================
// CorrectionBrushDialog - construction
// ============================================================================

CorrectionBrushDialog::CorrectionBrushDialog(QWidget* parent)
    : DialogBase(parent, tr("Correction Brush"), 900, 650)
{
    // Load the active image immediately if one is available.
    if (MainWindowCallbacks* mw = getCallbacks())
    {
        if (mw->getCurrentViewer())
        {
            m_currentImage = mw->getCurrentViewer()->getBuffer();
            m_sourceSet    = true;
        }
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Graphics view ---
    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene);
    m_view->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    m_pixItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixItem);

    // Cursor circle overlay
    m_cursorItem = new QGraphicsEllipseItem(0, 0, 10, 10);
    m_cursorItem->setPen(QPen(Qt::red));
    m_cursorItem->setBrush(Qt::NoBrush);
    m_cursorItem->setVisible(false);
    m_cursorItem->setZValue(100);
    m_scene->addItem(m_cursorItem);

    mainLayout->addWidget(m_view);

    // --- Zoom controls ---
    QHBoxLayout* zoomLayout = new QHBoxLayout();
    zoomLayout->addStretch();

    QPushButton* zOut = new QPushButton(tr("Zoom Out"), this);
    QPushButton* zIn  = new QPushButton(tr("Zoom In"),  this);
    QPushButton* zFit = new QPushButton(tr("Fit"),      this);

    zoomLayout->addWidget(zOut);
    zoomLayout->addWidget(zIn);
    zoomLayout->addWidget(zFit);
    zoomLayout->addStretch();
    mainLayout->addLayout(zoomLayout);

    connect(zOut, &QPushButton::clicked, this, &CorrectionBrushDialog::onZoomOut);
    connect(zIn,  &QPushButton::clicked, this, &CorrectionBrushDialog::onZoomIn);
    connect(zFit, &QPushButton::clicked, this, &CorrectionBrushDialog::onFit);

    // --- Brush parameters ---
    QGroupBox*   ctrlGroup = new QGroupBox(tr("Controls"), this);
    QFormLayout* form      = new QFormLayout(ctrlGroup);

    m_radiusSlider  = new QSlider(Qt::Horizontal, this);
    m_radiusSlider->setRange(1, 200);
    m_radiusSlider->setValue(12);

    m_featherSlider = new QSlider(Qt::Horizontal, this);
    m_featherSlider->setRange(0, 100);
    m_featherSlider->setValue(50);

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);

    form->addRow(tr("Radius:"),  m_radiusSlider);
    form->addRow(tr("Feather:"), m_featherSlider);
    form->addRow(tr("Opacity:"), m_opacitySlider);

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Content-Aware (Slow, Best)"),
                           static_cast<int>(CorrectionMethod::ContentAware));
    m_methodCombo->addItem(tr("Standard (Median)"),
                           static_cast<int>(CorrectionMethod::Standard));
    form->addRow(tr("Method:"), m_methodCombo);

    m_autoStretchCheck = new QCheckBox(tr("Auto-stretch preview"), this);
    m_autoStretchCheck->setChecked(false);

    m_linkedCheck = new QCheckBox(tr("Linked channels"), this);
    m_linkedCheck->setChecked(true);

    m_targetMedianSpin = new QDoubleSpinBox(this);
    m_targetMedianSpin->setRange(0.01, 0.9);
    m_targetMedianSpin->setValue(0.25);

    form->addRow(m_autoStretchCheck);
    form->addRow(tr("Target Median:"), m_targetMedianSpin);
    form->addRow(m_linkedCheck);

    mainLayout->addWidget(ctrlGroup);

    // --- Bottom action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_undoBtn = new QPushButton(tr("Undo"), this);
    m_redoBtn = new QPushButton(tr("Redo"), this);
    QPushButton* applyBtn = new QPushButton(tr("Apply to Document"), this);
    QPushButton* closeBtn = new QPushButton(tr("Close"),             this);

    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);

    btnLayout->addWidget(m_undoBtn);
    btnLayout->addWidget(m_redoBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_undoBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::onRedo);
    connect(applyBtn,  &QPushButton::clicked, this, &CorrectionBrushDialog::onApply);
    connect(closeBtn,  &QPushButton::clicked, this, &CorrectionBrushDialog::reject);

    connect(m_autoStretchCheck, &QCheckBox::toggled,
            this, &CorrectionBrushDialog::updateDisplay);
    connect(m_linkedCheck,      &QCheckBox::toggled,
            this, &CorrectionBrushDialog::updateDisplay);
    connect(m_targetMedianSpin, &QDoubleSpinBox::valueChanged,
            this, &CorrectionBrushDialog::updateDisplay);

    updateDisplay();

    // Defer fit until the dialog geometry is fully resolved.
    QTimer::singleShot(0, this, &CorrectionBrushDialog::onFit);
}

CorrectionBrushDialog::~CorrectionBrushDialog()
{
    // Workers are auto-deleted by the thread pool; nothing to clean up here.
}

// ============================================================================
// Public interface
// ============================================================================

void CorrectionBrushDialog::setSource(const ImageBuffer& img)
{
    // Guard: do not replace an active session that already has undo history,
    // which would silently discard unsaved brush work.
    if (m_sourceSet && !m_undoStack.empty())
        return;

    m_sourceSet    = true;
    m_currentImage = img;

    m_undoStack.clear();
    m_redoStack.clear();
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);

    updateDisplay();
    onFit();
}

// ============================================================================
// Event handling
// ============================================================================

void CorrectionBrushDialog::keyPressEvent(QKeyEvent* e)
{
    if (e->matches(QKeySequence::Undo))
    {
        onUndo();
        e->accept();
        return;
    }

    if (e->matches(QKeySequence::Redo) ||
        (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Y))
    {
        onRedo();
        e->accept();
        return;
    }

    DialogBase::keyPressEvent(e);
}

bool CorrectionBrushDialog::eventFilter(QObject* src, QEvent* ev)
{
    if (src == m_view->viewport())
    {
        if (ev->type() == QEvent::MouseMove)
        {
            auto* me = static_cast<QMouseEvent*>(ev);
            const QPointF p = m_view->mapToScene(me->pos());
            const int     r = m_radiusSlider->value();

            m_cursorItem->setRect(p.x() - r, p.y() - r, 2 * r, 2 * r);
            m_cursorItem->setVisible(true);

            // Right-button drag performs a pan.
            if (me->buttons() & Qt::RightButton)
            {
                const QPointF delta = me->pos() - m_lastPanPos;
                m_view->horizontalScrollBar()->setValue(
                    m_view->horizontalScrollBar()->value() - static_cast<int>(delta.x()));
                m_view->verticalScrollBar()->setValue(
                    m_view->verticalScrollBar()->value() - static_cast<int>(delta.y()));
                m_lastPanPos = me->pos();
                return true;
            }
        }
        else if (ev->type() == QEvent::Wheel)
        {
            auto* we = static_cast<QWheelEvent*>(ev);
            const float factor    = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
            const float newZoom   = std::clamp(m_zoom * factor, 0.05f, 8.0f);
            const float actFactor = newZoom / m_zoom;
            m_zoom = newZoom;
            m_view->scale(actFactor, actFactor);
            return true;
        }
        else if (ev->type() == QEvent::Leave)
        {
            m_cursorItem->setVisible(false);
        }
        else if (ev->type() == QEvent::MouseButtonPress)
        {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton)
            {
                healAt(m_view->mapToScene(me->pos()));
                return true;
            }
            if (me->button() == Qt::RightButton)
            {
                m_lastPanPos = me->pos();
                return true;
            }
        }
    }

    return QDialog::eventFilter(src, ev);
}

// ============================================================================
// Brush application
// ============================================================================

void CorrectionBrushDialog::healAt(QPointF scenePos)
{
    if (m_busy || !m_currentImage.isValid()) return;

    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());

    if (x < 0 || y < 0 ||
        x >= m_currentImage.width() ||
        y >= m_currentImage.height()) return;

    const int              r      = m_radiusSlider->value();
    const float            f      = m_featherSlider->value() / 100.0f;
    const float            op     = m_opacitySlider->value() / 100.0f;
    const CorrectionMethod method =
        static_cast<CorrectionMethod>(m_methodCombo->currentData().toInt());

    const std::vector<int> chans = { 0, 1, 2 };

    m_busy = true;
    m_undoStack.push_back(m_currentImage);
    m_undoBtn->setEnabled(true);
    m_redoStack.clear();
    m_redoBtn->setEnabled(false);

    CorrectionWorker* w = new CorrectionWorker(
        m_currentImage, x, y, r, f, op, chans, method);
    connect(w, &CorrectionWorker::finished,
            this, &CorrectionBrushDialog::onWorkerFinished);
    QThreadPool::globalInstance()->start(w);
}

// ============================================================================
// Slots
// ============================================================================

void CorrectionBrushDialog::onWorkerFinished(ImageBuffer result)
{
    m_currentImage = result;
    updateDisplay();
    m_busy = false;
}

void CorrectionBrushDialog::onUndo()
{
    if (m_undoStack.empty() || m_busy) return;

    m_redoStack.push_back(m_currentImage);
    m_currentImage = m_undoStack.back();
    m_undoStack.pop_back();

    m_undoBtn->setEnabled(!m_undoStack.empty());
    m_redoBtn->setEnabled(true);
    updateDisplay();

    if (auto mw = getCallbacks())
        mw->logMessage(tr("Undo: Correction Brush stroke reverted."), 1);
}

void CorrectionBrushDialog::onRedo()
{
    if (m_redoStack.empty() || m_busy) return;

    m_undoStack.push_back(m_currentImage);
    m_currentImage = m_redoStack.back();
    m_redoStack.pop_back();

    m_undoBtn->setEnabled(true);
    m_redoBtn->setEnabled(!m_redoStack.empty());
    updateDisplay();

    if (auto mw = getCallbacks())
        mw->logMessage(tr("Redo: Correction Brush stroke reapplied."), 1);
}

void CorrectionBrushDialog::updateDisplay()
{
    if (!m_currentImage.isValid()) return;

    const ImageBuffer::DisplayMode mode = m_autoStretchCheck->isChecked()
        ? ImageBuffer::Display_AutoStretch
        : ImageBuffer::Display_Linear;

    const QImage qimg = m_currentImage.getDisplayImage(mode, m_linkedCheck->isChecked());
    m_pixItem->setPixmap(QPixmap::fromImage(qimg));

    // Pin the scene rect to the pixmap bounds so the cursor overlay never
    // expands the scene and causes the view to drift.
    m_scene->setSceneRect(m_pixItem->boundingRect());
}

void CorrectionBrushDialog::onZoomIn()  { setZoom(m_zoom * 1.25f); }
void CorrectionBrushDialog::onZoomOut() { setZoom(m_zoom / 1.25f); }

void CorrectionBrushDialog::setZoom(float z)
{
    m_zoom = std::clamp(z, 0.05f, 4.0f);
    m_view->resetTransform();
    m_view->scale(m_zoom, m_zoom);
}

void CorrectionBrushDialog::onFit()
{
    if (m_pixItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixItem, Qt::KeepAspectRatio);
    m_zoom = static_cast<float>(m_view->transform().m11());
}

void CorrectionBrushDialog::onApply()
{
    if (MainWindowCallbacks* mw = getCallbacks())
    {
        ImageViewer* v = mw->getCurrentViewer();
        if (v)
        {
            // Snapshot document state for undo before overwriting the buffer.
            v->pushUndo(tr("Correction Brush"));
            v->setBuffer(m_currentImage, v->getBuffer().name(), true);
            mw->logMessage(tr("Correction Brush applied."), 1);
            accept();
        }
    }
}