#ifndef CURVESDIALOG_H
#define CURVESDIALOG_H

#include "DialogBase.h"
#include <vector>
#include "algos/CubicSpline.h"

#include <QPointer>
#include "../ImageViewer.h"

class ImageBuffer; 
class QComboBox;
class QCheckBox;
class QLabel;
class QToolButton;

// Interactive Graph Widget
class CurvesGraph : public QWidget {
    Q_OBJECT
public:
    explicit CurvesGraph(QWidget* parent = nullptr);
    
    void setHistogram(const std::vector<std::vector<int>>& hist);
    void setChannelMode(int mode); // 0=RGB/K, 1=R, 2=G, 3=B
    
    std::vector<SplinePoint> getPoints() const { return m_points; }
    void setPoints(const std::vector<SplinePoint>& pts);
    void reset();

    SplineData getSpline() const;

    void setLogScale(bool enabled);
    void setGridVisible(bool visible);

signals:
    void curvesChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void sortPoints();
    void updateResampledBins();

    std::vector<std::vector<int>> m_hist;
    std::vector<SplinePoint> m_points;
    
    // Caching
    std::vector<std::vector<float>> m_resampledBins;
    std::vector<float> m_channelMaxVals;
    int m_lastW = 0;

    int m_channelMode = 0; 
    bool m_logScale = false; 
    bool m_showGrid = true;
    
    int m_dragIdx = -1;
    int m_hoverIdx = -1;
};

class CurvesDialog : public DialogBase {
    Q_OBJECT
public:
    explicit CurvesDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~CurvesDialog();

    // Returns true if applied
    bool applied() const { return m_applied; }

    struct State {
        std::vector<SplinePoint> points;
        bool logScale;
        bool showGrid;
        bool ch[3];
        bool preview;
    };
    State getState() const;
    void setState(const State& s);
    void resetState();

    void setInputHistogram(const std::vector<std::vector<int>>& hist); 
    void setViewer(ImageViewer* viewer); // Renamed from setImageBuffer
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void previewRequested(const std::vector<std::vector<float>>& lut);
    void applyRequested(const SplineData& spline, const bool channels[3]);

private slots:
    void onChannelToggled(); 
    void onReset();
    void onApply();
    void onPreviewToggled(bool checked);
    void onLogToggled(bool checked);
    void onGridToggled(bool checked);
    void onCurvesChanged(bool isFinal); 

private:
    QPointer<CurvesGraph> m_graph;
    QCheckBox* m_previewCheck;
    QToolButton* m_logBtn;
    QToolButton* m_gridBtn;
    QLabel* m_statsLabel;
    
    class QToolButton* m_redBtn;
    class QToolButton* m_greenBtn;
    class QToolButton* m_blueBtn;
    
    QPointer<ImageViewer> m_viewer; 
    bool m_applied = false;
    
    std::vector<std::vector<int>> m_origHist; 
    std::vector<std::vector<int>> m_uiHist; // Downsampled to 1024 bins for real-time UI
};


#endif // CURVESDIALOG_H
