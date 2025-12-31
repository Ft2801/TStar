#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QPointer>
#include "../ImageBuffer.h"
#include "../ImageViewer.h"

class MainWindow;

class PixelMathDialog : public QDialog {
    Q_OBJECT
public:
    explicit PixelMathDialog(MainWindow* parent, ImageViewer* viewer);
    ~PixelMathDialog();
    
    void setViewer(ImageViewer* viewer);
    
    // Static helper to evaluate on buffer. 
    // If rescale is true, it maps the resulting [min, max] to [0, 1].
    static bool evaluateExpression(const QString& expr, ImageBuffer& buf, bool rescale = false, QString* errorMsg = nullptr);

signals:
    void apply(const QString& expression, bool rescale);

private slots:
    void onApply();

private:
    void setupUI();

    MainWindow* m_mainWin;
    QPointer<ImageViewer> m_viewer;
    
    QPlainTextEdit* m_exprEdit;
    QCheckBox* m_checkRescale;
    QPushButton* m_btnApply;
    QPushButton* m_btnCancel;
    QLabel* m_statusLabel;
};
