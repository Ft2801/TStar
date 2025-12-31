#ifndef PERFECTPALETTEDIALOG_H
#define PERFECTPALETTEDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollArea>
#include "../ImageBuffer.h"
#include "../algos/PerfectPaletteRunner.h"
#include <QPointer>

class MainWindow;
class ImageViewer;

class PerfectPaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit PerfectPaletteDialog(MainWindow* mainWin, QWidget* parent = nullptr);
    void setViewer(ImageViewer* v);

private slots:
    void onLoadChannel(const QString& channel);
    void onCreatePalettes();
    void onPaletteSelected(const QString& name);
    void onApply();
    void onIntensityChanged();

private:
    void createUI();
    void updateThumbnails();
    
    MainWindow* m_mainWin;
    QPointer<ImageViewer> m_viewer;
    PerfectPaletteRunner m_runner;
    
    // Loaded Channels
    ImageBuffer m_ha, m_oiii, m_sii;
    // Downscaled versions for fast preview
    ImageBuffer m_previewHa, m_previewOiii, m_previewSii; 
    QLabel *m_lblHa, *m_lblOiii, *m_lblSii;
    
    // UI Elements
    QScrollArea* m_scrollArea;
    QGridLayout* m_gridPalettes;
    QLabel* m_lblPreview;
    
    QSlider *m_sliderHa, *m_sliderOiii, *m_sliderSii;
    QLabel *m_lblValHa, *m_lblValOiii, *m_lblValSii;
    
    QString m_selectedPalette;
    
    struct PaletteThumb {
        QPushButton* btn;
        QString name;
    };
    QList<PaletteThumb> m_thumbs;
};

#endif // PERFECTPALETTEDIALOG_H
