/**
 * @file StackingCommands.h
 * @brief Script commands for stacking and preprocessing operations
 * 
 * Defines all commands related to stacking and preprocessing
 * that can be used in TStar scripts.
 * 
 * Copyright (C) 2024-2026 TStar Team
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

/**
 * @brief Stacking-related script commands
 * 
 * Provides commands for:
 * - cd: Change working directory
 * - convert: Convert files to FITS format
 * - register: Register images
 * - stack: Stack images
 * - calibrate: Calibrate images with masters
 * - load/save/close: File operations
 */
class StackingCommands {
public:
    /**
     * @brief Register all stacking commands with a runner
     */
    static void registerCommands(ScriptRunner& runner);
    
    /**
     * @brief Get current sequence
     */
    static Stacking::ImageSequence* currentSequence() { return s_sequence.get(); }
    
    /**
     * @brief Get current preprocessing engine
     */
    static Preprocessing::PreprocessingEngine* preprocessor() { return &s_preprocessor; }
    
private:
    // Command handlers
    static bool cmdCd(const ScriptCommand& cmd);
    static bool cmdConvert(const ScriptCommand& cmd);
    static bool cmdLoad(const ScriptCommand& cmd);
    static bool cmdSave(const ScriptCommand& cmd);
    static bool cmdClose(const ScriptCommand& cmd);
    
    static bool cmdStack(const ScriptCommand& cmd);
    static bool cmdCalibrate(const ScriptCommand& cmd);
    static bool cmdRegister(const ScriptCommand& cmd);
    
    static bool cmdSetMaster(const ScriptCommand& cmd);
    static bool cmdDebayer(const ScriptCommand& cmd);
    static bool cmdSeqExtract(const ScriptCommand& cmd);
    static bool cmdMirror(const ScriptCommand& cmd);
    static bool cmdBackground(const ScriptCommand& cmd);
    static bool cmdRGBComp(const ScriptCommand& cmd);
    static bool cmdLinearMatch(const ScriptCommand& cmd);
    static bool cmdPixelMath(const ScriptCommand& cmd);
    static bool cmdStarNet(const ScriptCommand& cmd);
    
    // Phase 5: Project Commands
    static bool cmdNewProject(const ScriptCommand& cmd);
    static bool cmdConvertAll(const ScriptCommand& cmd);
    static bool cmdAutoStack(const ScriptCommand& cmd);
    
    // Helper functions
    static QString resolvePath(const QString& path);
    static Stacking::Method parseMethod(const QString& str);
    static Stacking::Rejection parseRejection(const QString& str);
    static Stacking::NormalizationMethod parseNormalization(const QString& str);
    static Stacking::WeightingType parseWeighting(const QString& str);
    
    // State
    static std::unique_ptr<Stacking::ImageSequence> s_sequence;
    static std::unique_ptr<ImageBuffer> s_currentImage;
    static Preprocessing::PreprocessingEngine s_preprocessor;
    static QString s_workingDir;
    static ScriptRunner* s_runner;
};

} // namespace Scripting

#endif // STACKING_COMMANDS_H
