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

#ifndef APPLYMASKDIALOG_H
#define APPLYMASKDIALOG_H

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

#include "DialogBase.h"
#include "core/MaskLayer.h"

/**
 * @brief Dialog for selecting and previewing a mask to apply to the active image.
 *
 * Presents a list of available masks sourced from open viewer windows and
 * from the saved mask store, together with a scaled grayscale preview of
 * the currently selected entry. The caller retrieves the confirmed selection
 * via getSelectedMask() after the dialog is accepted.
 */
class ApplyMaskDialog : public DialogBase
{
    Q_OBJECT

public:
    ApplyMaskDialog(int targetWidth, int targetHeight, QWidget* parent = nullptr);

    /**
     * @brief Registers a mask for display in the selection list.
     * @param name    Display name for the mask entry.
     * @param mask    The mask data.
     * @param isView  True if the mask originates from an open viewer window.
     */
    void addAvailableMask(const QString& name, const MaskLayer& mask, bool isView = false);

    /** Returns the mask corresponding to the confirmed selection. */
    MaskLayer getSelectedMask() const;

private slots:
    void onSelectionChanged();

private:
    /** Renders the mask data into the preview label as a scaled grayscale image. */
    void updatePreview(const MaskLayer& mask);

    // --- Image dimensions for which the mask must be compatible -----------
    int m_targetWidth;
    int m_targetHeight;

    // --- UI controls ------------------------------------------------------
    QListWidget* m_listWidget;
    QLabel*      m_previewLabel;

    // --- Data storage -----------------------------------------------------
    QMap<QString, MaskLayer> m_availableMasks;
    MaskLayer                m_selectedMask;
};

#endif // APPLYMASKDIALOG_H