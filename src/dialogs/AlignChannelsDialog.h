#ifndef ALIGN_CHANNELS_DIALOG_H
#define ALIGN_CHANNELS_DIALOG_H

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include "DialogBase.h"

class ImageViewer;
class MainWindowCallbacks;

/**
 * @brief Dialog for aligning multiple open images to a reference image.
 *
 * Uses the star-based registration engine to compute the geometric transform
 * (rotation, translation, optionally scale) between each target image and
 * the selected reference, then applies the warp in-place with undo support.
 * No brightness or colour changes are performed.
 */
class AlignChannelsDialog : public DialogBase {
    Q_OBJECT
public:
    explicit AlignChannelsDialog(QWidget* parent = nullptr);

    void refreshImageList();

private slots:
    void onApply();

private:
    // Helper – add all open images to a combo box
    void populateCombo(QComboBox* combo);
    // Returns the ImageViewer stored as user-data in a combo, or nullptr
    ImageViewer* viewerFromCombo(QComboBox* combo) const;

    MainWindowCallbacks* m_mainWindow = nullptr;

    // Reference image
    QComboBox* m_refCombo = nullptr;

    // Up to 3 target images (each with enable checkbox + combo)
    struct TargetRow {
        QCheckBox*  check = nullptr;
        QComboBox*  combo = nullptr;
    };
    static constexpr int kMaxTargets = 3;
    TargetRow m_targets[kMaxTargets];

    // Registration parameters
    QCheckBox*       m_allowRotationCheck = nullptr;
    QCheckBox*       m_allowScaleCheck    = nullptr;
    QDoubleSpinBox*  m_thresholdSpin      = nullptr;

    QLabel*      m_statusLabel = nullptr;
    QPushButton* m_applyBtn    = nullptr;
};

#endif // ALIGN_CHANNELS_DIALOG_H
