#ifndef CONVERSION_DIALOG_H
#define CONVERSION_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QLabel>

class ConversionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConversionDialog(QWidget* parent = nullptr);
    
private slots:
    void onAddFiles();
    void onRemoveFiles();
    void onClearList();
    void onBrowseOutput();
    void onConvert();
    void updateStatus();
    
private:
    void setupUI();
    
    QListWidget* m_fileList;
    QLineEdit* m_outputDir;
    QPushButton* m_browseBtn;
    QComboBox* m_outputFormat;
    QComboBox* m_bitDepth;
    QCheckBox* m_debayerCheck;
    QProgressBar* m_progress;
    QLabel* m_statusLabel;
    
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_convertBtn;
    QPushButton* m_closeBtn;
};

#endif // CONVERSION_DIALOG_H
