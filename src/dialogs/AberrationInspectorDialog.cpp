#include "AberrationInspectorDialog.h"
#include <QGraphicsView>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>

AberrationInspectorDialog::AberrationInspectorDialog(const ImageBuffer& img, QWidget* parent)
    : DialogBase(parent, "Aberration Inspector", 400, 400), m_source(img), m_panelSize(200)
{
    setupUi();
    // Defer heavy panel update until after fade-in animation (300ms)
    QTimer::singleShot(300, this, &AberrationInspectorDialog::updatePanels);
}

AberrationInspectorDialog::~AberrationInspectorDialog() {}

void AberrationInspectorDialog::setSource(const ImageBuffer& img) {
    m_source = img;
    updatePanels();
}

void AberrationInspectorDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Top Controls
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->addWidget(new QLabel(tr("Panel Size:")));
    m_sizeSpin = new QSpinBox();
    m_sizeSpin->setRange(50, 500);
    m_sizeSpin->setValue(m_panelSize);
    m_sizeSpin->setSingleStep(50);
    connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int val){
        m_panelSize = val;
        updatePanels();
    });
    topLayout->addWidget(m_sizeSpin);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);
    
    // 3x3 Grid
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(2);
    
    m_panels.resize(9);
    for (int i = 0; i < 9; ++i) {
        m_panels[i].scene = new QGraphicsScene(this);
        m_panels[i].view = new QGraphicsView(m_panels[i].scene);
        m_panels[i].view->setBackgroundBrush(Qt::black);
        // Remove scrollbars for cleaner look
        m_panels[i].view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_panels[i].view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        
        int row = i / 3;
        int col = i % 3;
        grid->addWidget(m_panels[i].view, row, col);
    }
    mainLayout->addLayout(grid, 1);
    
    // Close Button
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnClose = new QPushButton(tr("Close"));
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);
}

ImageBuffer AberrationInspectorDialog::cropPanel(int x, int y, int w, int h) {
    if (!m_source.isValid()) return ImageBuffer();
    
    // Bounds check
    int imgW = m_source.width();
    int imgH = m_source.height();
    
    // Adjust logic to ensure we stay within bounds
    // x,y is top-left
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > imgW) x = imgW - w;
    if (y + h > imgH) y = imgH - h;
    
    // If image is smaller than panel?
    if (w > imgW) w = imgW;
    if (h > imgH) h = imgH;
    
    // Extract
    ImageBuffer crop;
    int c = m_source.channels();
    std::vector<float> newData(w * h * c);
    const float* srcData = m_source.data().data();
    
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            for (int k = 0; k < c; ++k) {
                int srcIdx = ((y + r) * imgW + (x + col)) * c + k;
                int dstIdx = (r * w + col) * c + k;
                newData[dstIdx] = srcData[srcIdx];
            }
        }
    }
    
    crop.setData(w, h, c, newData);
    return crop;
}

void AberrationInspectorDialog::updatePanels() {
    if (!m_source.isValid()) return;
    
    int w = m_source.width();
    int h = m_source.height();
    int p = m_panelSize;
    
    // Define 9 points (Top-Left coordinates)
    // 0: TL, 1: TC, 2: TR
    // 3: ML, 4: MC, 5: MR
    // 6: BL, 7: BC, 8: BR
    
    struct Point { int x, y; };
    Point points[9];
    
    // Top Row
    points[0] = {0, 0};
    points[1] = {(w - p) / 2, 0};
    points[2] = {w - p, 0};
    
    // Middle Row
    points[3] = {0, (h - p) / 2};
    points[4] = {(w - p) / 2, (h - p) / 2};
    points[5] = {w - p, (h - p) / 2};
    
    // Bottom Row
    points[6] = {0, h - p};
    points[7] = {(w - p) / 2, h - p};
    points[8] = {w - p, h - p};
    
    for (int i = 0; i < 9; ++i) {
        ImageBuffer crop = cropPanel(points[i].x, points[i].y, p, p);
        QImage qimg = crop.getDisplayImage();
        
        m_panels[i].scene->clear();
        QGraphicsPixmapItem* item = m_panels[i].scene->addPixmap(QPixmap::fromImage(qimg));
        m_panels[i].scene->setSceneRect(item->boundingRect());
        m_panels[i].view->fitInView(item, Qt::KeepAspectRatio); // Ensure whole panel is visible
    }
}
