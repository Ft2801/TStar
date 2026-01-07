#include "ApplyMaskDialog.h"
#include <QImage>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <opencv2/opencv.hpp>

ApplyMaskDialog::ApplyMaskDialog(int targetWidth, int targetHeight, QWidget* parent)
    : QDialog(parent), m_targetWidth(targetWidth), m_targetHeight(targetHeight)
{
    setWindowTitle(tr("Apply Mask"));
    resize(600, 400);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // Left: List
    QVBoxLayout* listLayout = new QVBoxLayout();
    listLayout->addWidget(new QLabel(tr("Available Masks:")));
    m_listWidget = new QListWidget();
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &ApplyMaskDialog::onSelectionChanged);
    listLayout->addWidget(m_listWidget);
    mainLayout->addLayout(listLayout, 1);

    // Right: Preview
    QVBoxLayout* previewLayout = new QVBoxLayout();
    QGroupBox* previewGroup = new QGroupBox(tr("Preview"));
    QVBoxLayout* groupLayout = new QVBoxLayout(previewGroup);
    m_previewLabel = new QLabel(tr("Select a mask to see preview"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(256, 256);
    m_previewLabel->setStyleSheet("background-color: #1a1a1a; border: 1px solid #444;");
    groupLayout->addWidget(m_previewLabel);
    previewLayout->addWidget(previewGroup, 1);

    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    previewLayout->addWidget(bbox);

    mainLayout->addLayout(previewLayout, 1);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

void ApplyMaskDialog::addAvailableMask(const QString& name, const MaskLayer& mask, bool isView) {
    QString displayName = isView ? tr("[View] %1").arg(name) : tr("[Saved] %1").arg(name);
    m_availableMasks[displayName] = mask;
    m_listWidget->addItem(displayName);
}

MaskLayer ApplyMaskDialog::getSelectedMask() const {
    return m_selectedMask;
}

void ApplyMaskDialog::onSelectionChanged() {
    QListWidgetItem* item = m_listWidget->currentItem();
    if (!item) return;

    QString name = item->text();
    if (m_availableMasks.contains(name)) {
        m_selectedMask = m_availableMasks[name];
        updatePreview(m_selectedMask);
    }
}

void ApplyMaskDialog::updatePreview(const MaskLayer& mask) {
    if (!mask.isValid()) {
        m_previewLabel->setText(tr("Invalid Mask Data"));
        return;
    }

    int w = mask.width;
    int h = mask.height;
    
    // Create grayscale QImage from mask data
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float val = mask.data[y * w + x];
            line[x] = static_cast<uchar>(std::clamp(val * 255.0f, 0.0f, 255.0f));
        }
    }

    // Scale to fit preview label
    QPixmap pix = QPixmap::fromImage(img);
    m_previewLabel->setPixmap(pix.scaled(m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
