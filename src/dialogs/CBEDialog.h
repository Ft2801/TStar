#ifndef CBEDIALOG_H
#define CBEDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../astrometry/HiPSClient.h"
#include <QPointer>

class ImageViewer;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;

class CBEDialog : public DialogBase {
    Q_OBJECT
public:
    explicit CBEDialog(QWidget* parent, ImageViewer* viewer, const ImageBuffer& buffer);
    ~CBEDialog();

signals:
    void applyResult(const ImageBuffer& result);
    void progressMsg(const QString& msg);

protected:
    void closeEvent(class QCloseEvent* event) override;

private slots:
    void onApply();
    void onHiPSImageReady(const ImageBuffer& refImg);
    void onHiPSError(const QString& err);

private:
    QPointer<ImageViewer> m_viewer;
    ImageBuffer m_originalBuffer;
    HiPSClient* m_hipsClient = nullptr;

    QComboBox*  m_comboSurvey;
    QSpinBox*   m_spinScale;
    QCheckBox*  m_checkProtectStars;
    QCheckBox*  m_checkGradientMap;
    QPushButton* m_btnApply;
};

#endif // CBEDIALOG_H
