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

#include "NBtoRGBStarsDialog.h"
#include "../ImageViewer.h"
#include "../MainWindowCallbacks.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../io/FitsLoader.h"
#include "../io/XISFReader.h"
#include "../io/SimpleTiffReader.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <cmath>

// =============================================================================
// Anonymous namespace: file loading utility (shared pattern with NBN dialog)
// =============================================================================
namespace {

bool loadImageFromPath(const QString& path, ImageBuffer& buffer, QString& error)
{
    const QString ext = QFileInfo(path).suffix().toLower();

    if (ext == "fits" || ext == "fit")
        return FitsLoader::load(path, buffer, &error);

    if (ext == "xisf")
        return XISFReader::read(path, buffer, &error);

    if (ext == "tif" || ext == "tiff") {
        int w, h, c;
        std::vector<float> data;
        if (!SimpleTiffReader::readFloat32(path, w, h, c, data, &error))
            return false;
        buffer.setData(w, h, c, data);
        return true;
    }

    QImage img(path);
    if (img.isNull()) { error = "Failed to load image."; return false; }

    img = img.convertToFormat(QImage::Format_RGB888);
    const int w = img.width();
    const int h = img.height();
    std::vector<float> data(w * h * 3);

    for (int y = 0; y < h; ++y) {
        const uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            data[(y * w + x) * 3 + 0] = line[x * 3 + 0] / 255.0f;
            data[(y * w + x) * 3 + 1] = line[x * 3 + 1] / 255.0f;
            data[(y * w + x) * 3 + 2] = line[x * 3 + 2] / 255.0f;
        }
    }

    buffer.setData(w, h, 3, data);
    return true;
}

} // anonymous namespace

// =============================================================================
// Constructor / Destructor
// =============================================================================

NBtoRGBStarsDialog::NBtoRGBStarsDialog(QWidget* parent)
    : DialogBase(parent, tr("NB \u2192 RGB Stars"))
{
    m_mainWindow = dynamic_cast<MainWindowCallbacks*>(parent);
    buildUI();
    resize(900, 520);
}

NBtoRGBStarsDialog::~NBtoRGBStarsDialog() = default;

// =============================================================================
// Public API
// =============================================================================

void NBtoRGBStarsDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
}

// =============================================================================
// UI Construction
// =============================================================================

void NBtoRGBStarsDialog::buildUI()
{
    auto* root = new QHBoxLayout(this);

    // -------------------------------------------------------------------------
    // Left panel (fixed width): channel loading, parameters, and actions
    // -------------------------------------------------------------------------
    auto* leftLayout = new QVBoxLayout();
    auto* leftHost   = new QWidget(this);
    leftHost->setLayout(leftLayout);
    leftHost->setFixedWidth(320);

    leftLayout->addWidget(new QLabel(
        "<b>" + tr("NB -> RGB Stars") + "</b><br>" +
        tr("Load Ha / OIII / (optional SII) and/or OSC stars.") + "<br>" +
        tr("Tune ratio and preview; push to a new view.")
    ));

    // Channel load buttons and status labels
    m_btnHa   = new QPushButton(tr("Load Ha..."));
    m_btnOIII = new QPushButton(tr("Load OIII..."));
    m_btnSII  = new QPushButton(tr("Load SII (optional)..."));
    m_btnOSC  = new QPushButton(tr("Load OSC stars (optional)..."));

    m_lblHa   = new QLabel(tr("No Ha loaded."));
    m_lblOIII = new QLabel(tr("No OIII loaded."));
    m_lblSII  = new QLabel(tr("No SII loaded."));
    m_lblOSC  = new QLabel(tr("No OSC stars loaded."));

    for (auto* lab : { m_lblHa, m_lblOIII, m_lblSII, m_lblOSC }) {
        lab->setWordWrap(true);
        lab->setStyleSheet("color:#888; margin-left:8px;");
    }

    for (auto pair : std::initializer_list<std::pair<QPushButton*, QLabel*>>{
            { m_btnHa,   m_lblHa   },
            { m_btnOIII, m_lblOIII },
            { m_btnSII,  m_lblSII  },
            { m_btnOSC,  m_lblOSC  } })
    {
        leftLayout->addWidget(pair.first);
        leftLayout->addWidget(pair.second);
    }

    connect(m_btnHa,   &QPushButton::clicked, this, [this]() { onLoadChannel("Ha");   });
    connect(m_btnOIII, &QPushButton::clicked, this, [this]() { onLoadChannel("OIII"); });
    connect(m_btnSII,  &QPushButton::clicked, this, [this]() { onLoadChannel("SII");  });
    connect(m_btnOSC,  &QPushButton::clicked, this, [this]() { onLoadChannel("OSC");  });

    // Ha:OIII blend ratio
    m_lblRatio = new QLabel(tr("Ha:OIII ratio = 0.30"));
    m_sldRatio = new QSlider(Qt::Horizontal);
    m_sldRatio->setRange(0, 100);
    m_sldRatio->setValue(30);
    connect(m_sldRatio, &QSlider::valueChanged, this, &NBtoRGBStarsDialog::onRatioChanged);
    leftLayout->addWidget(m_lblRatio);
    leftLayout->addWidget(m_sldRatio);

    // Star stretch
    m_chkStarStretch = new QCheckBox(tr("Enable star stretch"));
    m_chkStarStretch->setChecked(true);
    leftLayout->addWidget(m_chkStarStretch);

    m_lblStretch = new QLabel(tr("Stretch factor = 5.00"));
    m_sldStretch = new QSlider(Qt::Horizontal);
    m_sldStretch->setRange(0, 800);
    m_sldStretch->setValue(500);
    connect(m_sldStretch, &QSlider::valueChanged, this, &NBtoRGBStarsDialog::onStretchChanged);
    leftLayout->addWidget(m_lblStretch);
    leftLayout->addWidget(m_sldStretch);

    // Saturation
    m_lblSat = new QLabel(tr("Saturation = 1.00x"));
    m_sldSat = new QSlider(Qt::Horizontal);
    m_sldSat->setRange(0, 300);
    m_sldSat->setValue(100);
    connect(m_sldSat, &QSlider::valueChanged, this, &NBtoRGBStarsDialog::onSatChanged);
    leftLayout->addWidget(m_lblSat);
    leftLayout->addWidget(m_sldSat);

    // Action buttons
    auto* actRow  = new QHBoxLayout();
    m_btnPreview  = new QPushButton(tr("Preview Combine"));
    m_btnPush     = new QPushButton(tr("Push Final to New View"));
    connect(m_btnPreview, &QPushButton::clicked, this, &NBtoRGBStarsDialog::onPreviewCombine);
    connect(m_btnPush,    &QPushButton::clicked, this, &NBtoRGBStarsDialog::onPushFinal);
    actRow->addWidget(m_btnPreview);
    actRow->addWidget(m_btnPush);
    leftLayout->addLayout(actRow);

    m_btnClear = new QPushButton(tr("Clear Inputs"));
    connect(m_btnClear, &QPushButton::clicked, this, &NBtoRGBStarsDialog::onClear);
    leftLayout->addWidget(m_btnClear);
    leftLayout->addStretch(1);

    root->addWidget(leftHost, 0);

    // -------------------------------------------------------------------------
    // Right panel: preview with zoom toolbar and status label
    // -------------------------------------------------------------------------
    auto* rightLayout = new QVBoxLayout();

    auto* tools      = new QHBoxLayout();
    auto* btnZoomIn  = new QPushButton("+"); btnZoomIn->setFixedWidth(30);
    auto* btnZoomOut = new QPushButton("-"); btnZoomOut->setFixedWidth(30);
    auto* btnFit     = new QPushButton(tr("Fit")); btnFit->setFixedWidth(40);

    connect(btnZoomIn,  &QPushButton::clicked, this, [this]() { m_view->scale(1.25, 1.25); });
    connect(btnZoomOut, &QPushButton::clicked, this, [this]() { m_view->scale(0.8,  0.8);  });
    connect(btnFit,     &QPushButton::clicked, this, [this]() {
        if (m_pixBase && !m_pixBase->pixmap().isNull()) {
            m_view->resetTransform();
            m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
        }
    });

    tools->addWidget(btnZoomIn);
    tools->addWidget(btnZoomOut);
    tools->addWidget(btnFit);
    rightLayout->addLayout(tools);

    m_scene   = new QGraphicsScene(this);
    m_view    = new QGraphicsView(m_scene);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setAlignment(Qt::AlignCenter);

    m_pixBase = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixBase);
    rightLayout->addWidget(m_view, 1);

    m_status = new QLabel("");
    rightLayout->addWidget(m_status, 0);

    auto* rightHost = new QWidget(this);
    rightHost->setLayout(rightLayout);
    root->addWidget(rightHost, 1);
}

// =============================================================================
// Slider Label Updates
// =============================================================================

void NBtoRGBStarsDialog::onRatioChanged(int v)
{
    m_lblRatio->setText(tr("Ha:OIII ratio = %1").arg(v / 100.0, 0, 'f', 2));
}

void NBtoRGBStarsDialog::onStretchChanged(int v)
{
    m_lblStretch->setText(tr("Stretch factor = %1").arg(v / 100.0, 0, 'f', 2));
}

void NBtoRGBStarsDialog::onSatChanged(int v)
{
    m_lblSat->setText(tr("Saturation = %1x").arg(v / 100.0, 0, 'f', 2));
}

// =============================================================================
// Channel Loading
// =============================================================================

void NBtoRGBStarsDialog::onLoadChannel(const QString& which)
{
    bool ok;
    const QStringList options = { tr("From View"), tr("From File") };
    const QString src = QInputDialog::getItem(this, tr("Load %1").arg(which),
                                              tr("Source:"), options, 0, false, &ok);
    if (!ok) return;

    if (src == tr("From View")) loadFromViewer(which);
    else                        loadFromFile(which);
}

void NBtoRGBStarsDialog::loadFromViewer(const QString& which)
{
    if (!m_mainWindow) return;

    ImageViewer* vCur = m_mainWindow->getCurrentViewer();
    if (!vCur) {
        QMessageBox::warning(this, tr("No Image"), tr("No active image view found."));
        return;
    }

    QStringList         names;
    QList<ImageViewer*> viewers;
    for (CustomMdiSubWindow* csw : vCur->window()->findChildren<CustomMdiSubWindow*>()) {
        ImageViewer* v = csw->viewer();
        if (!v || !v->getBuffer().isValid() || csw->isToolWindow()) continue;
        names   << csw->windowTitle();
        viewers << v;
    }

    if (names.isEmpty()) {
        QMessageBox::warning(this, tr("No Image"), tr("No active image view found."));
        return;
    }

    bool    ok = true;
    QString choice;
    if (names.size() == 1) {
        choice = names[0];
    } else {
        choice = QInputDialog::getItem(this, tr("Select View - %1").arg(which),
                                       tr("Choose:"), names, 0, false, &ok);
        if (!ok) return;
    }

    const int idx = names.indexOf(choice);
    if (idx < 0) return;

    const ImageBuffer& buf  = viewers[idx]->getBuffer();
    ImageBuffer::ReadLock lock(&buf);
    const int    w    = buf.width();
    const int    h    = buf.height();
    const int    ch   = buf.channels();
    const float* data = buf.data().data();
    const size_t n    = static_cast<size_t>(w) * h;

    if (which == "Ha" || which == "OIII" || which == "SII") {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; ++i)
            mono[i] = std::clamp(data[i * ch], 0.0f, 1.0f);

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                tr("Channel dimensions don't match."));
            return;
        }

        m_chW = w; m_chH = h;
        if      (which == "Ha")   m_ha   = std::move(mono);
        else if (which == "OIII") m_oiii = std::move(mono);
        else                      m_sii  = std::move(mono);

        // Preserve metadata from the first loaded source for result inheritance
        if (!m_hasSrcMeta) { m_srcMeta = buf.metadata(); m_hasSrcMeta = true; }
        setStatusLabel(which, tr("From View (%1x%2)").arg(w).arg(h));

    } else if (which == "OSC") {
        if (ch >= 3) {
            m_osc.resize(n * 3);
            for (size_t i = 0; i < n; ++i) {
                m_osc[i * 3 + 0] = std::clamp(data[i * ch + 0], 0.0f, 1.0f);
                m_osc[i * 3 + 1] = std::clamp(data[i * ch + 1], 0.0f, 1.0f);
                m_osc[i * 3 + 2] = std::clamp(data[i * ch + 2], 0.0f, 1.0f);
            }
            m_oscChannels = 3;
        } else {
            m_osc.resize(n);
            for (size_t i = 0; i < n; ++i)
                m_osc[i] = std::clamp(data[i * ch], 0.0f, 1.0f);
            m_oscChannels = 1;
        }

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                tr("Channel dimensions don't match."));
            m_osc.clear(); m_oscChannels = 0;
            return;
        }

        m_chW = w; m_chH = h;
        if (!m_hasSrcMeta) { m_srcMeta = buf.metadata(); m_hasSrcMeta = true; }
        setStatusLabel(which, tr("From View (%1x%2)").arg(w).arg(h));
    }

    m_status->setText(tr("%1 loaded.").arg(which));
}

void NBtoRGBStarsDialog::loadFromFile(const QString& which)
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select %1 File").arg(which), "",
        tr("Images (*.png *.tif *.tiff *.fits *.fit *.xisf)"));
    if (path.isEmpty()) return;

    ImageBuffer buf;
    QString     err;
    if (!loadImageFromPath(path, buf, err)) {
        QMessageBox::critical(this, tr("Load Error"),
            tr("Could not load %1: %2").arg(QFileInfo(path).fileName(), err));
        return;
    }

    const int    w    = buf.width();
    const int    h    = buf.height();
    const int    ch   = buf.channels();
    const float* data = buf.data().data();
    const size_t n    = static_cast<size_t>(w) * h;

    if (which == "Ha" || which == "OIII" || which == "SII") {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; ++i)
            mono[i] = std::clamp(data[i * ch], 0.0f, 1.0f);

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                tr("Channel dimensions don't match."));
            return;
        }

        m_chW = w; m_chH = h;
        if (!m_hasSrcMeta) { m_srcMeta = buf.metadata(); m_hasSrcMeta = true; }
        setStatusLabel(which, QFileInfo(path).fileName());

    } else if (which == "OSC") {
        if (ch >= 3) {
            m_osc.resize(n * 3);
            for (size_t i = 0; i < n; ++i) {
                m_osc[i * 3 + 0] = std::clamp(data[i * ch + 0], 0.0f, 1.0f);
                m_osc[i * 3 + 1] = std::clamp(data[i * ch + 1], 0.0f, 1.0f);
                m_osc[i * 3 + 2] = std::clamp(data[i * ch + 2], 0.0f, 1.0f);
            }
            m_oscChannels = 3;
        } else {
            m_osc.resize(n);
            for (size_t i = 0; i < n; ++i)
                m_osc[i] = std::clamp(data[i * ch], 0.0f, 1.0f);
            m_oscChannels = 1;
        }

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                tr("Channel dimensions don't match."));
            m_osc.clear(); m_oscChannels = 0;
            return;
        }

        m_chW = w; m_chH = h;
        if (!m_hasSrcMeta) { m_srcMeta = buf.metadata(); m_hasSrcMeta = true; }
        setStatusLabel(which, QFileInfo(path).fileName());
    }

    m_status->setText(tr("%1 loaded from file.").arg(which));
}

void NBtoRGBStarsDialog::setStatusLabel(const QString& which, const QString& text)
{
    QLabel* lab = nullptr;
    if      (which == "Ha")   lab = m_lblHa;
    else if (which == "OIII") lab = m_lblOIII;
    else if (which == "SII")  lab = m_lblSII;
    else if (which == "OSC")  lab = m_lblOSC;
    if (!lab) return;

    if (text.isEmpty()) {
        lab->setText(tr("No %1 loaded.").arg(which));
        lab->setStyleSheet("color:#888; margin-left:8px;");
    } else {
        lab->setText(text);
        lab->setStyleSheet("color:#2a7; font-weight:600; margin-left:8px;");
    }
}

// =============================================================================
// Preview, Push, Clear
// =============================================================================

void NBtoRGBStarsDialog::onPreviewCombine()
{
    if (m_osc.empty() && (m_ha.empty() || m_oiii.empty())) {
        QMessageBox::warning(this, tr("Missing Images"),
            tr("Load OSC, or Ha+OIII (SII optional)."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    ChannelOps::NBStarsParams params;
    params.ratio         = m_sldRatio->value()   / 100.0f;
    params.starStretch   = m_chkStarStretch->isChecked();
    params.stretchFactor = m_sldStretch->value() / 100.0f;
    params.saturation    = m_sldSat->value()     / 100.0f;
    params.applySCNR     = true;

    m_result = ChannelOps::combineNBtoRGBStars(
        m_ha, m_oiii, m_sii, m_osc,
        m_chW, m_chH, m_oscChannels, params);

    if (m_result.empty()) {
        QMessageBox::critical(this, tr("Combine Error"),
            tr("Failed to combine channels."));
        QApplication::restoreOverrideCursor();
        return;
    }

    m_pixBase->setPixmap(floatToPixmap(m_result, m_chW, m_chH, 3));
    m_scene->setSceneRect(0, 0, m_chW, m_chH);
    m_view->resetTransform();
    m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
    m_status->setText(tr("Preview updated."));

    QApplication::restoreOverrideCursor();
}

void NBtoRGBStarsDialog::onPushFinal()
{
    if (m_result.empty()) {
        onPreviewCombine();
        if (m_result.empty()) return;
    }

    ImageBuffer newBuf;
    newBuf.setData(m_chW, m_chH, 3, m_result);
    newBuf.setMetadata(m_srcMeta);

    if (m_mainWindow)
        m_mainWindow->createResultWindow(newBuf, tr("NB->RGB Stars"));
}

void NBtoRGBStarsDialog::onClear()
{
    m_ha.clear(); m_oiii.clear(); m_sii.clear();
    m_osc.clear(); m_oscChannels = 0;
    m_result.clear();
    m_hasSrcMeta = false;
    m_srcMeta    = ImageBuffer::Metadata();
    m_chW = m_chH = 0;

    m_lblHa->setText(tr("No Ha loaded."));
    m_lblOIII->setText(tr("No OIII loaded."));
    m_lblSII->setText(tr("No SII loaded."));
    m_lblOSC->setText(tr("No OSC stars loaded."));

    for (auto* lab : { m_lblHa, m_lblOIII, m_lblSII, m_lblOSC })
        lab->setStyleSheet("color:#888; margin-left:8px;");

    m_pixBase->setPixmap(QPixmap());
    m_status->setText(tr("Cleared."));
}

// =============================================================================
// Helper: Float to Pixmap
// =============================================================================

QPixmap NBtoRGBStarsDialog::floatToPixmap(const std::vector<float>& img,
                                           int w, int h, int ch)
{
    QImage qimg(w, h, QImage::Format_RGB888);

    for (int y = 0; y < h; ++y) {
        uchar* scan = qimg.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            if (ch >= 3) {
                scan[x * 3 + 0] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 0] * 255.0f), 0, 255));
                scan[x * 3 + 1] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 1] * 255.0f), 0, 255));
                scan[x * 3 + 2] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 2] * 255.0f), 0, 255));
            } else {
                const uchar v = static_cast<uchar>(std::clamp(static_cast<int>(img[idx] * 255.0f), 0, 255));
                scan[x * 3 + 0] = v;
                scan[x * 3 + 1] = v;
                scan[x * 3 + 2] = v;
            }
        }
    }

    return QPixmap::fromImage(qimg);
}