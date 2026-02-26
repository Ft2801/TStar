#ifndef HISTOGRAMWIDGET_H
#define HISTOGRAMWIDGET_H

#include <QWidget>
#include <vector>

class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr);
    
    void setData(const std::vector<std::vector<int>>& bins, int channels);
    void setGhostData(const std::vector<std::vector<int>>& bins, int channels);
    void setLogScale(bool enabled);
    void setZoom(float h, float v);
    void setShowGrid(bool show);
    void setShowCurve(bool show);
    void setTransformCurve(const std::vector<float>& lut);
    void clear();

protected:
    void paintEvent(class QPaintEvent *event) override;

private:
    std::vector<std::vector<int>> m_bins;
    std::vector<std::vector<int>> m_ghostBins;
    
    int m_channels = 0;
    int m_ghostChannels = 0;
    bool m_logScale = false;
    float m_zoomH = 1.0f;
    float m_zoomV = 1.0f;
    bool m_showGrid = true;
    bool m_showCurve = true;
    std::vector<float> m_lut; // Transform curve for overlay
    
    // Cache for computed histograms to avoid recomputation during fast window drags
    std::vector<std::vector<float>> m_cachedDisplayBins;
    std::vector<std::vector<float>> m_cachedDisplayGhostBins;
    double m_cachedMaxVal = 0.0;
    double m_cachedGhostMaxVal = 0.0;
    int m_cachedWidth = -1; // Invalidates cache when width changes
};

#endif // HISTOGRAMWIDGET_H
