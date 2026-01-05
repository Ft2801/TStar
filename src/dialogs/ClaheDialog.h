#ifndef CLAHEDIALOG_H
#define CLAHEDIALOG_H

#include <QDialog>
#include <QImage>
#include "../ImageBuffer.h"

class QSlider;
class QLabel;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;
class MainWindow;

class ClaheDialog : public QDialog {
    Q_OBJECT
public:
    explicit ClaheDialog(MainWindow* parent);
    ~ClaheDialog();
    
    void setSource(const ImageBuffer& img);

private slots:
    void onApply();
    void onReset();
    void updatePreview();

private:
    void setupUi();
    void createPreview(const ImageBuffer& src, float clipLimit, int tileGridSize);
    
    MainWindow* m_mainWindow;
    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;
    
    // UI Elements
    QSlider* m_clipSlider;
    QLabel* m_clipLabel;
    
    QSlider* m_tileSlider;
    QLabel* m_tileLabel;
    
    QGraphicsView* m_view;
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    
    bool m_previewDirty;
};

#endif // CLAHEDIALOG_H
