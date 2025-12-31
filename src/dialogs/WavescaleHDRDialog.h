#ifndef WAVESCALEHDRDIALOG_H
#define WAVESCALEHDRDIALOG_H

#include <QDialog>
#include <QThread>
#include <vector>
#include <QTimer> // Added for QTimer
#include <QGroupBox> // Added for QGroupBox
#include "../ImageBuffer.h"
#include <QPointer>

class ImageViewer;
class QSlider;
class QLabel;
class QCheckBox;
class QPushButton;
class QProgressBar;

class WavescaleHDRWorker : public QThread {
    Q_OBJECT
public:
    WavescaleHDRWorker(QObject* parent = nullptr);
    void setup(const ImageBuffer& src, int scales, float compression, float maskGamma, float dimmingGamma); // Updated signature
    void run() override;

signals:
    void progress(int pct);
    void finished(ImageBuffer result, ImageBuffer mask);

private:
    ImageBuffer m_src;
    int m_scales;
    float m_compression;
    float m_maskGamma;
    float m_dimmingGamma; // New
};

class WavescaleHDRDialog : public QDialog {
    Q_OBJECT
public:
    explicit WavescaleHDRDialog(QWidget* parent = nullptr, ImageViewer* targetViewer = nullptr);
    ~WavescaleHDRDialog();
    
    void setViewer(ImageViewer* v);
    ImageViewer* viewer() const { return m_targetViewer; }

signals:
    void applyInternal(const ImageBuffer& result);

private slots:
    void onPreviewClicked();
    void onApplyClicked();
    void onWorkerFinished(ImageBuffer result, ImageBuffer mask);
    void toggleOriginal(bool showOriginal);
    void updateMaskPreview(const ImageBuffer& mask);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void createUI();
    void startPreview();
    void updateQuickMask(); // New fast mask update

    ImageViewer* m_viewer; // Internal Preview
    QLabel* m_maskLabel; // Small preview of mask
    
    QSlider* m_scalesSlider;
    QSlider* m_compSlider;
    QSlider* m_gammaSlider;
    
    // NEW: Dimming Gamma Slider (to fix shadow crushing)
    QSlider* m_dimmingSlider;
    QLabel* m_dimmingLabel;
    
    QLabel* m_scalesLabel;
    QLabel* m_compLabel;
    QLabel* m_gammaLabel;
    
    QCheckBox* m_showOriginalCheck;
    QPushButton* m_previewBtn;
    QPushButton* m_applyBtn;
    QPushButton* m_closeBtn;
    QProgressBar* m_progressBar;
    
    // Logic
    QPointer<ImageViewer> m_targetViewer; // Source Image Window (Safe Pointer)
    ImageBuffer m_previewBuffer; // Current processed result
    ImageBuffer m_maskBuffer;    // Current mask
    ImageBuffer m_originalBuffer; 
    
    // Cache for fast mask preview
    std::vector<float> m_L_channel_cache; 
    int m_cacheW = 0;
    int m_cacheH = 0;
    
    WavescaleHDRWorker* m_worker;
    bool m_isPreviewDirty;
    bool m_isFirstShow = true;
};

#endif // WAVESCALESHDRDIALOG_H
