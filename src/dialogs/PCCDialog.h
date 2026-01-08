#ifndef PCCDIALOG_H
#define PCCDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include "photometry/CatalogClient.h"
#include "photometry/PCCCalibrator.h"
#include "../ImageBuffer.h"

#include <QPointer>
#include "../ImageViewer.h"

#include "DialogBase.h"

class PCCDialog : public DialogBase {
    Q_OBJECT
public:
    explicit PCCDialog(ImageViewer* viewer, QWidget* parent = nullptr);

    void setViewer(ImageViewer* v);

    PCCResult result() const { return m_result; }

private slots:
    void onRun();
    void onCatalogReady(const std::vector<CatalogStar>& stars);
    void onCatalogError(const QString& err);

private:
    QPointer<ImageViewer> m_viewer;
    QLabel* m_status;
    QCheckBox* m_chkNeutralizeBackground;
    QPushButton* m_btnRun;
    
    CatalogClient* m_catalog;
    PCCCalibrator* m_calibrator;
    PCCResult m_result;
};

#endif // PCCDIALOG_H
