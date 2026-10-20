/**
 * @file StackingCommands.h
 * @brief Script command implementations for image stacking and preprocessing.
 *
 * This class registers all stacking, calibration, and image-processing
 * commands with the ScriptRunner and provides the static handler
 * functions invoked during script execution.
 *
 * Key command groups:
 *   - File I/O        : load, save, close, convert, convertall
 *   - Calibration     : calibrate, setmaster
 *   - Registration    : register, seqapplyreg
 *   - Stacking        : stack, autostack, newproject
 *   - Processing      : debayer, background, starnet, pixelmath, rgbcomp,
 *                        linear_match, mirror, rotate, resample, crop,
 *                        threshold, math, stat, update_key
 *   - CFA Extraction  : seqextract_Ha, seqextract_HaOIII
 *
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
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

#ifndef STACKING_COMMANDS_H
#define STACKING_COMMANDS_H

#include "ScriptTypes.h"
#include "ScriptRunner.h"
#include "../stacking/StackingEngine.h"
#include "../stacking/StackingSequence.h"
#include "../stacking/StackingProject.h"
#include "../stacking/SequenceFile.h"
#include "../preprocessing/Preprocessing.h"
#include "../stacking/PixelMath.h"

#include <QString>

namespace Scripting {

class StackingCommands {
public:
    // ========================================================================
    // Registration
    // ========================================================================

    /**
     * @brief Register all stacking-related commands with the given runner.
     * @param runner  The ScriptRunner that will own the command definitions.
     */
    static void registerCommands(ScriptRunner& runner);

    // ========================================================================
    // Accessors
    // ========================================================================

    /** @brief Return the currently loaded image sequence, or nullptr. */
    static Stacking::ImageSequence* currentSequence()
    { return s_sequence.get(); }

    /** @brief Return the preprocessing engine instance. */
    static Preprocessing::PreprocessingEngine* preprocessor()
    { return &s_preprocessor; }

    /**
     * @brief Seed the script's current image from an external buffer.
     *
     * Call this before executing a script so that commands such as
     * @c save and @c starnet operate on the image displayed in the main
     * window rather than on an empty buffer.
     */
    static void initCurrentImage(const ImageBuffer& image);

    /** @brief Return a read-only pointer to the script's current image. */
    static const ImageBuffer* getCurrentImage()
    { return s_currentImage.get(); }

private:
    // ========================================================================
    // Command handlers -- file I/O
    // ========================================================================

    static bool cmdCd(const ScriptCommand& cmd);
    static bool cmdConvert(const ScriptCommand& cmd);
    static bool cmdLoad(const ScriptCommand& cmd);
    static bool cmdSave(const ScriptCommand& cmd);
    static bool cmdClose(const ScriptCommand& cmd);

    // ========================================================================
    // Command handlers -- stacking pipeline
    // ========================================================================

    static bool cmdStack(const ScriptCommand& cmd);
    static bool cmdMosaic(const ScriptCommand& cmd);
    static bool cmdCalibrate(const ScriptCommand& cmd);
    static bool cmdRegister(const ScriptCommand& cmd);
    static bool cmdNewProject(const ScriptCommand& cmd);
    static bool cmdConvertAll(const ScriptCommand& cmd);
    static bool cmdAutoStack(const ScriptCommand& cmd);

    // ========================================================================
    // Command handlers -- preprocessing and processing
    // ========================================================================

    static bool cmdSetMaster(const ScriptCommand& cmd);
    static bool cmdDebayer(const ScriptCommand& cmd);
    static bool cmdSeqExtract(const ScriptCommand& cmd);
    static bool cmdMirror(const ScriptCommand& cmd);
    static bool cmdBackground(const ScriptCommand& cmd);
    static bool cmdRGBComp(const ScriptCommand& cmd);
    static bool cmdLinearMatch(const ScriptCommand& cmd);
    static bool cmdPixelMath(const ScriptCommand& cmd);
    static bool cmdStarNet(const ScriptCommand& cmd);

    // ========================================================================
    // Command handlers -- geometry and metadata
    // ========================================================================

    static bool cmdSeqPlateSolve(const ScriptCommand& cmd);
    static bool cmdSeqApplyReg(const ScriptCommand& cmd);
    static bool cmdRotate(const ScriptCommand& cmd);
    static bool cmdResample(const ScriptCommand& cmd);
    static bool cmdUpdateKey(const ScriptCommand& cmd);
    static bool cmdCrop(const ScriptCommand& cmd);
    static bool cmdStat(const ScriptCommand& cmd);
    static bool cmdThreshold(const ScriptCommand& cmd);
    static bool cmdMath(const ScriptCommand& cmd);

    // ========================================================================
    // Helper functions
    // ========================================================================

    /** @brief Resolve a possibly-relative path against the working directory. */
    static QString resolvePath(const QString& path);

    /** @brief Parse a stacking method name string to its enumeration value. */
    static Stacking::Method parseMethod(const QString& str);

    /** @brief Parse a rejection algorithm name to its enumeration value. */
    static Stacking::Rejection parseRejection(const QString& str);

    /** @brief Parse a normalization method name to its enumeration value. */
    static Stacking::NormalizationMethod parseNormalization(const QString& str);

    /** @brief Parse a weighting type name to its enumeration value. */
    static Stacking::WeightingType parseWeighting(const QString& str);

    // ========================================================================
    // Static state
    // ========================================================================

    static std::unique_ptr<Stacking::ImageSequence>  s_sequence;
    static std::unique_ptr<ImageBuffer>              s_currentImage;
    static QString                                   s_currentFilename;  ///< Basename of last loaded/saved file.
    static Stacking::FramingMode                     s_framingMode;
    static Preprocessing::PreprocessingEngine         s_preprocessor;
    static QString                                   s_workingDir;
    static ScriptRunner*                             s_runner;
};

} // namespace Scripting

#endif // STACKING_COMMANDS_H
