#!/bin/bash
# =============================================================================
# TStar Build Script for macOS
#
# Configures and builds the TStar application using CMake.
# Supports optional --clean (force CMake reconfiguration) and --lto-on
# (enable link-time optimization) flags.
# This is the macOS equivalent of build_all.bat on Windows.
# =============================================================================

set -e

# -----------------------------------------------------------------------------
# Parse command-line arguments
# -----------------------------------------------------------------------------

CLEAN_MODE=0
LTO_MODE="OFF"

for arg in "$@"; do
    case "$arg" in
        --clean)  CLEAN_MODE=1 ;;
        --lto-on) LTO_MODE="ON" ;;
    esac
done

# -----------------------------------------------------------------------------
# Script initialization
# -----------------------------------------------------------------------------

echo "==========================================="
echo " TStar Build Script (macOS)"
if [ $CLEAN_MODE -eq 1 ]; then
    echo " (CLEAN MODE - Reconfiguring CMake)"
fi
echo "==========================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
PROJECT_ROOT="$(pwd)"

# Load shared utility functions
if [ -f "$SCRIPT_DIR/macos_utils.sh" ]; then
    source "$SCRIPT_DIR/macos_utils.sh"
else
    echo "[ERROR] macos_utils.sh not found!"
    exit 1
fi

# -----------------------------------------------------------------------------
# Build configuration
# -----------------------------------------------------------------------------

CMAKE_CMD="cmake"
BUILD_TYPE="Release"
BUILD_DIR="build"
GENERATOR="Unix Makefiles"

# Prefer Ninja if available (faster incremental builds)
if command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
    echo "[INFO] Using Ninja generator"
fi

# -----------------------------------------------------------------------------
# STEP 1: Check prerequisites
# -----------------------------------------------------------------------------

echo ""
echo "[STEP 1] Checking prerequisites..."

# Homebrew
check_command brew || {
    log_error "Homebrew not found!"
    echo "Install with: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    exit 1
}
echo "  - Homebrew: OK"

HOMEBREW_PREFIX=$(get_homebrew_prefix)
echo "  - Homebrew prefix: $HOMEBREW_PREFIX"

# Qt 6
QT_PREFIX=$(detect_qt_prefix)
if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX" ]; then
    log_error "Qt6 not found!"
    echo "Install with: brew install qt@6"
    exit 1
fi
echo "  - Qt6: OK ($QT_PREFIX)"

# Required Homebrew dependencies
DEPS=("zlib" "opencv" "gsl" "cfitsio" "libomp" "libraw" "brotli" "qtdeclarative")
for dep in "${DEPS[@]}"; do
    DEP_PREFIX=$(brew --prefix "$dep" 2>/dev/null || echo "")
    if [ -z "$DEP_PREFIX" ] || [ ! -d "$DEP_PREFIX" ]; then
        if [ "$dep" == "opencv" ]; then
            echo "[WARNING] $dep not found. Install with: brew install $dep"
            echo "  NOTE: TStar is built WITHOUT OpenCV DNN to avoid external dependencies like OpenVINO"
        else
            echo "[WARNING] $dep not found. Install with: brew install $dep"
        fi
    else
        echo "  - $dep: OK"
    fi
done

# Optional Homebrew dependencies
for dep in "lz4" "zstd"; do
    DEP_PREFIX=$(brew --prefix "$dep" 2>/dev/null || echo "")
    if [ -z "$DEP_PREFIX" ] || [ ! -d "$DEP_PREFIX" ]; then
        echo "  - $dep: NOT FOUND (optional)"
    else
        echo "  - $dep: OK"
    fi
done

# -----------------------------------------------------------------------------
# STEP 2: Verify Python virtual environment
# -----------------------------------------------------------------------------

echo ""
echo "[STEP 2] Checking Python environment..."

PYTHON_VENV="$PROJECT_ROOT/deps/python_venv"
if [ ! -f "$PYTHON_VENV/bin/python3" ]; then
    echo "[INFO] Python venv not found. Running setup script..."
    if [ -f "$PROJECT_ROOT/setup_python_macos.sh" ]; then
        chmod +x "$PROJECT_ROOT/setup_python_macos.sh"
        "$PROJECT_ROOT/setup_python_macos.sh"
    else
        echo "[WARNING] setup_python_macos.sh not found. Python bridge may not work."
    fi
else
    echo "  - Python venv: OK"
fi

# -----------------------------------------------------------------------------
# STEP 3: CMake configuration
# -----------------------------------------------------------------------------

log_step 3 "Configuring CMake..."

ensure_dir "$BUILD_DIR"

# Optionally remove cached configuration
if [ $CLEAN_MODE -eq 1 ]; then
    echo "[INFO] Cleaning CMake cache..."
    safe_rm_rf "$BUILD_DIR/CMakeCache.txt"
    safe_rm_rf "$BUILD_DIR/CMakeFiles"
fi

if [ -f "$BUILD_DIR/CMakeCache.txt" ] && [ $CLEAN_MODE -eq 0 ]; then
    echo "[INFO] CMakeCache.txt found. Skipping configuration."
else
    "$CMAKE_CMD" -S . -B "$BUILD_DIR" \
        -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
        -DENABLE_LTO="$LTO_MODE"

    if [ $? -ne 0 ]; then
        echo "[ERROR] CMake configuration failed!"
        exit 1
    fi
fi

# -----------------------------------------------------------------------------
# STEP 4: Build
# -----------------------------------------------------------------------------

echo ""
echo "[STEP 4] Building TStar..."

NUM_CORES=$(sysctl -n hw.ncpu)
"$CMAKE_CMD" --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$NUM_CORES"

if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed!"
    exit 1
fi

# -----------------------------------------------------------------------------
# STEP 5: Verify build output
# -----------------------------------------------------------------------------

echo ""
echo "[STEP 5] Verifying build..."

APP_BUNDLE="$BUILD_DIR/TStar.app"
EXECUTABLE="$APP_BUNDLE/Contents/MacOS/TStar"

if [ -f "$EXECUTABLE" ]; then
    echo "  - TStar.app: OK"
else
    # Fallback: check for a non-bundle standalone binary
    if [ -f "$BUILD_DIR/TStar" ]; then
        echo "  - TStar (binary): OK"
        EXECUTABLE="$BUILD_DIR/TStar"
    else
        echo "[ERROR] Build output not found!"
        exit 1
    fi
fi

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------

echo ""
echo "==========================================="
echo " SUCCESS!"
echo " Executable: $EXECUTABLE"
echo "==========================================="
echo ""
echo "Next steps:"
echo "  1. Run: $EXECUTABLE"
echo "  2. Package: ./src/package_macos.sh"
echo "  3. Create DMG: ./src/build_installer_macos.sh"