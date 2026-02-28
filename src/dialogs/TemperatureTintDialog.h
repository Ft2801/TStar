#ifndef TEMPERATURETINTDIALOG_H
#define TEMPERATURETINTDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include <QPointer>
#include "../ImageViewer.h"
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

class TemperatureTintDialog : public DialogBase {
    Q_OBJECT
public:
    explicit TemperatureTintDialog(QWidget* parent, ImageViewer* viewer);
    ~TemperatureTintDialog();

    void setViewer(ImageViewer* viewer);
    void triggerPreview();

    struct State {
        int temperature; // -100 to +100
        int tint;        // -100 to +100
    };
    State getState() const;
    void setState(const State& s);
    void resetState();

signals:
    void applyInternal();

private slots:
    void onSliderChanged();
    void handleApply();

private:
    QPointer<ImageViewer> m_viewer;
    ImageBuffer* m_buffer;
    ImageBuffer m_originalBuffer;
    bool m_applied = false;

    QSlider* m_sldTemperature;
    QSlider* m_sldTint;

    QLabel* m_valTemperature;
    QLabel* m_valTint;

    QCheckBox* m_chkPreview;

    // Compute RGB gain factors from current slider values
    void computeGain(float& r, float& g, float& b) const;

    void setupUI();
};

#endif // TEMPERATURETINTDIALOG_H
