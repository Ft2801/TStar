#include "PerfectPaletteDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QIcon>

PerfectPaletteDialog::PerfectPaletteDialog(QWidget* parent)
    : DialogBase(parent, tr("Perfect Palette Picker"), 1000, 700), m_selectedPalette("SHO")
{
    m_mainWin = getCallbacks();
    createUI();
}

void PerfectPaletteDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

void PerfectPaletteDialog::createUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: Controls
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    ctrlLayout->setContentsMargins(10, 10, 10, 10);
    
    ctrlLayout->addWidget(new QLabel(tr("<b>Narrowband Channels</b>")));
    
    auto addLoadBtn = [&](const QString& name, QLabel** lbl) {
        QPushButton* btn = new QPushButton(tr("Load %1...").arg(name));
        *lbl = new QLabel(tr("Not loaded"));
        (*lbl)->setStyleSheet("color: gray;");
        ctrlLayout->addWidget(btn);
        ctrlLayout->addWidget(*lbl);
        connect(btn, &QPushButton::clicked, this, [this, name](){ onLoadChannel(name); });
    };
    
    addLoadBtn("Ha", &m_lblHa);
    addLoadBtn("OIII", &m_lblOiii);
    addLoadBtn("SII", &m_lblSii);
    
    ctrlLayout->addSpacing(20);
    ctrlLayout->addWidget(new QLabel(tr("<b>Intensities</b>")));
    
    auto addSlider = [&](const QString& name, QSlider** sld, QLabel** val) {
        QHBoxLayout* hl = new QHBoxLayout();
        hl->addWidget(new QLabel(name + ":"));
        *sld = new QSlider(Qt::Horizontal);
        (*sld)->setRange(0, 200);
        (*sld)->setValue(100);
        *val = new QLabel("1.00");
        hl->addWidget(*sld);
        hl->addWidget(*val);
        ctrlLayout->addLayout(hl);
        connect(*sld, &QSlider::valueChanged, this, &PerfectPaletteDialog::onIntensityChanged);
    };
    
    addSlider("Ha", &m_sliderHa, &m_lblValHa);
    addSlider("OIII", &m_sliderOiii, &m_lblValOiii);
    addSlider("SII", &m_sliderSii, &m_lblValSii);
    
    ctrlLayout->addSpacing(20);
    ctrlLayout->addWidget(new QLabel(tr("<b>Stretch Settings</b>")));
    
    // Auto Stretch Checkbox
    m_chkAutoStretch = new QCheckBox(tr("Auto Stretch Channels"));
    m_chkAutoStretch->setChecked(true);
    m_chkAutoStretch->setToolTip(tr("Automatically stretch linear data to non-linear for preview/result."));
    ctrlLayout->addWidget(m_chkAutoStretch);
    
    // Stretch Strength Slider
    QHBoxLayout* hlStretch = new QHBoxLayout();
    hlStretch->addWidget(new QLabel(tr("Strength:")));
    m_sliderStretch = new QSlider(Qt::Horizontal);
    m_sliderStretch->setRange(10, 50); // 0.10 to 0.50
    m_sliderStretch->setValue(25); // Default 0.25
    m_lblStretchVal = new QLabel("0.25");
    hlStretch->addWidget(m_sliderStretch);
    hlStretch->addWidget(m_lblStretchVal);
    ctrlLayout->addLayout(hlStretch);
    
    // Connections
    connect(m_chkAutoStretch, &QCheckBox::toggled, this, &PerfectPaletteDialog::onStretchChanged);
    connect(m_sliderStretch, &QSlider::valueChanged, this, &PerfectPaletteDialog::onStretchChanged);
    
    ctrlLayout->addStretch();
    
    QPushButton* btnApply = new QPushButton(tr("Apply to New View"));
    btnApply->setFixedHeight(40);
    btnApply->setStyleSheet("background-color: #2c3e50; color: white; font-weight: bold;");
    connect(btnApply, &QPushButton::clicked, this, &PerfectPaletteDialog::onApply);
    ctrlLayout->addWidget(btnApply);
    
    QLabel* copyright = new QLabel(tr("© 2026 SetiAstro"), this);
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setStyleSheet("color: gray; font-size: 10px;");
    ctrlLayout->addWidget(copyright);
    
    mainLayout->addLayout(ctrlLayout, 1);
    
    // Right: Palettes & Preview
    QVBoxLayout* previewLayout = new QVBoxLayout();
    
    m_lblPreview = new QLabel();
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background-color: black; border: 1px solid #333;");
    m_lblPreview->setMinimumSize(400, 300);
    previewLayout->addWidget(m_lblPreview, 3);
    
    QWidget* gridContainer = new QWidget();
    m_gridPalettes = new QGridLayout(gridContainer);
    m_gridPalettes->setSpacing(5);
    
    QStringList palettes = {"SHO", "HOO", "HSO", "HOS", "OSS", "OHH", "Realistic1", "Realistic2", "Foraxx"};
    int row = 0, col = 0;
    for (const QString& p : palettes) {
        QPushButton* b = new QPushButton(p);
        b->setFixedSize(90, 40);
        b->setCheckable(true);
        if (p == "SHO") b->setChecked(true);
        m_gridPalettes->addWidget(b, row, col);
        
        PaletteThumb thumb;
        thumb.btn = b;
        thumb.name = p;
        m_thumbs.append(thumb);
        
        connect(b, &QPushButton::clicked, this, [this, p](){ onPaletteSelected(p); });
        
        col++;
        if (col > 2) { col = 0; row++; }
    }
    
    previewLayout->addWidget(gridContainer);
    
    mainLayout->addLayout(previewLayout, 3);
}

void PerfectPaletteDialog::onLoadChannel(const QString& channel) {
    if (!m_mainWin) return;
    QList<ImageViewer*> viewers = m_mainWin->getCurrentViewer()->window()->findChildren<ImageViewer*>();
    QStringList titles;
    QMap<QString, ImageViewer*> map;
    for (ImageViewer* v : viewers) {
        if (!v->windowTitle().isEmpty()) {
            titles << v->windowTitle();
            map[v->windowTitle()] = v;
        }
    }
    
    if (titles.isEmpty()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    int initialIdx = 0;
    if (m_viewer && !m_viewer->windowTitle().isEmpty()) {
        initialIdx = titles.indexOf(m_viewer->windowTitle());
        if (initialIdx < 0) initialIdx = 0;
    }
    
    bool ok;
    QString item = QInputDialog::getItem(this, tr("Select View"), tr("Source for %1:").arg(channel), titles, initialIdx, false, &ok);
    if (ok && !item.isEmpty()) {
        ImageViewer* v = map[item];
        if (channel == "Ha") { 
            m_ha = v->getBuffer(); 
            m_previewHa = m_ha; // Start with copy
            // Downscale to ~1024px max dimension for speed
            float scale = 1024.0f / std::max(m_ha.width(), m_ha.height());
            if (scale < 1.0f) {
                 // Simple nearest-neighbor downsampling for fast preview generation.

                 int newW = (int)(m_ha.width() * scale);
                 int newH = (int)(m_ha.height() * scale);
                 ImageBuffer small;
                 small.resize(newW, newH, m_ha.channels());
                 // Nearest neighbor subsample is fast and enough for color preview
                 #pragma omp parallel for
                 for(int y=0; y<newH; ++y) {
                     int sy = (int)(y / scale);
                     for(int x=0; x<newW; ++x) {
                         int sx = (int)(x / scale);
                         for(int c=0; c<m_ha.channels(); ++c) {
                             small.data()[(y*newW+x)*m_ha.channels()+c] = m_ha.getPixelValue(sx, sy, c);
                         }
                     }
                 }
                 m_previewHa = small;
            }
            m_lblHa->setText(item); m_lblHa->setStyleSheet("color: green;"); 
        }
        if (channel == "OIII") { 
            m_oiii = v->getBuffer(); 
            m_previewOiii = m_oiii;
            float scale = 1024.0f / std::max(m_oiii.width(), m_oiii.height());
            if (scale < 1.0f) {
                 int newW = (int)(m_oiii.width() * scale);
                 int newH = (int)(m_oiii.height() * scale);
                 ImageBuffer small;
                 small.resize(newW, newH, m_oiii.channels());
                 #pragma omp parallel for
                 for(int y=0; y<newH; ++y) {
                     int sy = (int)(y / scale);
                     for(int x=0; x<newW; ++x) {
                         int sx = (int)(x / scale);
                         for(int c=0; c<m_oiii.channels(); ++c) {
                             small.data()[(y*newW+x)*m_oiii.channels()+c] = m_oiii.getPixelValue(sx, sy, c);
                         }
                     }
                 }
                 m_previewOiii = small;
            }
            m_lblOiii->setText(item); m_lblOiii->setStyleSheet("color: green;"); 
        }
        if (channel == "SII") { 
            m_sii = v->getBuffer(); 
            m_previewSii = m_sii;
            float scale = 1024.0f / std::max(m_sii.width(), m_sii.height());
            if (scale < 1.0f) {
                 int newW = (int)(m_sii.width() * scale);
                 int newH = (int)(m_sii.height() * scale);
                 ImageBuffer small;
                 small.resize(newW, newH, m_sii.channels());
                 #pragma omp parallel for
                 for(int y=0; y<newH; ++y) {
                     int sy = (int)(y / scale);
                     for(int x=0; x<newW; ++x) {
                         int sx = (int)(x / scale);
                         for(int c=0; c<m_sii.channels(); ++c) {
                             small.data()[(y*newW+x)*m_sii.channels()+c] = m_sii.getPixelValue(sx, sy, c);
                         }
                     }
                 }
                 m_previewSii = small;
            }
            m_lblSii->setText(item); m_lblSii->setStyleSheet("color: green;"); 
        }
        
        onCreatePalettes();
    }
}

void PerfectPaletteDialog::onIntensityChanged() {
    m_lblValHa->setText(QString::number(m_sliderHa->value() / 100.0, 'f', 2));
    m_lblValOiii->setText(QString::number(m_sliderOiii->value() / 100.0, 'f', 2));
    m_lblValSii->setText(QString::number(m_sliderSii->value() / 100.0, 'f', 2));
    
    onCreatePalettes();
}

void PerfectPaletteDialog::onPaletteSelected(const QString& name) {
    m_selectedPalette = name;
    for (auto& t : m_thumbs) {
        t.btn->setChecked(t.name == name);
    }
    onCreatePalettes();
}

    // Logic updates
    
void PerfectPaletteDialog::onStretchChanged() {
    float val = m_sliderStretch->value() / 100.0f;
    m_lblStretchVal->setText(QString::number(val, 'f', 2));
    
    // Enable/Disable slider based on checkbox
    m_sliderStretch->setEnabled(m_chkAutoStretch->isChecked());
    m_lblStretchVal->setEnabled(m_chkAutoStretch->isChecked());
    
    onCreatePalettes();
}

void PerfectPaletteDialog::onCreatePalettes() {
    if (!m_oiii.isValid() || (!m_ha.isValid() && !m_sii.isValid())) return;
    
    PerfectPaletteParams params;
    params.paletteName = m_selectedPalette;
    params.haFactor = m_sliderHa->value() / 100.0f;
    params.oiiiFactor = m_sliderOiii->value() / 100.0f;
    params.siiFactor = m_sliderSii->value() / 100.0f;
    
    // Get Stretch Settings
    params.applyStatisticalStretch = m_chkAutoStretch->isChecked();
    params.targetMedian = m_sliderStretch->value() / 100.0f;

    ImageBuffer result;
    QString err;
    
    // For preview, we use the downscaled preview buffers
    if (m_runner.run(&m_previewHa, &m_previewOiii, &m_previewSii, result, params, &err)) {
        // Convert ImageBuffer to QImage for preview
        int w = result.width();
        int h = result.height();
        QImage qimg(w, h, QImage::Format_RGB888);
        const float* data = result.data().data();
        
        // Parallel conversion for responsiveness
        #pragma omp parallel for
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = (y * w + x) * 3;
                qimg.setPixel(x, y, qRgb(
                    std::min(255, std::max(0, (int)(data[idx + 0] * 255))),
                    std::min(255, std::max(0, (int)(data[idx + 1] * 255))),
                    std::min(255, std::max(0, (int)(data[idx + 2] * 255)))
                ));
            }
        }
        
        m_lblPreview->setPixmap(QPixmap::fromImage(qimg).scaled(m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void PerfectPaletteDialog::onApply() {
    if (!m_oiii.isValid() || (!m_ha.isValid() && !m_sii.isValid())) return;
    
    PerfectPaletteParams params;
    params.paletteName = m_selectedPalette;
    params.haFactor = m_sliderHa->value() / 100.0f;
    params.oiiiFactor = m_sliderOiii->value() / 100.0f;
    params.siiFactor = m_sliderSii->value() / 100.0f;
    
    // Get Stretch Settings
    params.applyStatisticalStretch = m_chkAutoStretch->isChecked();
    params.targetMedian = m_sliderStretch->value() / 100.0f;

    ImageBuffer result;
    QString err;
    if (m_runner.run(&m_ha, &m_oiii, &m_sii, result, params, &err)) {
        if (m_mainWin) m_mainWin->createResultWindow(result, "Palette_" + m_selectedPalette);
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"), err);
    }
}
