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

#ifndef NBTORGB_STARS_DIALOG_H
#define NBTORGB_STARS_DIALOG_H

#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

class ImageViewer;
class MainWindowCallbacks;

// =============================================================================
// NBtoRGBStarsDialog
//
// Combines narrowband (Ha, OIII, optional SII) and/or OSC star layers into
// a colour-corrected RGB star image. Provides controls for the Ha:OIII blend
// ratio, star stretch factor, and saturation before pushing to a new view.
// =============================================================================
class NBtoRGBStarsDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit NBtoRGBStarsDialog(QWidget* parent = nullptr);
    ~NBtoRGBStarsDialog();

    void setViewer(ImageViewer* v);

private slots:
    void onLoadChannel(const QString& which);
    void onPreviewCombine();
    void onPushFinal();
    void onClear();
    void onRatioChanged(int v);
    void onStretchChanged(int v);
    void onSatChanged(int v);

private:
    void    buildUI();
    void    loadFromViewer(const QString& which);
    void    loadFromFile  (const QString& which);
    void    setStatusLabel(const QString& which, const QString& text);
    QPixmap floatToPixmap (const std::vector<float>& img, int w, int h, int ch);

    // --- Viewer ---
    ImageViewer*         m_viewer     = nullptr;
    MainWindowCallbacks* m_mainWindow = nullptr;

    // --- Raw channels (mono float32 [0,1]) ---
    std::vector<float> m_ha, m_oiii, m_sii;
    std::vector<float> m_osc;      // RGB interleaved when ch==3, mono otherwise
    int m_oscChannels = 0;
    int m_chW = 0, m_chH = 0;

    // --- Result (RGB interleaved, size = w*h*3) ---
    std::vector<float>    m_result;
    ImageBuffer::Metadata m_srcMeta;
    bool                  m_hasSrcMeta = false;

    // --- Load buttons and status labels ---
    QPushButton* m_btnHa   = nullptr;
    QPushButton* m_btnOIII = nullptr;
    QPushButton* m_btnSII  = nullptr;
    QPushButton* m_btnOSC  = nullptr;
    QLabel*      m_lblHa   = nullptr;
    QLabel*      m_lblOIII = nullptr;
    QLabel*      m_lblSII  = nullptr;
    QLabel*      m_lblOSC  = nullptr;

    // --- Parameter controls ---
    QLabel*    m_lblRatio       = nullptr;
    QSlider*   m_sldRatio       = nullptr;
    QCheckBox* m_chkStarStretch = nullptr;
    QLabel*    m_lblStretch     = nullptr;
    QSlider*   m_sldStretch     = nullptr;
    QLabel*    m_lblSat         = nullptr;
    QSlider*   m_sldSat         = nullptr;

    // --- Action buttons ---
    QPushButton* m_btnPreview = nullptr;
    QPushButton* m_btnPush    = nullptr;
    QPushButton* m_btnClear   = nullptr;

    // --- Preview ---
    QGraphicsView*       m_view    = nullptr;
    QGraphicsScene*      m_scene   = nullptr;
    QGraphicsPixmapItem* m_pixBase = nullptr;
    QLabel*              m_status  = nullptr;
};

#endif // NBTORGB_STARS_DIALOG_H