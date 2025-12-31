#ifndef CHANNEL_COMBINATION_DIALOG_H
#define CHANNEL_COMBINATION_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <vector>
#include "../ImageBuffer.h"

class ChannelCombinationDialog : public QDialog {
    Q_OBJECT
public:
    struct ChannelSource {
        QString name;
        ImageBuffer buffer;
    };

    explicit ChannelCombinationDialog(const std::vector<ChannelSource>& availableSources, QWidget* parent = nullptr);
    ImageBuffer getResult() const { return m_result; }

private slots:
    void onApply();
    void onCancel();

private:
    QComboBox* m_comboR;
    QComboBox* m_comboG;
    QComboBox* m_comboB;
    
    std::vector<ChannelSource> m_sources;
    ImageBuffer m_result;
};

#endif // CHANNEL_COMBINATION_DIALOG_H
