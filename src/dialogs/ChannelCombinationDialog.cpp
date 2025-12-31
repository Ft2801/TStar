#include "ChannelCombinationDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include "../algos/ChannelOps.h"
#include <QIcon>

ChannelCombinationDialog::ChannelCombinationDialog(const std::vector<ChannelSource>& availableSources, QWidget* parent)
    : QDialog(parent), m_sources(availableSources)
{
    setWindowTitle(tr("Channel Combination"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    resize(400, 200);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    auto addChannelRow = [&](const QString& label, QComboBox** combo) {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        *combo = new QComboBox();
        (*combo)->addItem(tr("None"), -1);
        for (size_t i = 0; i < m_sources.size(); ++i) {
            (*combo)->addItem(m_sources[i].name, (int)i);
        }
        row->addWidget(*combo);
        mainLayout->addLayout(row);
    };

    addChannelRow(tr("Red:"), &m_comboR);
    addChannelRow(tr("Green:"), &m_comboG);
    addChannelRow(tr("Blue:"), &m_comboB);

    // Auto-select if names match?
    // E.g. if we find "R", "G", "B" or "_R", "_G", "_B" suffix
    // Simple heuristic: 
    for(size_t i=0; i<m_sources.size(); ++i) {
        QString name = m_sources[i].name;
        if (name.contains("_R") || name.endsWith(" R")) m_comboR->setCurrentIndex(i + 1);
        if (name.contains("_G") || name.endsWith(" G")) m_comboG->setCurrentIndex(i + 1);
        if (name.contains("_B") || name.endsWith(" B")) m_comboB->setCurrentIndex(i + 1);
    }

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnOk = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    connect(btnOk, &QPushButton::clicked, this, &ChannelCombinationDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &ChannelCombinationDialog::onCancel);
}

void ChannelCombinationDialog::onApply() {
    int idxR = m_comboR->currentData().toInt();
    int idxG = m_comboG->currentData().toInt();
    int idxB = m_comboB->currentData().toInt();

    if (idxR < 0 || idxG < 0 || idxB < 0) {
        QMessageBox::warning(this, tr("Incomplete"), tr("Please select a source for all R, G, B channels."));
        return;
    }

    ImageBuffer bufR = m_sources[idxR].buffer;
    ImageBuffer bufG = m_sources[idxG].buffer;
    ImageBuffer bufB = m_sources[idxB].buffer;

    // Validate dimensions
    if (bufR.width() != bufG.width() || bufR.height() != bufG.height() || 
        bufR.width() != bufB.width() || bufR.height() != bufB.height()) {
        QMessageBox::warning(this, tr("Error"), tr("Selected images must have the same dimensions."));
        return;
    }

    m_result = ChannelOps::combineChannels(bufR, bufG, bufB);
    if (!m_result.isValid()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to combine channels."));
        return;
    }

    accept();
}

void ChannelCombinationDialog::onCancel() {
    reject();
}
