#include "LivePreviewDialog.h"
#include "../ImageBuffer.h"
#include <QVBoxLayout>
#include <QImage>
#include <algorithm>

LivePreviewDialog::LivePreviewDialog(int width, int height, QWidget* parent)
    : DialogBase(parent, "Live Mask Preview", 0, 0), m_targetWidth(width), m_targetHeight(height) {
    setWindowFlags(windowFlags() | Qt::Tool); // Make it a tool window
    
    // Calculate scaled size - limit to reasonable subwindow size (max 800x600)
    int maxW = 800;
    int maxH = 600;
    
    float scaleW = (width > maxW) ? (float)maxW / width : 1.0f;
    float scaleH = (height > maxH) ? (float)maxH / height : 1.0f;
    float scale = std::min(scaleW, scaleH);
    
    m_targetWidth = static_cast<int>(width * scale);
    m_targetHeight = static_cast<int>(height * scale);
    
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // Allow shrinking
    m_label->setScaledContents(true);
    
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_label);
    layout->setContentsMargins(0, 0, 0, 0);
    
    resize(m_targetWidth + 20, m_targetHeight + 20);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

void LivePreviewDialog::updateMask(const std::vector<float>& maskData, int width, int height, 
                                 ImageBuffer::DisplayMode mode, bool inverted, bool falseColor) {
    if (maskData.empty() || width == 0 || height == 0) {
        // Do not clear. Keep the last valid state to prevent black flickering.
        // m_label->clear(); 
        // m_label->setText(tr("No mask data"));
        return;
    }
    
    // Use ImageBuffer for advanced rendering
    ImageBuffer buf;
    buf.setData(width, height, 1, maskData);
    
    // Generate at native resolution (or constrained if needed, but 1024 is fine)
    QImage img = buf.getDisplayImage(mode, true, nullptr, width, height, false, inverted, falseColor);
    
    m_label->setPixmap(QPixmap::fromImage(img));
}
