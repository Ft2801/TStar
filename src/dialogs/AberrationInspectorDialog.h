#ifndef ABERRATIONINSPECTORDIALOG_H
#define ABERRATIONINSPECTORDIALOG_H

#include <QDialog>
#include <vector>
#include "../ImageBuffer.h"

class QGraphicsScene;
class QGraphicsView;
class QSpinBox;
class QLabel;

class AberrationInspectorDialog : public QDialog {
    Q_OBJECT
public:
    explicit AberrationInspectorDialog(const ImageBuffer& img, QWidget* parent = nullptr);
    ~AberrationInspectorDialog();
    
    void setSource(const ImageBuffer& img);

private slots:
    void updatePanels();

private:
    void setupUi();
    ImageBuffer cropPanel(int x, int y, int w, int h);
    
    // 3x3 Grid of Views
    struct Panel {
        QGraphicsScene* scene;
        QGraphicsView* view;
    };
    std::vector<Panel> m_panels; // 9 panels
    
    ImageBuffer m_source;
    
    // Controls
    QSpinBox* m_sizeSpin;
    int m_panelSize;
};

#endif // ABERRATIONINSPECTORDIALOG_H
