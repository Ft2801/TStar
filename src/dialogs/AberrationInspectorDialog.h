#ifndef ABERRATIONINSPECTORDIALOG_H
#define ABERRATIONINSPECTORDIALOG_H

#include <array>
#include "../ImageBuffer.h"
#include "DialogBase.h"

class QLabel;

class AberrationInspectorDialog : public DialogBase {
    Q_OBJECT
public:
    explicit AberrationInspectorDialog(const ImageBuffer& img, QWidget* parent = nullptr);

    void setSource(const ImageBuffer& img);

private:
    void setupUi();
    void updatePanels();

    // Returns a QImage cropped (size x size) centred on (cx, cy), clamped to bounds.
    QImage cropToQImage(int cx, int cy, int size);

    ImageBuffer m_source;
    std::array<QLabel*, 9> m_panels; // TL TC TR / ML MC MR / BL BC BR
};

#endif // ABERRATIONINSPECTORDIALOG_H
