#ifndef WCSMOSAICDIALOG_H
#define WCSMOSAICDIALOG_H

/*
 * WCSMosaicDialog.h
 *
 * Dialog that allows the user to select multiple open images with valid WCS
 * astrometric solutions and stitch them into a single reprojected mosaic.
 * The user designates one image as the reference anchor, which defines the
 * output projection plane and pixel scale. A feathering distance controls
 * the width of the blending transition between overlapping panels.
 *
 * The dialog emits mosaicReady() when the mosaic has been successfully built,
 * carrying the resulting ImageBuffer and a display title for the caller to
 * handle (e.g. open in a new viewer window).
 */

#include "DialogBase.h"
#include "../ImageViewer.h"

#include <QVector>

class QListWidget;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;


class WCSMosaicDialog : public DialogBase
{
    Q_OBJECT

public:
    /*
     * Constructs the dialog. The parent widget follows the standard Qt
     * ownership model; passing a valid parent ensures proper lifetime
     * management and correct modal behaviour.
     */
    explicit WCSMosaicDialog(QWidget* parent = nullptr);

    /*
     * Destructor. All child widgets are owned by Qt's object tree and are
     * released automatically; dynamically allocated blend masks are freed
     * inside buildMosaic() before it returns.
     */
    ~WCSMosaicDialog() override;

    /*
     * Sets the currently active viewer so that it can be pre-selected in the
     * image list when the dialog is populated.
     */
    void setViewer(ImageViewer* v);

    /*
     * Populates the image list with every viewer in 'images' that holds a
     * valid image buffer and a valid WCS astrometric solution. Viewers that
     * fail either check are silently skipped. Call this after setViewer() so
     * the active viewer is correctly pre-selected.
     */
    void setAvailableImages(const QVector<ImageViewer*>& images);

signals:
    /*
     * Emitted after a successful mosaic build. 'buffer' contains the
     * reprojected, blended result; 'title' is a suggested display name for
     * the new viewer window.
     */
    void mosaicReady(const ImageBuffer& buffer, const QString& title);

private slots:
    /*
     * Triggered when the user clicks "Apply". Validates the selection,
     * delegates to buildMosaic(), and either reports an error or emits
     * mosaicReady() with the result.
     */
    void onApply();

    /*
     * Synchronises the reference-anchor combo box with the current list
     * selection and enables or disables the Apply button based on whether
     * at least two images are selected.
     */
    void updateSelection();

private:
    /*
     * Creates and arranges all child widgets. Called once from the
     * constructor.
     */
    void setupUI();

    /*
     * Core computation routine. Projects every selected panel into the
     * coordinate frame of the anchor image using the WCS solution of each
     * panel, blends overlapping regions with feathered weight maps via
     * bilinear interpolation, and writes the normalised result into
     * 'outputBuffer'. Returns true on success; on failure returns false and
     * sets a human-readable message in 'errorMsg'.
     */
    bool buildMosaic(ImageBuffer& outputBuffer, QString& errorMsg);

    // --- State ---

    /* Currently active viewer; used only for pre-selection in the list. */
    ImageViewer* m_activeViewer = nullptr;

    /* Full list of candidate viewers supplied by the caller. */
    QVector<ImageViewer*> m_availableImages;

    // --- Widgets ---

    /* Multi-selection list showing the title of each eligible input image. */
    QListWidget* m_imageList;

    /* Drop-down populated with the currently selected images; the chosen
     * entry defines the reference anchor for the output projection. */
    QComboBox* m_referenceCombo;

    /* Feathering distance in pixels applied when generating blend masks. */
    QSpinBox* m_featherSpin;

    /* One-line status indicator showing the current selection count. */
    QLabel* m_statusLabel;

    /* Initiates the mosaic build when clicked. */
    QPushButton* m_btnApply;

    /* Closes the dialog without producing output. */
    QPushButton* m_btnCancel;
};

#endif // WCSMOSAICDIALOG_H