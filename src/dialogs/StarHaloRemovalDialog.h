#ifndef STARHALOREMOVALDIALOG_H
#define STARHALOREMOVALDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QImage>

class MainWindowCallbacks;
class QLabel;
class QCheckBox;
class QComboBox;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QSlider;
class QTimer;

class StarHaloRemovalDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StarHaloRemovalDialog(QWidget* parent = nullptr);
    ~StarHaloRemovalDialog();

    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onApply();
    void onReset();
    void updatePreview();
    void zoomIn();
    void zoomOut();
    void zoomFit();

private:
    void setupUi();
    void applyHaloRemoval(const ImageBuffer& src, int reductionLevel, bool isLinear, ImageBuffer& dst) const;
    void updateReductionLabel(int level);

    MainWindowCallbacks* m_mainWindow = nullptr;
    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;

    QSlider* m_reductionSlider = nullptr;
    QLabel* m_reductionLabel = nullptr;
    QCheckBox* m_linearCheck = nullptr;
    QComboBox* m_applyTargetCombo = nullptr;
    QCheckBox* m_previewCheck = nullptr;
    QTimer* m_previewTimer = nullptr;

    QGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
    float m_zoom = 1.0f;
    bool m_firstDisplay = true;
};

#endif // STARHALOREMOVALDIALOG_H
