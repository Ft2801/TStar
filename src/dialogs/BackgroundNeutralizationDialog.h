#pragma once

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QRect>
#include "../ImageBuffer.h"

class MainWindow;
class ImageViewer;

/**
 * @brief Background Neutralization Dialog
 * Simplified UI: User selects a reference region directly on the main image viewer.
 */
#include <QPointer>
class BackgroundNeutralizationDialog : public QDialog {
    Q_OBJECT
public:
    explicit BackgroundNeutralizationDialog(MainWindow* parent);
    ~BackgroundNeutralizationDialog();

    static void neutralizeBackground(ImageBuffer& img, const QRect& rect);
    
    void setInteractionEnabled(bool enabled);
    void setViewer(ImageViewer* viewer);

signals:
    void apply(const QRect& rect);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onApply();
    void onRectSelected(const QRectF& r);

private:
    void setupUI();
    void setSelectionMode(bool active);

    MainWindow* m_mainWin;
    QPointer<ImageViewer> m_activeViewer;
    QRect m_selection;
    bool m_hasSelection = false;
    bool m_interactionEnabled = false;
    
    QLabel* m_statusLabel;
    QPushButton* m_btnApply;
    QPushButton* m_btnCancel;
};
