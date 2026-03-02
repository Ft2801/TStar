#include "CBEDialog.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../background/CatalogGradientExtractor.h"
#include "../astrometry/WCSUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QtConcurrent>

CBEDialog::CBEDialog(QWidget* pParent, ImageViewer* viewer, const ImageBuffer& buffer)
    : DialogBase(pParent), m_viewer(viewer), m_originalBuffer(buffer)
{
    setWindowTitle(tr("Catalog Background Extraction (MARS-like)"));
    setMinimumWidth(380);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout;

    // Survey selector
    m_comboSurvey = new QComboBox(this);
    m_comboSurvey->addItem(tr("PanSTARRS DR1 (Color)"), HiPSClient::SUR_PANSTARRS_DR1_COLOR);
    m_comboSurvey->addItem(tr("DSS2 Red (Optical)"),    HiPSClient::SUR_DSS2_RED);
    m_comboSurvey->addItem(tr("unWISE (Infrared)"),     HiPSClient::SUR_UNWISE_COLOR);
    form->addRow(tr("HiPS Survey:"), m_comboSurvey);

    // Smoothing scale (sigma for Gaussian blur)
    m_spinScale = new QSpinBox(this);
    m_spinScale->setRange(16, 1024);
    m_spinScale->setValue(128);
    m_spinScale->setSingleStep(16);
    m_spinScale->setSuffix(tr(" px"));
    m_spinScale->setToolTip(tr("Gaussian sigma for large-scale background extraction.\n"
                                "Larger values capture broader gradients."));
    form->addRow(tr("Smoothing Scale:"), m_spinScale);

    mainLayout->addLayout(form);

    // Options
    m_checkProtectStars = new QCheckBox(tr("Protect Stars (Morphological filter)"), this);
    m_checkProtectStars->setChecked(true);
    m_checkProtectStars->setToolTip(tr("Applies morphological opening before blurring\n"
                                        "to prevent bright stars from biasing the gradient map."));
    mainLayout->addWidget(m_checkProtectStars);

    m_checkGradientMap = new QCheckBox(tr("Output Gradient Map Only (Debug)"), this);
    m_checkGradientMap->setToolTip(tr("Instead of correcting the image, output the\n"
                                       "computed gradient map for inspection."));
    mainLayout->addWidget(m_checkGradientMap);

    mainLayout->addStretch();

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout;

    QPushButton* btnClearCache = new QPushButton(tr("Clear Cache"), this);
    btnClearCache->setToolTip(tr("Delete all cached HiPS reference images."));
    btnLayout->addWidget(btnClearCache);
    btnLayout->addStretch();

    m_btnApply = new QPushButton(tr("Apply"), this);
    m_btnApply->setDefault(true);
    btnLayout->addWidget(m_btnApply);

    QPushButton* btnClose = new QPushButton(tr("Close"), this);
    btnLayout->addWidget(btnClose);

    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_btnApply, &QPushButton::clicked, this, &CBEDialog::onApply);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

    connect(btnClearCache, &QPushButton::clicked, this, [this]() {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_hipsClient->clearCache();
        QApplication::restoreOverrideCursor();
        QMessageBox::information(this, tr("Cache Cleared"),
                                 tr("Reference image cache has been emptied."));
    });

    m_hipsClient = new HiPSClient(this);
    connect(m_hipsClient, &HiPSClient::imageReady,     this, &CBEDialog::onHiPSImageReady);
    connect(m_hipsClient, &HiPSClient::errorOccurred,  this, &CBEDialog::onHiPSError);

    if (auto cb = getCallbacks())
        cb->logMessage(tr("Catalog Background Extraction tool active."), 0);
}

CBEDialog::~CBEDialog() {
}

void CBEDialog::closeEvent(QCloseEvent* event) {
    DialogBase::closeEvent(event);
}

void CBEDialog::onApply() {
    const auto& meta = m_originalBuffer.metadata();

    if (!WCSUtils::hasValidWCS(meta)) {
        QMessageBox::warning(this, tr("No Astrometry"),
                             tr("Image must be Plate Solved before using Catalog Background Extraction."));
        return;
    }

    m_btnApply->setEnabled(false);
    setCursor(Qt::WaitCursor);

    if (auto cb = getCallbacks()) {
        cb->logMessage(tr("Requesting reference survey from HiPS..."), 0);
        cb->startLongProcess();
    }

    // Calculate FoV from WCS
    double fov_x = 0, fov_y = 0;
    if (!WCSUtils::getFieldOfView(meta, m_originalBuffer.width(), m_originalBuffer.height(), fov_x, fov_y))
        fov_x = 1.0; // Fallback 1 degree

    QString survey = m_comboSurvey->currentData().toString();
    m_hipsClient->fetchFITS(survey, meta.ra, meta.dec, fov_x,
                             m_originalBuffer.width(), m_originalBuffer.height());
}

void CBEDialog::onHiPSImageReady(const ImageBuffer& refImg) {
    if (auto cb = getCallbacks()) {
        cb->endLongProcess();
        cb->logMessage(tr("Reference received (%1x%2, %3ch). Running gradient extraction...")
                           .arg(refImg.width()).arg(refImg.height()).arg(refImg.channels()), 0);
    }

    // Capture options for the worker thread
    Background::CatalogGradientExtractor::Options opts;
    opts.blurScale       = m_spinScale->value();
    opts.protectStars    = m_checkProtectStars->isChecked();
    opts.outputGradientMap = m_checkGradientMap->isChecked();

    ImageBuffer target = m_originalBuffer;
    ImageBuffer ref    = refImg;

    // Run extraction on a background thread to keep the UI responsive
    auto future = QtConcurrent::run([target, ref, opts]() mutable -> QPair<bool, ImageBuffer> {
        bool ok = Background::CatalogGradientExtractor::extract(target, ref, opts);
        return qMakePair(ok, target);
    });

    // Use a watcher to get the result back on the GUI thread
    auto* watcher = new QFutureWatcher<QPair<bool, ImageBuffer>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, ImageBuffer>>::finished, this, [this, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();

        if (result.first) {
            if (auto cb = getCallbacks())
                cb->logMessage(tr("Background extracted successfully."), 1);
            emit applyResult(result.second);
        } else {
            if (auto cb = getCallbacks())
                cb->logMessage(tr("Extraction failed during gradient matching."), 3);
            QMessageBox::warning(this, tr("Error"),
                                 tr("Failed to match gradients with the reference catalog."));
        }

        m_btnApply->setEnabled(true);
        unsetCursor();
    });
    watcher->setFuture(future);
}

void CBEDialog::onHiPSError(const QString& err) {
    if (auto cb = getCallbacks()) {
        cb->endLongProcess();
        cb->logMessage(tr("Catalog download failed: %1").arg(err), 3);
    }
    QMessageBox::critical(this, tr("HiPS Error"), err);
    m_btnApply->setEnabled(true);
    unsetCursor();
}
