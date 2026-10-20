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

#ifndef CONTINUUM_SUBTRACTION_DIALOG_H
#define CONTINUUM_SUBTRACTION_DIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QVBoxLayout>

class ImageViewer;
class MainWindowCallbacks;

// ============================================================================
// ContinuumSubtractWorker
//
// Executes the full continuum subtraction pipeline on a background thread,
// supporting both starry and starless image pairs per narrowband filter.
//
// Pipeline (matching SAS Pro continuum_subtract.py methodology):
//   BG Neutralization -> Red-to-Green Normalization -> Star-based White Balance
//   -> Linear Subtraction -> Optional non-linear finalization
// ============================================================================
class ContinuumSubtractWorker : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief Describes one narrowband subtraction task (one filter, starry + starless pairs).
     */
    struct Task
    {
        QString     name;          ///< Filter name, e.g. "Ha", "SII", "OIII"
        ImageBuffer nbStarry;      ///< Narrowband starry image
        ImageBuffer contStarry;    ///< Continuum starry image
        ImageBuffer nbStarless;    ///< Narrowband starless image (may be invalid)
        ImageBuffer contStarless;  ///< Continuum starless image (may be invalid)
        bool        starlessOnly = false; ///< True when only the starless pair is loaded
    };

    ContinuumSubtractWorker(const std::vector<Task>&       tasks,
                            const ContinuumSubtractParams& params,
                            bool                           denoiseWithCC,
                            const QString&                 cosmicClarityPath,
                            QObject*                       parent = nullptr);

signals:
    void resultReady(const QString& name, const ImageBuffer& result);
    void statusUpdate(const QString& message);
    void allDone();

protected:
    void run() override;

private:
    std::vector<Task>       m_tasks;
    ContinuumSubtractParams m_params;
    bool                    m_denoiseCC;
    QString                 m_ccPath;
};

// ============================================================================
// ContinuumSubtractionDialog
//
// Multi-image continuum subtraction UI supporting Ha, SII, OIII narrowband
// filters with Red, Green, and OSC continuum sources.  Also handles composite
// HaO3 / S2O3 inputs with automatic channel extraction.
// ============================================================================
class ContinuumSubtractionDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit ContinuumSubtractionDialog(QWidget* parent = nullptr);
    ~ContinuumSubtractionDialog();

    void setViewer(ImageViewer* v);

    /**
     * @brief Refreshes the list of available open images.
     *        Kept as a public slot for MainWindow compatibility.
     */
    void refreshImageList();

private slots:
    void onExecute();
    void onClear();
    void onQFactorChanged(double val);
    void onResultReady(const QString& name, const ImageBuffer& result);
    void onWorkerStatus(const QString& msg);
    void onWorkerDone();

private:
    // --- Image slot ---
    // Represents a single load button / status label pair for one image input.
    struct ImageSlot
    {
        ImageBuffer  buffer;
        QLabel*      label  = nullptr;
        QPushButton* button = nullptr;

        bool loaded() const { return buffer.isValid(); }
        void clear()
        {
            buffer = ImageBuffer();
            if (label) label->setText("---");
        }
    };

    // --- Narrowband filter slots ---
    ImageSlot m_haStarry,    m_haStarless;
    ImageSlot m_siiStarry,   m_siiStarless;
    ImageSlot m_oiiiStarry,  m_oiiiStarless;

    // --- Composite slots (HaO3, S2O3) ---
    ImageSlot m_hao3Starry,  m_hao3Starless;
    ImageSlot m_s2o3Starry,  m_s2o3Starless;

    // --- Continuum source slots ---
    ImageSlot m_redStarry,   m_redStarless;
    ImageSlot m_greenStarry, m_greenStarless;
    ImageSlot m_oscStarry,   m_oscStarless;

    // --- Image loading ---
    void loadImage(const QString& channel);
    void populateCombo(QComboBox* combo);

    /**
     * @brief Resolves the continuum buffer for a given NB filter.
     *        Ha/SII map to Red (or OSC R-channel); OIII maps to Green (or OSC G-channel).
     * @return True if a valid continuum source was found.
     */
    bool getContinuumForFilter(const QString& filter,
                               bool           starless,
                               ImageBuffer&   outCont);

    /**
     * @brief Extracts individual emission channels from a composite HaO3 or S2O3 image.
     *        The OIII channel is averaged with any previously loaded OIII data.
     */
    void extractFromComposite(const QString&     composite,
                              bool               starless,
                              const ImageBuffer& img);

    /**
     * @brief Creates a Load button + status label pair and wires it to loadImage().
     */
    void createSlotUI(QVBoxLayout* layout,
                      ImageSlot&   slot,
                      const QString& label,
                      const QString& channel);

    // --- UI references ---
    MainWindowCallbacks* m_mainWindow = nullptr;
    ImageViewer*         m_viewer     = nullptr;

    QDoubleSpinBox* m_qFactorSpin;
    QSlider*        m_qFactorSlider;
    QCheckBox*      m_outputLinearCheck;
    QCheckBox*      m_denoiseCheck;
    QDoubleSpinBox* m_thresholdSpin;
    QDoubleSpinBox* m_curvesSpin;

    QLabel*       m_statusLabel;
    QProgressBar* m_progress;
    QPushButton*  m_executeBtn;

    // --- Worker ---
    ContinuumSubtractWorker* m_worker = nullptr;
};

#endif // CONTINUUM_SUBTRACTION_DIALOG_H