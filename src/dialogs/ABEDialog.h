#ifndef ABEDIALOG_H
#define ABEDIALOG_H

#include <QDialog>
#include "ImageBuffer.h"

class ImageViewer; // Fwd Decl

#include <QPointer>

class ABEDialog : public QDialog {
    Q_OBJECT
public:
    explicit ABEDialog(QWidget* parent, ImageViewer* viewer, const ImageBuffer& buffer, bool initialStretch);
    ~ABEDialog();
    
    void setAbeMode(bool enabled);
    void setViewer(ImageViewer* viewer);

signals:
    void applyResult(const ImageBuffer& result);
    void progressMsg(const QString& msg); // New

protected:
    void closeEvent(class QCloseEvent* event) override;

private slots:
    void onApply();
    void clearPolys();

private:
    void generateModel(ImageBuffer& output); // Modify output in place

    // Refs
    QPointer<ImageViewer> m_viewer;
    ImageBuffer m_originalBuffer; // Owned backup
    bool m_applied = false;
    
    // Controls
    class QSpinBox* m_spinDegree;
    class QSpinBox* m_spinSamples;
    class QSpinBox* m_spinDown;
    class QSpinBox* m_spinPatch; // New
    class QCheckBox* m_checkRBF;
    class QDoubleSpinBox* m_spinSmooth;
    class QCheckBox* m_checkShowBG;
    class QCheckBox* m_checkNormalize; // New
};

#endif // ABEDIALOG_H
