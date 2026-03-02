#ifndef REFERENCEALIGNDIALOG_H
#define REFERENCEALIGNDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include <QImage>
#include <QPixmap>

class QLabel;
class QPushButton;

class ReferenceAlignDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ReferenceAlignDialog(QWidget* parent, const ImageBuffer& refBuffer);
    ~ReferenceAlignDialog();

    // Retrieves the final transformed buffer
    ImageBuffer getAlignedBuffer() const;

private slots:
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotateCW();
    void onRotateCCW();
    void updatePreview();

private:
    void applyTransformationToBuffer(int flipCode, bool transpose);

    ImageBuffer m_currentBuffer;
    QLabel* m_previewLabel;
    
    // Quick preview pixmap generation
    QImage bufferToQImage(const ImageBuffer& buf);
};

#endif // REFERENCEALIGNDIALOG_H
