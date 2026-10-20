/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

#include "ApplyMaskDialog.h"

#include <QGroupBox>
#include <QImage>
#include <QPixmap>
#include <QPushButton>

#include <algorithm>

// =============================================================================
// Constructor
// =============================================================================

ApplyMaskDialog::ApplyMaskDialog(int targetWidth, int targetHeight, QWidget* parent)
    : DialogBase(parent, tr("Apply Mask"), 450, 400)
    , m_targetWidth(targetWidth)
    , m_targetHeight(targetHeight)
{
    resize(600, 400);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // -------------------------------------------------------------------------
    // Left panel: mask selection list
    // -------------------------------------------------------------------------
    QVBoxLayout* listLayout = new QVBoxLayout();
    listLayout->addWidget(new QLabel(tr("Available Masks:")));

    m_listWidget = new QListWidget();
    connect(m_listWidget, &QListWidget::itemSelectionChanged,
            this, &ApplyMaskDialog::onSelectionChanged);
    listLayout->addWidget(m_listWidget);

    mainLayout->addLayout(listLayout, 1);

    // -------------------------------------------------------------------------
    // Right panel: preview and action buttons
    // -------------------------------------------------------------------------
    QVBoxLayout* previewLayout = new QVBoxLayout();

    QGroupBox*   previewGroup  = new QGroupBox(tr("Preview"));
    QVBoxLayout* groupLayout   = new QVBoxLayout(previewGroup);

    m_previewLabel = new QLabel(tr("Select a mask to preview"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(256, 256);
    m_previewLabel->setStyleSheet(
        "background-color: #1a1a1a; border: 1px solid #444;");
    groupLayout->addWidget(m_previewLabel);

    previewLayout->addWidget(previewGroup, 1);

    QHBoxLayout* btnLayout  = new QHBoxLayout();
    QPushButton* cancelBtn  = new QPushButton(tr("Cancel"));
    QPushButton* okBtn      = new QPushButton(tr("OK"));
    okBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);

    previewLayout->addLayout(btnLayout);

    mainLayout->addLayout(previewLayout, 1);

    // Centre the dialog over its parent window if one is present.
    if (parentWidget())
        move(parentWidget()->window()->geometry().center() - rect().center());
}

// =============================================================================
// Public interface
// =============================================================================

void ApplyMaskDialog::addAvailableMask(const QString& name,
                                       const MaskLayer& mask,
                                       bool isView)
{
    const QString displayName = isView
        ? tr("[View] %1").arg(name)
        : tr("[Saved] %1").arg(name);

    m_availableMasks[displayName] = mask;
    m_listWidget->addItem(displayName);
}

MaskLayer ApplyMaskDialog::getSelectedMask() const
{
    return m_selectedMask;
}

// =============================================================================
// Private slots
// =============================================================================

void ApplyMaskDialog::onSelectionChanged()
{
    QListWidgetItem* item = m_listWidget->currentItem();
    if (!item)
        return;

    const QString name = item->text();

    if (m_availableMasks.contains(name))
    {
        m_selectedMask = m_availableMasks[name];
        updatePreview(m_selectedMask);
    }
}

// =============================================================================
// Private helpers
// =============================================================================

void ApplyMaskDialog::updatePreview(const MaskLayer& mask)
{
    if (!mask.isValid())
    {
        m_previewLabel->setText(tr("Invalid mask data"));
        return;
    }

    const int w = mask.width;
    const int h = mask.height;

    // Convert the floating-point mask data to an 8-bit grayscale QImage.
    QImage img(w, h, QImage::Format_Grayscale8);

    for (int y = 0; y < h; ++y)
    {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const float val = mask.data[y * w + x];
            line[x] = static_cast<uchar>(
                std::clamp(val * 255.0f, 0.0f, 255.0f));
        }
    }

    const QPixmap pix = QPixmap::fromImage(img);
    m_previewLabel->setPixmap(
        pix.scaled(m_previewLabel->size(),
                   Qt::KeepAspectRatio,
                   Qt::SmoothTransformation));
}