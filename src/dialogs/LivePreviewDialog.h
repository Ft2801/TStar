#ifndef LIVEPREVIEWDIALOG_H
#define LIVEPREVIEWDIALOG_H

#include <QDialog>
#include <QLabel>
#include "../ImageBuffer.h"
#include <QPixmap>
#include <vector>

#include "DialogBase.h"

class LivePreviewDialog : public DialogBase {
    Q_OBJECT
public:
    explicit LivePreviewDialog(int width, int height, QWidget* parent = nullptr);
    void updateMask(const std::vector<float>& maskData, int width, int height, 
                    ImageBuffer::DisplayMode mode = ImageBuffer::Display_Linear, 
                    bool inverted = false, bool falseColor = false);
    
private:
    QLabel* m_label;
    int m_targetWidth;
    int m_targetHeight;
};

#endif // LIVEPREVIEWDIALOG_H
