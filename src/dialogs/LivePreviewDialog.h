#ifndef LIVEPREVIEWDIALOG_H
#define LIVEPREVIEWDIALOG_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "../ImageBuffer.h"
#include <QPixmap>
#include <vector>

#include "DialogBase.h"

class LivePreviewDialog : public DialogBase {
    Q_OBJECT
public:
    explicit LivePreviewDialog(int width, int height, QWidget* parent = nullptr);
    void updateMask(const std::vector<float>& maskData, int width, int height, 
                    ImageBuffer::DisplayMode mode = ImageBuffer::Display_Linear, 
                    bool inverted = false, bool falseColor = false);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onZoomIn();
    void onZoomOut();
    void onFit();

private:
    void setZoom(float z);

    QGraphicsView*        m_view    = nullptr;
    QGraphicsScene*       m_scene   = nullptr;
    QGraphicsPixmapItem*  m_pixItem = nullptr;
    float                 m_zoom    = 1.0f;
    bool                  m_firstUpdate = true; // fit-to-view only on first mask render
    int m_targetWidth;
    int m_targetHeight;
};

#endif // LIVEPREVIEWDIALOG_H
