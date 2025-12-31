#include "StarRecompositionDialog.h"
#include "../MainWindow.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QMdiSubWindow>
#include <QLabel>
#include <QPushButton>
#include <QIcon>

StarRecompositionDialog::StarRecompositionDialog(MainWindow* mainWin, QWidget* parent)
    : QDialog(parent), m_mainWin(mainWin)
{
    setWindowTitle(tr("Star Recomposition"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    createUI();
    populateCombos();
    
    // Set a reasonable minimum size
    setMinimumWidth(400);
    
    m_initializing = false;
    // Trigger initial update if possible
    m_initializing = false;
    // Trigger initial update if possible
}

void StarRecompositionDialog::setViewer(ImageViewer* v) {
    Q_UNUSED(v);
    populateCombos(); // Refresh list to ensure new viewer is available
}

void StarRecompositionDialog::createUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: Controls
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    
    QGridLayout* grid = new QGridLayout();
    
    // Starless Source
    grid->addWidget(new QLabel(tr("Starless View:")), 0, 0);
    m_cmbStarless = new QComboBox();
    m_cmbStarless->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbStarless, 0, 1);
    
    // Stars Source
    grid->addWidget(new QLabel(tr("Stars-Only View:")), 1, 0);
    m_cmbStars = new QComboBox();
    m_cmbStars->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbStars, 1, 1);
    
    // Blend Mode
    grid->addWidget(new QLabel(tr("Blend Mode:")), 2, 0);
    m_cmbMode = new QComboBox();
    m_cmbMode->addItem(tr("Screen"), StarRecompositionParams::Screen);
    m_cmbMode->addItem(tr("Add"), StarRecompositionParams::Add);
    m_cmbMode->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbMode, 2, 1);
    
    ctrlLayout->addLayout(grid);
    
    // Blend Ratio
    QHBoxLayout* ratioLayout = new QHBoxLayout();
    ratioLayout->addWidget(new QLabel(tr("Ratio:")));
    m_sliderRatio = new QSlider(Qt::Horizontal);
    m_sliderRatio->setRange(0, 100);
    m_sliderRatio->setValue(100);
    m_lblRatio = new QLabel("1.00");
    m_lblRatio->setFixedWidth(30);
    ratioLayout->addWidget(m_sliderRatio);
    ratioLayout->addWidget(m_lblRatio);
    ctrlLayout->addLayout(ratioLayout);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    
    btnLayout->addStretch();
    btnLayout->addWidget(btnApply);
    btnLayout->addWidget(btnCancel);
    ctrlLayout->addLayout(btnLayout);
    
    mainLayout->addLayout(ctrlLayout, 1);

    // Right: Preview
    QVBoxLayout* previewLayout = new QVBoxLayout();
    
    // Preview Toolbar
    QHBoxLayout* pToolbar = new QHBoxLayout();
    pToolbar->addWidget(new QLabel(tr("Preview:")));
    m_btnFit = new QPushButton(tr("Fit"));
    // Make them small
    m_btnFit->setFixedWidth(40);
    pToolbar->addWidget(m_btnFit);
    pToolbar->addStretch();
    previewLayout->addLayout(pToolbar);

    m_previewViewer = new ImageViewer(this);
    m_previewViewer->setProperty("isPreview", true);
    m_previewViewer->setMinimumSize(400, 400);
    previewLayout->addWidget(m_previewViewer);
    
    mainLayout->addLayout(previewLayout, 3);
    
    
    // Connect signals
    connect(btnApply, &QPushButton::clicked, this, &StarRecompositionDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    connect(m_btnFit, &QPushButton::clicked, m_previewViewer, &ImageViewer::fitToWindow);

    connect(m_sliderRatio, &QSlider::valueChanged, this, [this](int val){
        m_lblRatio->setText(QString::number(val / 100.0, 'f', 2));
        onUpdatePreview();
    });
    
    connect(m_cmbStarless, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    connect(m_cmbStars, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    connect(m_cmbMode, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
}

void StarRecompositionDialog::populateCombos() {
    m_cmbStarless->clear();
    m_cmbStars->clear();
    
    QList<ImageViewer*> viewers = m_mainWin->findChildren<ImageViewer*>();
    for (ImageViewer* v : viewers) {
        if (v == m_previewViewer) continue; // Skip our own preview window
        QString title = v->windowTitle();
        if (title.isEmpty()) continue;
        
        m_cmbStarless->addItem(title, QVariant::fromValue((void*)v));
        m_cmbStars->addItem(title, QVariant::fromValue((void*)v));
    }
}

void StarRecompositionDialog::onRefreshViews() {
    populateCombos();
}

void StarRecompositionDialog::onUpdatePreview() {
    if (m_initializing) return;
    
    ImageViewer* starlessViewer = (ImageViewer*)m_cmbStarless->currentData().value<void*>();
    ImageViewer* starsViewer = (ImageViewer*)m_cmbStars->currentData().value<void*>();
    
    if (!starlessViewer || !starsViewer) return;
    
    QImage qSll = starlessViewer->getCurrentDisplayImage();
    QImage qStr = starsViewer->getCurrentDisplayImage();
    
    if (qSll.isNull() || qStr.isNull()) return;

    // Ensure sizes match logic (resize stars to starless)
    if (qStr.size() != qSll.size()) {
       qStr = qStr.scaled(qSll.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    int qw = qSll.width();
    int qh = qSll.height();
    
    // Create temp buffers from display images
    ImageBuffer bufSll, bufStr;
    bufSll.setData(qw, qh, 3, {}); 
    bufStr.setData(qw, qh, 3, {});
    
    qSll = qSll.convertToFormat(QImage::Format_RGB888);
    qStr = qStr.convertToFormat(QImage::Format_RGB888);
    
    float* fSll = bufSll.data().data();
    float* fStr = bufStr.data().data();
    
    // Use scanLine to respect padding/stride
    for (int y = 0; y < qh; ++y) {
        const uchar* lineS = qSll.constScanLine(y);
        const uchar* lineT = qStr.constScanLine(y);
        for (int x = 0; x < qw; ++x) {
            size_t idx = (static_cast<size_t>(y) * qw + x) * 3;
            fSll[idx + 0] = lineS[x*3+0] / 255.0f;
            fSll[idx + 1] = lineS[x*3+1] / 255.0f;
            fSll[idx + 2] = lineS[x*3+2] / 255.0f;
            
            fStr[idx + 0] = lineT[x*3+0] / 255.0f;
            fStr[idx + 1] = lineT[x*3+1] / 255.0f;
            fStr[idx + 2] = lineT[x*3+2] / 255.0f;
        }
    }
    
    // Run Runner logic
    ImageBuffer result;
    StarRecompositionParams params;
    params.mode = (StarRecompositionParams::BlendMode)m_cmbMode->currentData().toInt();
    params.ratio = m_sliderRatio->value() / 100.0f;
    
    QString err;
    if (m_runner.run(bufSll, bufStr, result, params, &err)) {
        // Set result to preview viewer
        // Important: Force Linear display state so we see the exact output of the blend (0-1)
        // without auto-stretch interfering.
        m_previewViewer->setBuffer(result, "Preview"); 
        m_previewViewer->setDisplayState(ImageBuffer::Display_Linear, false);
    }
}

void StarRecompositionDialog::onApply() {
    ImageViewer* starlessViewer = (ImageViewer*)m_cmbStarless->currentData().value<void*>();
    ImageViewer* starsViewer = (ImageViewer*)m_cmbStars->currentData().value<void*>();
    
    if (!starlessViewer || !starsViewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select both Starless and Stars-Only views."));
        return;
    }
    
    StarRecompositionParams params;
    params.mode = (StarRecompositionParams::BlendMode)m_cmbMode->currentData().toInt();
    params.ratio = m_sliderRatio->value() / 100.0f;
    
    ImageBuffer result;
    QString err;
    if (m_runner.run(starlessViewer->getBuffer(), starsViewer->getBuffer(), result, params, &err)) {
        m_mainWin->createNewImageWindow(result, "Stars_Recomposed");
        accept();
    } else {
        QMessageBox::critical(this, tr("Processing Error"), err);
    }
}
