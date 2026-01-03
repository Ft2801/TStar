cmake_minimum_required(VERSION 3.16)

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

# Split Version for Windows Resource
string(REPLACE "." ";" VERSION_LIST ${TSTAR_VERSION})
list(GET VERSION_LIST 0 TSTAR_VERSION_MAJOR)
list(GET VERSION_LIST 1 TSTAR_VERSION_MINOR)
list(GET VERSION_LIST 2 TSTAR_VERSION_PATCH)

# Ensure none are empty
if(NOT TSTAR_VERSION_MAJOR) 
    set(TSTAR_VERSION_MAJOR 1) 
endif()
if(NOT TSTAR_VERSION_MINOR) 
    set(TSTAR_VERSION_MINOR 0) 
endif()
if(NOT TSTAR_VERSION_PATCH) 
    set(TSTAR_VERSION_PATCH 0) 
endif()

# Configure version.rc
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TStar.rc"
    @ONLY
)

add_definitions(-DTSTAR_VERSION="${TSTAR_VERSION}")


project(TStar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable AutoMoc predefs generation (Fixes pre-processing failures on Windows/MinGW)
set(CMAKE_AUTOMOC_COMPILER_PREDEFS OFF CACHE BOOL "" FORCE)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Prioritize User's Qt Location (Removed non-existent Program Files path)
# find_package will use standard locations or Qt6_DIR if provided.

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Concurrent Xml LinguistTools)
find_package(OpenMP REQUIRED)
# GSL
# Directories setup
set(GSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/gsl")
set(CFITSIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/cfitsio")

# Ensure we use the local libs if present
if(EXISTS "${GSL_DIR}")
    set(GSL_INCLUDE_DIRS "${GSL_DIR}/include")
    set(GSL_LIBRARY_DIRS "${GSL_DIR}/lib")
    set(GSL_LIBRARIES gsl gslcblas)
else()
    find_package(GSL REQUIRED)
    set(GSL_LIBRARIES GSL::gsl GSL::gslcblas)
endif()

# OpenCV
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv")
find_package(OpenCV REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv" NO_DEFAULT_PATH)

# ===========================
# Optional Compression Libraries (for XISF support)
# ===========================

# LZ4 - Check local deps first, then system
set(LZ4_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/lz4")
if(EXISTS "${LZ4_DIR}/include/lz4.h")
    message(STATUS "Using local LZ4 from ${LZ4_DIR}")
    set(LZ4_FOUND TRUE)
    set(LZ4_INCLUDE_DIRS "${LZ4_DIR}/include")
    if(EXISTS "${LZ4_DIR}/lib/liblz4.a")
        set(LZ4_LIBRARIES "${LZ4_DIR}/lib/liblz4.a")
    elseif(EXISTS "${LZ4_DIR}/lib/lz4.lib")
        set(LZ4_LIBRARIES "${LZ4_DIR}/lib/lz4.lib")
    else()
        set(LZ4_LIBRARIES lz4)
    endif()
else()
    # Try system LZ4
    find_path(LZ4_INCLUDE_DIRS lz4.h)
    find_library(LZ4_LIBRARIES NAMES lz4 liblz4)
    if(LZ4_INCLUDE_DIRS AND LZ4_LIBRARIES)
        set(LZ4_FOUND TRUE)
        message(STATUS "Found system LZ4: ${LZ4_LIBRARIES}")
    else()
        set(LZ4_FOUND FALSE)
        message(STATUS "LZ4 not found - XISF LZ4 compression will be disabled")
    endif()
endif()

# Zstd - Check local deps first, then system
set(ZSTD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd")
if(EXISTS "${ZSTD_DIR}/include/zstd.h")
    message(STATUS "Using local Zstd from ${ZSTD_DIR}")
    set(ZSTD_FOUND TRUE)
    set(ZSTD_INCLUDE_DIRS "${ZSTD_DIR}/include")
    if(EXISTS "${ZSTD_DIR}/lib/libzstd.a")
        set(ZSTD_LIBRARIES "${ZSTD_DIR}/lib/libzstd.a")
    elseif(EXISTS "${ZSTD_DIR}/lib/zstd.lib")
        set(ZSTD_LIBRARIES "${ZSTD_DIR}/lib/zstd.lib")
    else()
        set(ZSTD_LIBRARIES zstd)
    endif()
else()
    # Try system Zstd
    find_path(ZSTD_INCLUDE_DIRS zstd.h)
    find_library(ZSTD_LIBRARIES NAMES zstd libzstd)
    if(ZSTD_INCLUDE_DIRS AND ZSTD_LIBRARIES)
        set(ZSTD_FOUND TRUE)
        message(STATUS "Found system Zstd: ${ZSTD_LIBRARIES}")
    else()
        set(ZSTD_FOUND FALSE)
        message(STATUS "Zstd not found - XISF Zstd compression will be disabled")
    endif()
endif()

# Force disable AutoMoc predefs (Normal variable shadows cache)
set(CMAKE_AUTOMOC_COMPILER_PREDEFS OFF)

add_executable(TStar
    ${CMAKE_CURRENT_BINARY_DIR}/TStar.rc
    # ===========================
    # Root (src/)
    # ===========================
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ImageViewer.cpp
    src/ImageViewer.h
    src/ImageBuffer.cpp
    src/ImageBuffer.h
    src/resources.qrc
    
    # ===========================
    # Core (src/core/)
    # ===========================
    src/core/RobustStatistics.cpp
    src/core/RobustStatistics.h
    src/core/MaskManager.h
    
    # ===========================
    # IO (src/io/)
    # ===========================
    src/io/FitsLoader.cpp
    src/io/FitsLoader.h
    src/io/SimpleTiffWriter.cpp
    src/io/SimpleTiffReader.cpp
    src/io/SimpleTiffReader.h
    src/io/XISFReader.cpp
    src/io/XISFReader.h
    src/io/XISFWriter.cpp
    src/io/XISFWriter.h
    src/io/CompressionUtils.cpp
    src/io/CompressionUtils.h
    src/io/FitsHeaderUtils.cpp
    src/io/FitsHeaderUtils.h
    
    # ===========================
    # Widgets (src/widgets/)
    # ===========================
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
    
    # ===========================
    # Algorithms (src/algos/)
    # ===========================
    src/algos/AbeMath.cpp
    src/algos/GHSAlgo.cpp
    src/algos/GHSAlgo.h
    src/algos/ChannelOps.cpp
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
    
    # ===========================
    # Network (src/network/)
    # ===========================
    src/network/UpdateChecker.cpp
    src/network/UpdateChecker.h

    
    # ===========================
    # Dialogs (src/dialogs/)
    # ===========================
    src/dialogs/UpdateDialog.cpp
    src/dialogs/UpdateDialog.h
    src/dialogs/StretchDialog.cpp
    src/dialogs/StretchDialog.h
    src/dialogs/CropRotateDialog.cpp
    src/dialogs/CropRotateDialog.h
    src/dialogs/ABEDialog.cpp
    src/dialogs/SCNRDialog.cpp
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
    src/widgets/AnnotationOverlay.cpp
    src/widgets/AnnotationOverlay.h
    
    # ===========================
    # Astrometry (src/astrometry/)
    # ===========================
    src/astrometry/SimbadSearcher.cpp
    src/astrometry/SimbadSearcher.h
    src/astrometry/TriangleMatcher.cpp
    src/astrometry/TriangleMatcher.h
    src/astrometry/NativePlateSolver.cpp
    src/astrometry/NativePlateSolver.h
    src/astrometry/WcsSolver.h
    src/astrometry/WCSUtils.cpp
    src/astrometry/WCSUtils.h
    
    # ===========================
    # Photometry (src/photometry/)
    # ===========================
    src/photometry/StarDetector.cpp
    src/photometry/StarDetector.h
    src/photometry/CatalogClient.cpp
    src/photometry/CatalogClient.h
    src/photometry/PCCCalibrator.cpp
    src/photometry/PCCCalibrator.h
    src/photometry/AperturePhotometry.cpp
    src/photometry/AperturePhotometry.h
)

# Translations
qt_add_translations(TStar
    TS_FILES 
        "translations/tstar_en.ts"
        "translations/tstar_it.ts"
        "translations/tstar_es.ts"
        "translations/tstar_fr.ts"
        "translations/tstar_de.ts"
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
    RESOURCE "src/i18n.qrc"
)

target_include_directories(TStar PRIVATE 
    src
    src/astrometry
    src/photometry
    src/network
    src/dialogs
    src/widgets
    src/io
    src/algos
    ${GSL_INCLUDE_DIRS}
    "${CFITSIO_DIR}/include"
    ${OpenCV_INCLUDE_DIRS}
)

# Add optional compression library includes
if(LZ4_FOUND)
    target_include_directories(TStar PRIVATE ${LZ4_INCLUDE_DIRS})
    target_compile_definitions(TStar PRIVATE HAVE_LZ4)
endif()
if(ZSTD_FOUND)
    target_include_directories(TStar PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_compile_definitions(TStar PRIVATE HAVE_ZSTD)
endif()

target_link_directories(TStar PRIVATE
    ${GSL_LIBRARY_DIRS}
)

# Definitive fix for AutoMoc predefs failure on Windows/MinGW
set_property(TARGET TStar PROPERTY AUTOMOC_COMPILER_PREDEFS OFF)
# libraries need to be linked explicitly if they are static or simpler
# CFITSIO
target_link_libraries(TStar PRIVATE 
    Qt6::Widgets 
    Qt6::Svg
    Qt6::Network
    Qt6::Network
    Qt6::Concurrent
    Qt6::Xml
    OpenMP::OpenMP_CXX
    "${CFITSIO_DIR}/lib/libcfitsio.a" 
    z
    ws2_32
    z
    ws2_32
    ${GSL_LIBRARIES}
    ${OpenCV_LIBS}
)

# Link optional compression libraries
if(LZ4_FOUND)
    target_link_libraries(TStar PRIVATE ${LZ4_LIBRARIES})
    message(STATUS "LZ4 compression enabled for XISF")
endif()
if(ZSTD_FOUND)
    target_link_libraries(TStar PRIVATE ${ZSTD_LIBRARIES})
    message(STATUS "Zstd compression enabled for XISF")
endif()

if(WIN32)
    # set_property(TARGET TStar PROPERTY WIN32_EXECUTABLE ON)
endif()

# Copy images to build directory
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/images" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
# Copy catalogs to build directory

