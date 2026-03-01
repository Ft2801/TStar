#ifndef NBTORGB_STARS_DIALOG_H
#define NBTORGB_STARS_DIALOG_H

#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

class ImageViewer;
class MainWindowCallbacks;

// ============================================================================
// NB → RGB Stars Dialog
// ============================================================================
class NBtoRGBStarsDialog : public DialogBase {
    Q_OBJECT
public:
    explicit NBtoRGBStarsDialog(QWidget* parent = nullptr);
    ~NBtoRGBStarsDialog();

    void setViewer(ImageViewer* v);

private slots:
    void onLoadChannel(const QString& which);
    void onPreviewCombine();
    void onPushFinal();
    void onClear();
    void onRatioChanged(int v);
    void onStretchChanged(int v);
    void onSatChanged(int v);

private:
    void buildUI();
    void loadFromViewer(const QString& which);
    void loadFromFile(const QString& which);
    void setStatusLabel(const QString& which, const QString& text);
    QPixmap floatToPixmap(const std::vector<float>& img, int w, int h, int ch);

    // Viewer
    ImageViewer* m_viewer = nullptr;
    MainWindowCallbacks* m_mainWindow = nullptr;

    // Raw channels (mono float32 [0,1])
    std::vector<float> m_ha, m_oiii, m_sii;
    std::vector<float> m_osc;  // RGB interleaved if present
    int m_oscChannels = 0;
    int m_chW = 0, m_chH = 0;

    // Result
    std::vector<float> m_result; // RGB interleaved
    ImageBuffer::Metadata m_srcMeta; // Store metadata
    bool m_hasSrcMeta = false;

    // --- UI Widgets ---
    // Load buttons + labels
    QPushButton* m_btnHa = nullptr;
    QPushButton* m_btnOIII = nullptr;
    QPushButton* m_btnSII = nullptr;
    QPushButton* m_btnOSC = nullptr;

    QLabel* m_lblHa = nullptr;
    QLabel* m_lblOIII = nullptr;
    QLabel* m_lblSII = nullptr;
    QLabel* m_lblOSC = nullptr;

    // Controls
    QLabel* m_lblRatio = nullptr;
    QSlider* m_sldRatio = nullptr;

    QCheckBox* m_chkStarStretch = nullptr;
    QLabel* m_lblStretch = nullptr;
    QSlider* m_sldStretch = nullptr;

    QLabel* m_lblSat = nullptr;
    QSlider* m_sldSat = nullptr;

    // Actions
    QPushButton* m_btnPreview = nullptr;
    QPushButton* m_btnPush = nullptr;
    QPushButton* m_btnClear = nullptr;

    // Preview
    QGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixBase = nullptr;
    QLabel* m_status = nullptr;
};

#endif // NBTORGB_STARS_DIALOG_H
