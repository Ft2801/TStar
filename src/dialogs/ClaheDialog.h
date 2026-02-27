#ifndef CLAHEDIALOG_H
#define CLAHEDIALOG_H

#include "DialogBase.h"
#include <QSlider>
#include <QCheckBox>
#include <QTimer>
#include <QImage>
#include "../ImageBuffer.h"

class MainWindowCallbacks;
class QLabel;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;
class MainWindowCallbacks;

class ClaheDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ClaheDialog(QWidget* parent = nullptr);
    ~ClaheDialog();

    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onApply();
    void onReset();
    void updatePreview();

private:
    void setupUi();
    void createPreview(const ImageBuffer& src, float clipLimit, int tileGridSize);
    
    MainWindowCallbacks* m_mainWindow;
    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;
    
    // UI Elements
    QSlider*    m_clipSlider;
    QLabel*     m_clipLabel;

    QSlider*    m_tileSlider;
    QLabel*     m_tileLabel;

    QSlider*    m_opacitySlider = nullptr;
    QLabel*     m_opacityLabel  = nullptr;

    QCheckBox*  m_chkPreview;
    QTimer*     m_previewTimer;

    QGraphicsView*       m_view;
    QGraphicsScene*      m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    float                m_zoom = 1.0f;
    bool                 m_firstDisplay = true; // fit-to-view only on first render

    bool m_previewDirty;
};

#endif // CLAHEDIALOG_H
