#!/bin/bash
# =============================================================================
# TStar Distribution Packager for macOS
# Equivalent of package_dist.bat for Windows
# =============================================================================
# Creates a standalone .app bundle with all dependencies
# =============================================================================

set -e

# Check for silent mode
SILENT_MODE=0
if [ "$1" == "--silent" ]; then
    SILENT_MODE=1
fi

if [ $SILENT_MODE -eq 0 ]; then
    echo "==========================================="
    echo " TStar Distribution Packager (macOS)"
    echo "==========================================="
    echo ""
fi

# Move to project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
PROJECT_ROOT="$(pwd)"

BUILD_DIR="build"
DIST_DIR="dist/TStar.app"
APP_BUNDLE="$BUILD_DIR/TStar.app"
ERROR_COUNT=0

# --- Read version from changelog.txt ---
VERSION="1.0.0"
if [ -f "changelog.txt" ]; then
    VERSION=$(grep -E "^Version [0-9.]+" changelog.txt | head -1 | awk '{print $2}')
    if [ -z "$VERSION" ]; then
        VERSION="1.0.0"
    fi
fi
echo "[INFO] Packaging version: $VERSION"

# --- Verify build exists ---
echo ""
echo "[STEP 1] Verifying build..."

if [ ! -d "$APP_BUNDLE" ]; then
    echo "[ERROR] TStar.app not found in $BUILD_DIR"
    echo "Please run ./src/build_macos.sh first."
    exit 1
fi
echo "  - TStar.app: OK"

# --- Clean old dist ---
echo ""
echo "[STEP 2] Preparing distribution folder..."

if [ -d "dist" ]; then
    rm -rf "dist"
fi
mkdir -p "dist"

# --- Copy app bundle ---
echo ""
echo "[STEP 3] Copying app bundle..."

cp -R "$APP_BUNDLE" "$DIST_DIR"
echo "  - App bundle copied"

# --- Run macdeployqt ---
echo ""
echo "[STEP 4] Running macdeployqt..."

QT_PREFIX=$(brew --prefix qt@6 2>/dev/null || brew --prefix qt 2>/dev/null || echo "")

# Fallback to Cellar if opt symlink is broken (Dependency Harvester logic)
if [ ! -d "$QT_PREFIX/bin" ]; then
    POTENTIAL_QT=$(find /opt/homebrew/Cellar/qtbase -maxdepth 2 -name "bin" -type d 2>/dev/null | head -1)
    if [ -n "$POTENTIAL_QT" ]; then
        QT_PREFIX=$(dirname "$POTENTIAL_QT")
        echo "[INFO] Using physical Qt prefix: $QT_PREFIX"
    fi
fi

MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

if [ ! -f "$MACDEPLOYQT" ]; then
    # Try to find in PATH
    MACDEPLOYQT=$(which macdeployqt 2>/dev/null || echo "")
fi

if [ -f "$MACDEPLOYQT" ]; then
    # Run macdeployqt with Qt lib path and filter out rpath warnings
    # The rpath warnings are non-fatal and occur because some plugins reference
    # Qt frameworks that will be bundled. We filter them to keep output clean.
    # We also filter "no file at /opt/homebrew/opt" because macdeployqt is confused by
    # the broken symlinks, but we manually fix these dependencies in Step 5.
    "$MACDEPLOYQT" "$DIST_DIR" \
        -verbose=1 \
        -libpath="$QT_PREFIX/lib" \
        2>&1 | grep -v "Cannot resolve rpath" | grep -v "using QList" | grep -v "ERROR: no file at \"/opt/homebrew/opt" || true
    echo "  - Qt frameworks deployed"
else
    echo "[WARNING] macdeployqt not found. Qt frameworks not bundled."
    echo "  Install Qt6 with: brew install qt@6"
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# --- Copy Homebrew dylibs ---
echo ""
echo "[STEP 5] Copying Homebrew libraries..."

FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

copy_dylib() {
    local lib_name="$1"
    local brew_pkg="$2"
    
    local prefix=$(brew --prefix "$brew_pkg" 2>/dev/null || echo "")
    
    # Fallback to Cellar if opt symlink is broken
    if [ ! -d "$prefix/lib" ]; then
        CELLAR_PATH="/opt/homebrew/Cellar/$brew_pkg"
        if [ -d "$CELLAR_PATH" ]; then
             # Pick the first version found
             VERSION_PATH=$(find "$CELLAR_PATH" -maxdepth 1 -mindepth 1 -type d | head -1)
             if [ -n "$VERSION_PATH" ]; then
                 prefix="$VERSION_PATH"
                 echo "    (Using Cellar path for $lib_name)"
             fi
        fi
    fi

    if [ -n "$prefix" ] && [ -d "$prefix/lib" ]; then
        local dylib=$(find "$prefix/lib" -name "${lib_name}*.dylib" -type f | head -1)
        if [ -f "$dylib" ]; then
            cp "$dylib" "$FRAMEWORKS_DIR/"
            echo "  - $lib_name: OK"
            return 0
        fi
    fi
    echo "  - $lib_name: NOT FOUND"
    return 1
}

# Copy required dylibs
copy_dylib "libgsl" "gsl" || true
copy_dylib "libgslcblas" "gsl" || true
copy_dylib "libcfitsio" "cfitsio" || true
copy_dylib "liblz4" "lz4" || true
copy_dylib "libzstd" "zstd" || true
copy_dylib "libomp" "libomp" || true

# OpenCV (only required modules - dnn excluded to avoid libprotobuf conflict)
OPENCV_PREFIX=$(brew --prefix opencv 2>/dev/null || echo "")
if [ ! -d "$OPENCV_PREFIX/lib" ]; then
    # OpenCV Fallback
    OPENCV_PREFIX=$(find /opt/homebrew/Cellar/opencv -maxdepth 2 -name "lib" -type d 2>/dev/null | head -1)
    if [ -n "$OPENCV_PREFIX" ]; then OPENCV_PREFIX=$(dirname "$OPENCV_PREFIX"); fi
fi

if [ -n "$OPENCV_PREFIX" ] && [ -d "$OPENCV_PREFIX/lib" ]; then
    # Only copy the modules TStar actually uses to avoid protobuf conflicts
    # TStar uses: core, imgproc, imgcodecs, photo
    # Excluded: dnn (links protobuf), video, videoio, objdetect, ml, highgui
    OPENCV_MODULES="core imgproc imgcodecs photo"
    
    for module in $OPENCV_MODULES; do
        for dylib in "$OPENCV_PREFIX/lib"/libopencv_${module}*.dylib; do
            if [ -f "$dylib" ]; then
                cp "$dylib" "$FRAMEWORKS_DIR/" 2>/dev/null || true
            fi
        done
    done
    echo "  - OpenCV (core, imgproc, imgcodecs, photo): OK"
    echo "    (dnn and video modules excluded to avoid protobuf conflict)"
else
    echo "  - OpenCV: NOT FOUND"
fi


# --- Copy Python environment ---
echo ""
echo "[STEP 6] Copying Python environment..."

PYTHON_VENV="$PROJECT_ROOT/deps/python_venv"
RESOURCES_DIR="$DIST_DIR/Contents/Resources"

if [ -d "$PYTHON_VENV" ]; then
    cp -R "$PYTHON_VENV" "$RESOURCES_DIR/python_venv"
    echo "  - Python venv: OK"
else
    echo "[WARNING] Python venv not found. AI features may not work."
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# --- Copy scripts ---
echo ""
echo "[STEP 7] Copying scripts..."

if [ -d "src/scripts" ]; then
    mkdir -p "$RESOURCES_DIR/scripts"
    cp -R src/scripts/* "$RESOURCES_DIR/scripts/"
    echo "  - Scripts: OK"
else
    echo "[WARNING] Scripts folder not found."
fi

# --- Copy images ---
echo ""
echo "[STEP 8] Copying resources..."

if [ -d "src/images" ]; then
    mkdir -p "$RESOURCES_DIR/images"
    cp -R src/images/* "$RESOURCES_DIR/images/"
    echo "  - Images: OK"
fi

# --- Copy translations ---
if [ -d "$BUILD_DIR/translations" ]; then
    mkdir -p "$RESOURCES_DIR/translations"
    cp -R "$BUILD_DIR/translations"/* "$RESOURCES_DIR/translations/"
    echo "  - Translations: OK"
fi

# --- Fix library paths (install_name_tool) ---
echo ""
echo "[STEP 9] Fixing library paths..."

# This is a simplified fix - for production, use a proper tool like dylibbundler
EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
if [ -f "$EXECUTABLE" ]; then
    # Update rpath to look in Frameworks
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXECUTABLE" 2>/dev/null || true
    echo "  - Updated rpath"
fi

# --- Ad-hoc Code Signing ---
echo ""
echo "[STEP 9.5] Applying ad-hoc code signing..."
# Required for Apple Silicon and helps with "damaged app" errors
if command -v codesign &> /dev/null; then
    codesign --force --deep -s - "$DIST_DIR"
    echo "  - Ad-hoc signed: OK"
else
    echo "  - codesign not found (skip)"
fi

# --- Create README ---
echo ""
echo "[STEP 10] Creating README..."

cat > "dist/README.txt" << EOF
TStar v$VERSION - Astrophotography Processing Application
============================================================

INSTALLATION:
Drag TStar.app to your Applications folder.

FIRST RUN:
Right-click TStar.app and select "Open" to bypass Gatekeeper
on first launch (since the app is not notarized).

For external tools (Cosmic Clarity, StarNet, GraXpert):
- Configure paths in Settings menu

GitHub: https://github.com/Ft2801/TStar
EOF

echo "  - README.txt: OK"

# Copy changelog
cp "changelog.txt" "dist/" 2>/dev/null || true

# --- Summary ---
echo ""
echo "==========================================="
if [ $ERROR_COUNT -eq 0 ]; then
    echo " SUCCESS! Distribution ready"
else
    echo " COMPLETED WITH $ERROR_COUNT WARNING(S)"
fi
echo " Location: dist/TStar.app"
echo "==========================================="
echo ""
echo "Next step: ./src/build_installer_macos.sh"
