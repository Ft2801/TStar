cmake_minimum_required(VERSION 3.16)

# =============================================================================
# TStar Cross-Platform CMakeLists.txt
# =============================================================================
# Supports both Windows (MinGW) and macOS (Homebrew)
# Auto-detects platform and uses appropriate dependency paths
# =============================================================================

# Read version from changelog.txt
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/changelog.txt")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/changelog.txt")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/changelog.txt" VERSION_LINE REGEX "^Version [0-9.]+")
    if(VERSION_LINE)
        list(GET VERSION_LINE 0 FIRST_VERSION_LINE)
        string(REGEX MATCH "[0-9.]+" TSTAR_VERSION "${FIRST_VERSION_LINE}")
    else()
        set(TSTAR_VERSION "1.0.0")
    endif()
else()
    set(TSTAR_VERSION "1.0.0")
endif()

# Split Version
string(REPLACE "." ";" VERSION_LIST ${TSTAR_VERSION})
list(GET VERSION_LIST 0 TSTAR_VERSION_MAJOR)
list(GET VERSION_LIST 1 TSTAR_VERSION_MINOR)
list(GET VERSION_LIST 2 TSTAR_VERSION_PATCH)

if(NOT TSTAR_VERSION_MAJOR) 
    set(TSTAR_VERSION_MAJOR 1) 
endif()
if(NOT TSTAR_VERSION_MINOR) 
    set(TSTAR_VERSION_MINOR 0) 
endif()
if(NOT TSTAR_VERSION_PATCH) 
    set(TSTAR_VERSION_PATCH 0) 
endif()

# Configure version.rc for Windows
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/TStar.rc"
        @ONLY
    )
endif()

# Generate Version.cpp source file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp"
    @ONLY
)

# Remove the global TSTAR_VERSION definition to prevent full recompiles
# add_definitions(-DTSTAR_VERSION="${TSTAR_VERSION}")

project(TStar LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Compiler Optimizations & Warnings
# =============================================================================
if(MSVC)
    # Windows MSVC (less common for TStar but support if used)
    add_compile_options(/W4 /WX)
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    # GCC/Clang (used on both macOS and Windows with MinGW)
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    add_compile_options(-ffast-math)  # Enable fast math for scientific computing
    add_compile_options(-march=native)  # Architecture-specific optimizations
    
    # LTO Optimization (Conditional)
    # LTO Optimization (Conditional)
    option(ENABLE_LTO "Enable Link-Time Optimization" OFF)
    if(ENABLE_LTO)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options($<$<CONFIG:Release>:-flto=auto>)
            add_link_options($<$<CONFIG:Release>:-flto=auto>)
        else()
            add_compile_options($<$<CONFIG:Release>:-flto>)
            add_link_options($<$<CONFIG:Release>:-flto>)
        endif()
        message(STATUS "Link-Time Optimization (LTO) enabled for Release builds")
    endif()
    
    # Debug-specific flags
    add_compile_options($<$<CONFIG:Debug>:-g>)
    add_compile_options($<$<CONFIG:Debug>:-O0>)
    
    # Enable position-independent code (required for some platforms)
    add_compile_options(-fPIC)
endif()

# =============================================================================
# Platform-Specific Settings
# =============================================================================
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_MACOSX_BUNDLE ON)
    message(STATUS "Building for macOS")
elseif(WIN32)
    # Disable AutoMoc predefs generation (Fixes pre-processing failures on Windows/MinGW)
    set(CMAKE_AUTOMOC_COMPILER_PREDEFS OFF CACHE BOOL "" FORCE)
    message(STATUS "Building for Windows")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# =============================================================================
# Homebrew Detection (macOS)
# =============================================================================
if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(NOT HOMEBREW_PREFIX)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            set(HOMEBREW_PREFIX "/opt/homebrew")
        else()
            set(HOMEBREW_PREFIX "/usr/local")
        endif()
    endif()
    
    message(STATUS "Homebrew prefix: ${HOMEBREW_PREFIX}")
    
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/qt")
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/qt@6")
    
    if(NOT Qt6_DIR)
        if(EXISTS "${HOMEBREW_PREFIX}/opt/qt/lib/cmake/Qt6/Qt6Config.cmake")
            set(Qt6_DIR "${HOMEBREW_PREFIX}/opt/qt/lib/cmake/Qt6" CACHE PATH "Path to Qt6")
        elseif(EXISTS "${HOMEBREW_PREFIX}/opt/qtbase/lib/cmake/Qt6/Qt6Config.cmake")
            set(Qt6_DIR "${HOMEBREW_PREFIX}/opt/qtbase/lib/cmake/Qt6" CACHE PATH "Path to Qt6")
        endif()
    endif()
endif()

# =============================================================================
# Find Qt6
# =============================================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Concurrent Xml LinguistTools)

# =============================================================================
# OpenMP
# =============================================================================
if(APPLE)
    execute_process(
        COMMAND brew --prefix libomp
        OUTPUT_VARIABLE LIBOMP_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(LIBOMP_PREFIX AND EXISTS "${LIBOMP_PREFIX}")
        message(STATUS "Using Homebrew libomp from: ${LIBOMP_PREFIX}")
        add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
        set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
            INTERFACE_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBOMP_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES "${LIBOMP_PREFIX}/lib/libomp.dylib"
        )
        add_library(OpenMP::OpenMP_C INTERFACE IMPORTED)
        set_target_properties(OpenMP::OpenMP_C PROPERTIES
            INTERFACE_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBOMP_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES "${LIBOMP_PREFIX}/lib/libomp.dylib"
        )
    else()
        find_package(OpenMP)
        if(NOT OpenMP_FOUND)
            message(WARNING "OpenMP not found. Install with: brew install libomp")
        endif()
    endif()
else()
    find_package(OpenMP REQUIRED)
endif()

# =============================================================================
# GSL (GNU Scientific Library)
# =============================================================================
if(APPLE)
    execute_process(
        COMMAND brew --prefix gsl
        OUTPUT_VARIABLE GSL_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(GSL_PREFIX AND EXISTS "${GSL_PREFIX}")
        set(GSL_INCLUDE_DIRS "${GSL_PREFIX}/include")
        set(GSL_LIBRARY_DIRS "${GSL_PREFIX}/lib")
        set(GSL_LIBRARIES "${GSL_PREFIX}/lib/libgsl.dylib" "${GSL_PREFIX}/lib/libgslcblas.dylib")
        message(STATUS "Using Homebrew GSL from: ${GSL_PREFIX}")
    else()
        find_package(GSL REQUIRED)
        set(GSL_LIBRARIES GSL::gsl GSL::gslcblas)
    endif()
else()
    # Windows: local deps
    set(GSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/gsl")
    if(EXISTS "${GSL_DIR}")
        set(GSL_INCLUDE_DIRS "${GSL_DIR}/include")
        set(GSL_LIBRARY_DIRS "${GSL_DIR}/lib")
        set(GSL_LIBRARIES gsl gslcblas)
    else()
        find_package(GSL REQUIRED)
        set(GSL_LIBRARIES GSL::gsl GSL::gslcblas)
    endif()
endif()

# =============================================================================
# CFITSIO
# =============================================================================
if(APPLE)
    execute_process(
        COMMAND brew --prefix cfitsio
        OUTPUT_VARIABLE CFITSIO_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(CFITSIO_PREFIX AND EXISTS "${CFITSIO_PREFIX}")
        set(CFITSIO_INCLUDE_DIR "${CFITSIO_PREFIX}/include")
        set(CFITSIO_LIBRARY "${CFITSIO_PREFIX}/lib/libcfitsio.dylib")
        message(STATUS "Using Homebrew CFITSIO from: ${CFITSIO_PREFIX}")
    else()
        message(FATAL_ERROR "CFITSIO not found. Install with: brew install cfitsio")
    endif()
else()
    # Windows: local deps
    set(CFITSIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/cfitsio")
    set(CFITSIO_INCLUDE_DIR "${CFITSIO_DIR}/include")
    set(CFITSIO_LIBRARY "${CFITSIO_DIR}/lib/libcfitsio.a")
endif()

# =============================================================================
# OpenCV
# =============================================================================
if(APPLE)
    execute_process(
        COMMAND brew --prefix opencv
        OUTPUT_VARIABLE OPENCV_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(OPENCV_PREFIX AND EXISTS "${OPENCV_PREFIX}")
        set(OpenCV_DIR "${OPENCV_PREFIX}/lib/cmake/opencv4")
    endif()
    # Explicitly exclude dnn module which depends on OpenVINO
    # Only include modules actually used by TStar to minimize dependencies
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs photo features2d calib3d)
else()
    # Windows: local deps
    set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv")
    find_package(OpenCV REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv" NO_DEFAULT_PATH)
endif()

# =============================================================================
# Optional Compression Libraries (LZ4, Zstd)
# =============================================================================

# LZ4
if(APPLE)
    execute_process(
        COMMAND brew --prefix lz4
        OUTPUT_VARIABLE LZ4_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(LZ4_PREFIX AND EXISTS "${LZ4_PREFIX}/include/lz4.h")
        set(LZ4_FOUND TRUE)
        set(LZ4_INCLUDE_DIRS "${LZ4_PREFIX}/include")
        set(LZ4_LIBRARIES "${LZ4_PREFIX}/lib/liblz4.dylib")
        message(STATUS "Using Homebrew LZ4 from: ${LZ4_PREFIX}")
    endif()
else()
    # Windows: local deps in deps/lz4
    set(LZ4_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/lz4")
    if(EXISTS "${LZ4_DIR}/include/lz4.h")
        message(STATUS "Using local LZ4 from ${LZ4_DIR}")
        set(LZ4_FOUND TRUE)
        set(LZ4_INCLUDE_DIRS "${LZ4_DIR}/include")
        if(EXISTS "${LZ4_DIR}/static/liblz4_static.lib")
            set(LZ4_LIBRARIES "${LZ4_DIR}/static/liblz4_static.lib")
        else()
            set(LZ4_FOUND FALSE)
            message(STATUS "LZ4 static library not found in ${LZ4_DIR}/static")
        endif()
    else()
        set(LZ4_FOUND FALSE)
    endif()
endif()

if(NOT LZ4_FOUND)
    message(STATUS "LZ4 not found - XISF LZ4 compression will be disabled")
endif()

# Zstd
if(APPLE)
    execute_process(
        COMMAND brew --prefix zstd
        OUTPUT_VARIABLE ZSTD_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(ZSTD_PREFIX AND EXISTS "${ZSTD_PREFIX}/include/zstd.h")
        set(ZSTD_FOUND TRUE)
        set(ZSTD_INCLUDE_DIRS "${ZSTD_PREFIX}/include")
        set(ZSTD_LIBRARIES "${ZSTD_PREFIX}/lib/libzstd.dylib")
        message(STATUS "Using Homebrew Zstd from: ${ZSTD_PREFIX}")
    endif()
else()
    # Windows: local deps in deps/zstd
    set(ZSTD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd")
    if(EXISTS "${ZSTD_DIR}/include/zstd.h")
        message(STATUS "Using local Zstd from ${ZSTD_DIR}")
        set(ZSTD_FOUND TRUE)
        set(ZSTD_INCLUDE_DIRS "${ZSTD_DIR}/include")
        if(EXISTS "${ZSTD_DIR}/static/libzstd_static.lib")
            set(ZSTD_LIBRARIES "${ZSTD_DIR}/static/libzstd_static.lib")
        else()
            set(ZSTD_FOUND FALSE)
            message(STATUS "Zstd static library not found in ${ZSTD_DIR}/static")
        endif()
    else()
        set(ZSTD_FOUND FALSE)
    endif()
endif()

if(NOT ZSTD_FOUND)
    message(STATUS "Zstd not found - XISF Zstd compression will be disabled")
endif()

# LibRaw
if(APPLE)
    execute_process(
        COMMAND brew --prefix libraw
        OUTPUT_VARIABLE LIBRAW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Check if the prefix is valid, if not try Cellar directly
    if(NOT (LIBRAW_PREFIX AND EXISTS "${LIBRAW_PREFIX}"))
        # Fallback: search in Homebrew Cellar directly
        file(GLOB LIBRAW_CELLAR_PATHS "/opt/homebrew/Cellar/libraw/*")
        if(LIBRAW_CELLAR_PATHS)
            list(GET LIBRAW_CELLAR_PATHS 0 LIBRAW_PREFIX)
        else()
            # Try Intel Homebrew location
            file(GLOB LIBRAW_CELLAR_PATHS "/usr/local/Cellar/libraw/*")
            if(LIBRAW_CELLAR_PATHS)
                list(GET LIBRAW_CELLAR_PATHS 0 LIBRAW_PREFIX)
            endif()
        endif()
    endif()
    
    if(LIBRAW_PREFIX AND EXISTS "${LIBRAW_PREFIX}")
        set(LIBRAW_FOUND TRUE)
        set(LIBRAW_INCLUDE_DIRS "${LIBRAW_PREFIX}/include")
        
        # Find the actual libraw dylib file
        find_library(LIBRAW_DYLIB NAMES libraw PATHS "${LIBRAW_PREFIX}/lib" NO_DEFAULT_PATH)
        if(LIBRAW_DYLIB)
            set(LIBRAW_LIBRARIES "${LIBRAW_DYLIB}")
        else()
            # Fallback to generic name, let linker find it
            set(LIBRAW_LIBRARIES raw)
        endif()
        
        message(STATUS "Using Homebrew LibRaw from: ${LIBRAW_PREFIX}")
        message(STATUS "  - Include: ${LIBRAW_INCLUDE_DIRS}")
        message(STATUS "  - Library: ${LIBRAW_LIBRARIES}")
    endif()
else()
    # Windows: local deps
    set(LIBRAW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/libraw")
    if(EXISTS "${LIBRAW_DIR}/CMakeLists.txt")
         message(STATUS "Building local LibRaw from source")
         add_subdirectory(deps/libraw)
         set(LIBRAW_FOUND TRUE)
         set(LIBRAW_LIBRARIES libraw)
         set(LIBRAW_INCLUDE_DIRS "${LIBRAW_DIR}")
    elseif(EXISTS "${LIBRAW_DIR}/libraw/libraw.h")
        set(LIBRAW_FOUND TRUE)
        set(LIBRAW_INCLUDE_DIRS "${LIBRAW_DIR}")
        if(WIN32)
            if(EXISTS "${LIBRAW_DIR}/lib/libraw.lib")
                set(LIBRAW_LIBRARIES "${LIBRAW_DIR}/lib/libraw.lib")
            else()
                set(LIBRAW_LIBRARIES "${LIBRAW_DIR}/lib/libraw_static.lib")
            endif()
        else()
            set(LIBRAW_LIBRARIES raw)
        endif()
        message(STATUS "Using local LibRaw from ${LIBRAW_DIR}")
    endif()
endif()

# Force disable AutoMoc predefs on Windows
if(WIN32)
    set(CMAKE_AUTOMOC_COMPILER_PREDEFS OFF)
endif()

# =============================================================================
# Source Files
# =============================================================================
set(TSTAR_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ImageViewer.cpp
    src/ImageViewer.h
    src/ImageBuffer.cpp
    src/ImageBuffer.h
    src/ImageBufferDelta.cpp
    src/ImageBufferDelta.h
    src/MainWindowCallbacks.h
    src/Icons.h
    src/resources.qrc
    
    # Core
    src/core/RobustStatistics.cpp
    src/core/RobustStatistics.h
    src/core/ThreadState.cpp
    src/core/ThreadState.h
    src/core/MaskManager.h
    src/core/MaskLayer.h
    src/core/Logger.cpp
    src/core/Logger.h
    src/core/DpiHelper.h
    src/core/ErrorHandling.cpp
    src/core/ErrorHandling.h
    src/core/ResourceManager.cpp
    src/core/ResourceManager.h
    src/core/TStarApplication.cpp
    src/core/TStarApplication.h
    src/core/GlobalExceptionHandler.cpp
    src/core/GlobalExceptionHandler.h
    src/core/SwapManager.cpp
    src/core/SwapManager.h
    src/core/SimdOps.cpp
    src/core/SimdOps.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp
    
    # IO
    src/io/FitsLoader.cpp
    src/io/FitsLoader.h
    src/io/FitsLoaderC.c
    src/io/FitsLoaderC.h
    src/io/FitsLoaderCWrapper.cpp
    src/io/FitsLoaderCWrapper.h
    src/io/SimpleTiffWriter.cpp
    src/io/SimpleTiffReader.cpp
    src/io/SimpleTiffReader.h
    src/io/SimpleTiffWriter.cpp
    src/io/SimpleTiffWriter.h
    src/io/XISFReader.cpp
    src/io/XISFReader.h
    src/io/XISFWriter.cpp
    src/io/XISFWriter.h
    src/io/CompressionUtils.cpp
    src/io/CompressionUtils.h
    src/io/FitsHeaderUtils.cpp
    src/io/FitsHeaderUtils.h
    src/io/SERFile.cpp
    src/io/SERFile.h

    
    # Widgets
    src/widgets/HistogramWidget.cpp
    src/widgets/HistogramWidget.h
    src/widgets/CustomMdiSubWindow.cpp
    src/widgets/CustomMdiSubWindow.h
    src/widgets/SplashScreen.cpp
    src/widgets/SplashScreen.h
    src/widgets/SidebarWidget.cpp
    src/widgets/SidebarWidget.h
    src/widgets/HeaderPanel.cpp
    src/widgets/HeaderPanel.h
    src/widgets/AnnotationOverlay.cpp
    src/widgets/AnnotationOverlay.h
    src/widgets/SimplePlotWidget.cpp
    src/widgets/SimplePlotWidget.h
    
    # Algorithms
    src/algos/AbeMath.cpp
    src/algos/AbeMath.h
    src/algos/GHSAlgo.cpp
    src/algos/GHSAlgo.h
    src/algos/ChannelOps.cpp
    src/algos/ChannelOps.h
    src/algos/CubicSpline.h
    src/algos/GraXpertRunner.cpp
    src/algos/GraXpertRunner.h
    src/algos/CosmicClarityRunner.cpp
    src/algos/CosmicClarityRunner.h
    src/algos/StarNetRunner.cpp
    src/algos/StarNetRunner.h
    src/algos/RARRunner.cpp
    src/algos/RARRunner.h
    src/algos/StarStretchRunner.cpp
    src/algos/StarStretchRunner.h
    src/algos/StarRecompositionRunner.cpp
    src/algos/StarRecompositionRunner.h
    src/algos/PerfectPaletteRunner.cpp
    src/algos/PerfectPaletteRunner.h
    src/algos/StatisticalStretch.cpp
    src/algos/StatisticalStretch.h
    
    # Background
    src/background/BackgroundExtraction.cpp
    src/background/BackgroundExtraction.h

    # Stacking
    src/stacking/StackingProject.cpp
    src/stacking/StackingProject.h
    src/stacking/SequenceFile.cpp
    src/stacking/SequenceFile.h
    src/stacking/StackingTypes.h
    src/stacking/StackingEngine.cpp
    src/stacking/StackingEngine.h
    src/stacking/StackingSequence.cpp
    src/stacking/StackingSequence.h
    src/stacking/Registration.cpp
    src/stacking/Registration.h
    src/stacking/RejectionAlgorithms.cpp
    src/stacking/RejectionAlgorithms.h
    src/stacking/RejectionMaps.cpp
    src/stacking/RejectionMaps.h
    src/stacking/Normalization.cpp
    src/stacking/Normalization.h
    src/stacking/OverlapNormalization.cpp
    src/stacking/OverlapNormalization.h
    src/stacking/AnscombeTransform.h
    src/stacking/Statistics.cpp
    src/stacking/Statistics.h
    src/stacking/Weighting.cpp
    src/stacking/Weighting.h
    src/stacking/Blending.cpp
    src/stacking/Blending.h
    src/stacking/DrizzleStacking.cpp
    src/stacking/DrizzleStacking.h
    src/stacking/ImageCache.cpp
    src/stacking/ImageCache.h
    src/stacking/PixelMath.cpp
    src/stacking/Distortion.cpp
    src/stacking/Distortion.h
    src/stacking/CosmeticCorrection.cpp
    src/stacking/CosmeticCorrection.h
    src/stacking/PixelMath.h
    
    # Network
    src/network/UpdateChecker.cpp
    src/network/UpdateChecker.h
    
    # Dialogs
    src/dialogs/UpdateDialog.cpp
    src/dialogs/UpdateDialog.h
    src/dialogs/DialogBase.cpp
    src/dialogs/DialogBase.h
    src/dialogs/StretchDialog.cpp
    src/dialogs/StretchDialog.h
    src/dialogs/CropRotateDialog.cpp
    src/dialogs/CropRotateDialog.h
    src/dialogs/AboutDialog.cpp
    src/dialogs/AboutDialog.h
    src/dialogs/ABEDialog.cpp
    src/dialogs/ABEDialog.h
    src/dialogs/SCNRDialog.cpp
    src/dialogs/SCNRDialog.h
    src/dialogs/GHSDialog.cpp
    src/dialogs/GHSDialog.h
    src/dialogs/ChannelCombinationDialog.cpp
    src/dialogs/ChannelCombinationDialog.h
    src/dialogs/SettingsDialog.cpp
    src/dialogs/SettingsDialog.h
    src/dialogs/GraXpertDialog.cpp
    src/dialogs/GraXpertDialog.h
    src/dialogs/CosmicClarityDialog.cpp
    src/dialogs/CosmicClarityDialog.h
    src/dialogs/StarNetDialog.cpp
    src/dialogs/StarNetDialog.h
    src/dialogs/RARDialog.cpp
    src/dialogs/RARDialog.h
    src/dialogs/StarStretchDialog.cpp
    src/dialogs/StarStretchDialog.h
    src/dialogs/StarRecompositionDialog.cpp
    src/dialogs/StarRecompositionDialog.h
    src/dialogs/AstroSpikeDialog.cpp
    src/dialogs/AstroSpikeDialog.h
    src/dialogs/PerfectPaletteDialog.cpp
    src/dialogs/PerfectPaletteDialog.h
    src/dialogs/BackgroundNeutralizationDialog.cpp
    src/dialogs/BackgroundNeutralizationDialog.h
    src/dialogs/PixelMathDialog.cpp
    src/dialogs/PixelMathDialog.h
    src/dialogs/PlateSolvingDialog.cpp
    src/dialogs/PlateSolvingDialog.h
    src/dialogs/PCCDialog.cpp
    src/dialogs/PCCDialog.h
    src/dialogs/PCCDistributionDialog.cpp
    src/dialogs/PCCDistributionDialog.h
    src/dialogs/CurvesDialog.cpp
    src/dialogs/CurvesDialog.h
    src/dialogs/HeaderViewerDialog.cpp
    src/dialogs/HeaderViewerDialog.h
    src/dialogs/ArcsinhStretchDialog.cpp
    src/dialogs/ArcsinhStretchDialog.h
    src/dialogs/HistogramStretchDialog.cpp
    src/dialogs/HistogramStretchDialog.h
    src/dialogs/WavescaleHDRDialog.cpp
    src/dialogs/WavescaleHDRDialog.h
    src/dialogs/SaturationDialog.cpp
    src/dialogs/SaturationDialog.h
    src/dialogs/StarAnalysisDialog.cpp
    src/dialogs/StarAnalysisDialog.h
    src/dialogs/HeaderEditorDialog.cpp
    src/dialogs/HeaderEditorDialog.h
    src/dialogs/AboutDialog.cpp
    src/dialogs/MaskCanvas.cpp
    src/dialogs/MaskCanvas.h
    src/dialogs/MaskGenerationDialog.cpp
    src/dialogs/MaskGenerationDialog.h
    src/dialogs/LivePreviewDialog.cpp
    src/dialogs/LivePreviewDialog.h
    src/dialogs/ApplyMaskDialog.cpp
    src/dialogs/ApplyMaskDialog.h
    src/dialogs/DebayerDialog.cpp
    src/dialogs/DebayerDialog.h
    src/dialogs/ContinuumSubtractionDialog.cpp
    src/dialogs/ContinuumSubtractionDialog.h
    src/dialogs/AnnotationToolDialog.cpp
    src/dialogs/AnnotationToolDialog.h
    src/dialogs/HelpDialog.cpp
    src/dialogs/HelpDialog.h
    src/dialogs/ClaheDialog.cpp
    src/dialogs/ClaheDialog.h
    src/dialogs/SelectiveColorDialog.cpp
    src/dialogs/SelectiveColorDialog.h
    src/dialogs/CorrectionBrushDialog.cpp
    src/dialogs/CorrectionBrushDialog.h
    src/dialogs/ExtractLuminanceDialog.cpp
    src/dialogs/ExtractLuminanceDialog.h
    src/dialogs/RecombineLuminanceDialog.cpp
    src/dialogs/RecombineLuminanceDialog.h
    src/dialogs/AberrationInspectorDialog.cpp
    src/dialogs/AberrationInspectorDialog.h
    src/dialogs/NewProjectDialog.cpp
    src/dialogs/NewProjectDialog.h
    src/dialogs/StackingDialog.cpp
    src/dialogs/StackingDialog.h
    src/dialogs/RegistrationDialog.cpp
    src/dialogs/RegistrationDialog.h
    src/dialogs/PreprocessingDialog.cpp
    src/dialogs/PreprocessingDialog.h
    src/dialogs/ScriptBrowserDialog.cpp
    src/dialogs/ScriptBrowserDialog.h
    src/dialogs/ConversionDialog.cpp
    src/dialogs/ConversionDialog.h
    
    # Astrometry
    src/astrometry/SimbadSearcher.cpp
    src/astrometry/SimbadSearcher.h
    src/astrometry/TriangleMatcher.cpp
    src/astrometry/TriangleMatcher.h
    src/astrometry/NativePlateSolver.cpp
    src/astrometry/NativePlateSolver.h
    src/astrometry/WcsSolver.h
    src/astrometry/WCSUtils.cpp
    src/astrometry/WCSUtils.h
    
    # Photometry
    src/photometry/StarDetector.cpp
    src/photometry/StarDetector.h
    src/photometry/CatalogClient.cpp
    src/photometry/CatalogClient.h
    src/photometry/PCCCalibrator.cpp
    src/photometry/PCCCalibrator.h
    src/photometry/PCCResult.h
    src/photometry/AperturePhotometry.cpp
    src/photometry/AperturePhotometry.h
    
    # Preprocessing
    src/preprocessing/PreprocessingTypes.h
    src/preprocessing/MasterFrames.cpp
    src/preprocessing/MasterFrames.h
    src/preprocessing/Preprocessing.cpp
    src/preprocessing/Preprocessing.h
    src/preprocessing/Debayer.cpp
    src/preprocessing/Debayer.h
    src/preprocessing/XTransDemosaic.cpp
    src/preprocessing/XTransDemosaic.h
    
    # Calibration
    src/calibration/CalibrationEngine.cpp
    src/calibration/CalibrationC.c
    src/calibration/CalibrationEngine.h

    # Scripting
    src/scripting/ScriptTypes.h
    src/scripting/ScriptParser.cpp
    src/scripting/ScriptParser.h
    src/scripting/ScriptRunner.cpp
    src/scripting/ScriptRunner.h
    src/scripting/msvc_compat.cpp
    src/scripting/StackingCommands.cpp
    src/scripting/StackingCommands.h
    


    src/dialogs/ScriptDialog.cpp
    src/dialogs/ScriptDialog.h
)

# =============================================================================
# Create Executable
# =============================================================================
if(APPLE)
    # macOS App Bundle
    set(MACOSX_BUNDLE_ICON_FILE TStar.icns)
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/src/images/TStar.icns")
    
    if(EXISTS "${APP_ICON_MACOS}")
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        add_executable(TStar MACOSX_BUNDLE src/main.cpp ${TSTAR_SOURCES} ${APP_ICON_MACOS})
    else()
        add_executable(TStar MACOSX_BUNDLE src/main.cpp ${TSTAR_SOURCES})
    endif()
    
    set_target_properties(TStar PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "TStar"
        MACOSX_BUNDLE_BUNDLE_VERSION "${TSTAR_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${TSTAR_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.fabiotempera.tstar"
    )
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist")
        set_target_properties(TStar PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
        )
    endif()
else()
    # Windows with resource file
    add_executable(TStar
        ${CMAKE_CURRENT_BINARY_DIR}/TStar.rc
        ${TSTAR_SOURCES}
    )
endif()

# =============================================================================
# Translations
# =============================================================================
qt_add_translations(TStar
    TS_FILES 
        "translations/tstar_it.ts"
        "translations/tstar_es.ts"
        "translations/tstar_fr.ts"
        "translations/tstar_de.ts"
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
    RESOURCE "src/i18n.qrc"
)

# =============================================================================
# Include Directories
# =============================================================================
target_include_directories(TStar PRIVATE 
    src
    src/astrometry
    src/photometry
    src/network
    src/dialogs
    src/widgets
    src/io
    src/algos
    src/stacking
    src/preprocessing
    src/scripting
    ${GSL_INCLUDE_DIRS}
    ${CFITSIO_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

if(LZ4_FOUND)
    target_include_directories(TStar PRIVATE ${LZ4_INCLUDE_DIRS})
    target_compile_definitions(TStar PRIVATE HAVE_LZ4)
endif()
if(ZSTD_FOUND)
    target_include_directories(TStar PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_compile_definitions(TStar PRIVATE HAVE_ZSTD)
endif()
if(LIBRAW_FOUND)
    target_include_directories(TStar PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_compile_definitions(TStar PRIVATE HAVE_LIBRAW)
endif()

# =============================================================================
# Link Directories
# =============================================================================
target_link_directories(TStar PRIVATE
    ${GSL_LIBRARY_DIRS}
)

# Add Homebrew lib directories for macOS to find optional deps
if(APPLE)
    # Add standard Homebrew lib paths for linker
    if(EXISTS "/opt/homebrew/lib")
        target_link_directories(TStar PRIVATE /opt/homebrew/lib)
    endif()
    if(EXISTS "/usr/local/lib")
        target_link_directories(TStar PRIVATE /usr/local/lib)
    endif()
    
    # Add specific Homebrew package lib directories if found
    if(LIBRAW_FOUND AND LIBRAW_PREFIX)
        target_link_directories(TStar PRIVATE "${LIBRAW_PREFIX}/lib")
    endif()
endif()

# =============================================================================
# Link Libraries
# =============================================================================
if(WIN32)
    set_property(TARGET TStar PROPERTY AUTOMOC_COMPILER_PREDEFS OFF)
endif()

target_link_libraries(TStar PRIVATE 
    Qt6::Widgets 
    Qt6::Svg
    Qt6::Network
    Qt6::Concurrent
    Qt6::Xml
    ${CFITSIO_LIBRARY}
    ${GSL_LIBRARIES}
    ${OpenCV_LIBS}
)

# OpenMP
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(TStar PRIVATE OpenMP::OpenMP_CXX)
endif()
if(TARGET OpenMP::OpenMP_C)
    target_link_libraries(TStar PRIVATE OpenMP::OpenMP_C)
endif()

# Platform-specific libs
if(APPLE)
    find_package(ZLIB REQUIRED)
    target_link_libraries(TStar PRIVATE ZLIB::ZLIB)
else()
    # Windows: zlib and winsock
    target_link_libraries(TStar PRIVATE z ws2_32)
endif()

# Optional compression libraries
if(LZ4_FOUND)
    target_link_libraries(TStar PRIVATE ${LZ4_LIBRARIES})
    message(STATUS "LZ4 compression enabled for XISF")
endif()
if(ZSTD_FOUND)
    target_link_libraries(TStar PRIVATE ${ZSTD_LIBRARIES})
    message(STATUS "Zstd compression enabled for XISF")
endif()
if(LIBRAW_FOUND)
    target_link_libraries(TStar PRIVATE ${LIBRAW_LIBRARIES})
    message(STATUS "LibRaw support enabled")
endif()

# =============================================================================
# Post-build: Copy resources
# =============================================================================
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/images" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if(APPLE)
    add_custom_command(TARGET TStar POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/images"
            "$<TARGET_BUNDLE_DIR:TStar>/Contents/Resources/images"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/translations"
            "$<TARGET_BUNDLE_DIR:TStar>/Contents/Resources/translations"
        COMMENT "Copying resources to app bundle"
    )
endif()
