#include "BackgroundNeutralizationDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../core/RobustStatistics.h"
#include "../core/RobustStatistics.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDebug>
#include <QApplication>
#include <algorithm>
#include <numeric>

BackgroundNeutralizationDialog::BackgroundNeutralizationDialog(QWidget* parent)
    : DialogBase(parent, tr("Background Neutralization"), 350, 150)
{
    
    // Interaction Control: Initially disabled until focused
    m_interactionEnabled = false;
    
    setupUI();
}

void BackgroundNeutralizationDialog::setInteractionEnabled(bool enabled) {
    if (m_interactionEnabled == enabled) return;
    m_interactionEnabled = enabled;
    
    // Force update based on new state
    if (m_activeViewer) {
        setSelectionMode(m_interactionEnabled);
    }
}

BackgroundNeutralizationDialog::~BackgroundNeutralizationDialog() {
    setSelectionMode(false);
}

void BackgroundNeutralizationDialog::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QLabel* info = new QLabel(tr("Select a background reference on the image by drawing a rectangle."), this);
    info->setWordWrap(true);
    layout->addWidget(info);
    // Status
    m_statusLabel = new QLabel(tr("Ready - Please select a background region in the viewer."), this);
    layout->addWidget(m_statusLabel);
    
    layout->addStretch();
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnApply = new QPushButton(tr("Apply"), this);
    m_btnApply->setEnabled(false);
    m_btnCancel = new QPushButton(tr("Cancel"), this);
    
    connect(m_btnApply, &QPushButton::clicked, this, &BackgroundNeutralizationDialog::onApply);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addWidget(m_btnApply);
    btnLayout->addWidget(m_btnCancel);
    layout->addLayout(btnLayout);
}

void BackgroundNeutralizationDialog::setSelectionMode(bool active) {
    if (m_activeViewer) {
        if (active) {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
            m_activeViewer->setRegionSelectedCallback([this](QRectF r) { this->onRectSelected(r); });
        } else {
            qDebug() << "[BackgroundNeutralization::setSelectionMode] Disabling. Viewer:" << m_activeViewer << "Visible?" << (m_activeViewer ? m_activeViewer->isVisible() : false);
            if (m_activeViewer) {
                 m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
                 m_activeViewer->clearSelection();
                 m_activeViewer->setRegionSelectedCallback(nullptr);
            }
        }
    }
}

void BackgroundNeutralizationDialog::setViewer(ImageViewer* viewer) {
    if (m_activeViewer != viewer) {
        if (m_activeViewer) {
            setSelectionMode(false); // Disable on old
        }
        m_activeViewer = viewer;
        if (m_activeViewer && m_interactionEnabled) {
            setSelectionMode(true);
        }
    }
}

void BackgroundNeutralizationDialog::onRectSelected(const QRectF& r) {
    qDebug() << "[BackgroundNeutralizationDialog::onRectSelected] Received rect:" << r;
    try {
        // Safety checks to prevent potential crashes
        if (!m_statusLabel || !m_btnApply) {
            return;
        }
        m_selection = r.toRect();
        if (m_selection.width() >= 2 && m_selection.height() >= 2) {
            m_hasSelection = true;
            m_statusLabel->setText(tr("Selection: %1x%2 at %3,%4")
                .arg(m_selection.width()).arg(m_selection.height())
                .arg(m_selection.x()).arg(m_selection.y()));
            m_btnApply->setEnabled(true);
        } else {
            m_hasSelection = false;
            m_statusLabel->setText(tr("Selection too small."));
            m_btnApply->setEnabled(false);
        }
    } catch (const std::exception& e) {
             qWarning() << "BackgroundNeutralization selection error:" << e.what();
    } catch (...) {
             qWarning() << "BackgroundNeutralization selection error: unknown";
    }
}

void BackgroundNeutralizationDialog::onApply() {
    if (!m_hasSelection) return;
    
    emit apply(m_selection);
    
    // Repeatable selection interaction - dialog stays open.
    // So we don't close, just log success (MainWindow handles the logic and update)
}

void BackgroundNeutralizationDialog::closeEvent(QCloseEvent* event) {
    setSelectionMode(false);
    QDialog::closeEvent(event);
}

void BackgroundNeutralizationDialog::neutralizeBackground(ImageBuffer& img, const QRect& rect) {
    if (img.channels() != 3) return;

    int w = img.width();
    int h = img.height();
    int ch = img.channels();
    
    // Lock for writing before accessing data reference
    ImageBuffer::WriteLock lock(&img);
    std::vector<float>& data = img.data();
    if (data.empty()) return; // SWAP SAFETY


    // 1. Extract sample data
    std::vector<float> sample[3];
    int x0 = std::clamp(rect.x(), 0, w - 1);
    int y0 = std::clamp(rect.y(), 0, h - 1);
    int x1 = std::clamp(rect.x() + rect.width(), 0, w);
    int y1 = std::clamp(rect.y() + rect.height(), 0, h);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            // Sanitize input: Skip NaNs or Inf
            if (std::isfinite(data[idx])) sample[0].push_back(data[idx]);
            if (std::isfinite(data[idx + 1])) sample[1].push_back(data[idx + 1]);
            if (std::isfinite(data[idx + 2])) sample[2].push_back(data[idx + 2]);
        }
    }

    if (sample[0].empty() || sample[1].empty() || sample[2].empty()) return;

    // 2. Compute Medians and Average Median (ref)
    double med[3];
    double avgMed = 0;
    for (int c = 0; c < 3; ++c) {
        med[c] = RobustStatistics::getMedian(sample[c]);
        avgMed += med[c];
    }
    avgMed /= 3.0;

    // 3. Compute Offsets based on Mean 
    double offset[3];
    for (int c = 0; c < 3; ++c) {
        double mean = std::accumulate(sample[c].begin(), sample[c].end(), 0.0) / sample[c].size();
        offset[c] = mean - avgMed;
        // Safety check for calculated offset
        if (!std::isfinite(offset[c])) offset[c] = 0.0;
    }

    // 4. Apply Subtraction
    size_t totalPixels = static_cast<size_t>(w) * h;
    #pragma omp parallel for
    for (size_t i = 0; i < totalPixels; ++i) {
        size_t idx = i * ch;
        for (int c = 0; c < 3; ++c) {
            float v = data[idx + c] - static_cast<float>(offset[c]);
            data[idx + c] = std::clamp(v, 0.0f, 1.0f);
        }
    }
}
