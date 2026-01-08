#ifndef APPLYMASKDIALOG_H
#define APPLYMASKDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "core/MaskLayer.h"

#include "DialogBase.h"

class ApplyMaskDialog : public DialogBase {
    Q_OBJECT
public:
    ApplyMaskDialog(int targetWidth, int targetHeight, QWidget* parent = nullptr);
    
    void addAvailableMask(const QString& name, const MaskLayer& mask, bool isView = false);
    MaskLayer getSelectedMask() const;

private slots:
    void onSelectionChanged();

private:
    void updatePreview(const MaskLayer& mask);

    int m_targetWidth;
    int m_targetHeight;
    QListWidget* m_listWidget;
    QLabel* m_previewLabel;
    QMap<QString, MaskLayer> m_availableMasks;
    MaskLayer m_selectedMask;
};

#endif // APPLYMASKDIALOG_H
