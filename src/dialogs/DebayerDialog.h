#ifndef DEBAYER_DIALOG_H
#define DEBAYER_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>

class ImageViewer;
class MainWindow;

class DebayerDialog : public QDialog {
    Q_OBJECT
public:
    explicit DebayerDialog(QWidget* parent = nullptr);

    void setViewer(ImageViewer* v);

private slots:
    void onApply();
    void onPatternChanged(int index);

private:
    void updatePatternLabel();
    QString detectPatternFromHeader();
    QString autoDetectByScoring();

    ImageViewer* m_viewer = nullptr;
    QComboBox* m_patternCombo;
    QComboBox* m_methodCombo;
    QLabel* m_detectedLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progress;
    QPushButton* m_applyBtn;
};

#endif // DEBAYER_DIALOG_H
