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

# Load utilities
if [ -f "$SCRIPT_DIR/macos_utils.sh" ]; then
    source "$SCRIPT_DIR/macos_utils.sh"
else
    echo "[ERROR] macos_utils.sh not found!"
    exit 1
fi

BUILD_DIR="build"
DIST_DIR="dist/TStar.app"
APP_BUNDLE="$BUILD_DIR/TStar.app"
ERROR_COUNT=0

# --- Read version ---
VERSION=$(get_version)
if [ $SILENT_MODE -eq 0 ]; then
    echo "[INFO] Packaging version: $VERSION"
fi

# --- Verify build exists ---
echo ""
log_step 1 "Verifying build..."

verify_dir "$APP_BUNDLE" "TStar.app" || {
    echo "Please run ./src/build_macos.sh first."
    exit 1
}
echo "  - TStar.app: OK"

# --- Clean old dist ---
echo ""
log_step 2 "Preparing distribution folder..."

safe_rm_rf "dist"
ensure_dir "dist"

# --- Copy app bundle ---
echo ""
echo "[STEP 3] Copying app bundle..."

cp -R "$APP_BUNDLE" "$DIST_DIR"
echo "  - App bundle copied"

# --- Run macdeployqt ---
echo ""
log_step 4 "Running macdeployqt..."

QT_PREFIX=$(detect_qt_prefix)
MACDEPLOYQT=$(find_macdeployqt "$QT_PREFIX")

if [ -f "$MACDEPLOYQT" ]; then
    # Run macdeployqt with Qt lib path and filter out rpath warnings
    # The rpath warnings are non-fatal and occur because some plugins reference
    # Qt frameworks that will be bundled. We filter them to keep output clean.
    
    # EXECUTABLE path was defined earlier or needs re-definition here if used
    EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
    TARGET_ARCH=$(detect_build_architecture "$EXECUTABLE")

    # Build libpath string with architecture-specific paths
    # SIMPLIFIED: Only pass main lib directories, avoid looping over opt subdirs
    # which confuses macdeployqt when symlinks are broken or libs are missing
    LIBPATH_ARGS="-libpath=$QT_PREFIX/lib"
    
    # Only add Homebrew paths matching the target architecture
    if [ "$TARGET_ARCH" == "arm64" ]; then
        # Apple Silicon: Add /opt/homebrew main lib
        if [ -d "/opt/homebrew/lib" ]; then
            LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/opt/homebrew/lib"
        fi
    else
        # Intel: Add /usr/local main lib
        if [ -d "/usr/local/lib" ]; then
             LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/usr/local/lib"
        fi
    fi
    
    # Run macdeployqt with expanded libpath
    "$MACDEPLOYQT" "$DIST_DIR" \
        -verbose=1 \
        $LIBPATH_ARGS \
        2>&1 | grep -v "Cannot resolve rpath" | grep -v "using QList" || true
    echo "  - Qt frameworks deployed"
else
    echo "[WARNING] macdeployqt not found. Qt frameworks not bundled."
    echo "  Install Qt6 with: brew install qt@6"
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# --- Copy Homebrew dylibs ---
echo ""
log_step 5 "Copying Homebrew libraries..."

# Detect build architecture from executable
EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
BUILD_ARCH=$(detect_build_architecture "$EXECUTABLE")
echo "  - Target architecture: $BUILD_ARCH"

FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"
ensure_dir "$FRAMEWORKS_DIR"

# Copy required dylibs using shared function (pass architecture)
copy_dylib "libgsl" "gsl" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libgslcblas" "gsl" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libcfitsio" "cfitsio" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "liblz4" "lz4" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libzstd" "zstd" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libomp" "libomp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libbrotlicommon" "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libbrotlidec" "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libraw" "libraw" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# OpenCV (only required modules - dnn and video excluded to avoid external dependencies)
OPENCV_PREFIX=$(brew --prefix opencv 2>/dev/null || echo "")
if [ ! -d "$OPENCV_PREFIX/lib" ]; then
    # OpenCV Fallback
    OPENCV_PREFIX=$(find /opt/homebrew/Cellar/opencv -maxdepth 2 -name "lib" -type d 2>/dev/null | head -1)
    if [ -n "$OPENCV_PREFIX" ]; then OPENCV_PREFIX=$(dirname "$OPENCV_PREFIX"); fi
fi

if [ -n "$OPENCV_PREFIX" ] && [ -d "$OPENCV_PREFIX/lib" ]; then
    # Only copy the modules TStar actually uses (matching CMakeLists.txt requirements)
    # INCLUDED: core, imgproc, imgcodecs, photo, features2d, calib3d
    # EXCLUDED: dnn (OpenVINO dependency), video, videoio, objdetect (not required)
    OPENCV_MODULES="core imgproc imgcodecs photo features2d calib3d"
    
    COPIED_COUNT=0
    for module in $OPENCV_MODULES; do
        for dylib in "$OPENCV_PREFIX/lib"/libopencv_${module}*.dylib; do
            if [ -f "$dylib" ]; then
                cp "$dylib" "$FRAMEWORKS_DIR/" 2>/dev/null || true
                COPIED_COUNT=$((COPIED_COUNT + 1))
            fi
        done
    done
    
    if [ $COPIED_COUNT -gt 0 ]; then
        echo "  - OpenCV (core, imgproc, imgcodecs, photo, features2d, calib3d): OK"
    else
        echo "  - OpenCV: NOT FOUND"
    fi
    echo "    (dnn, video, videoio, objdetect excluded to avoid external dependencies)"
    
    # Verify no OpenVINO or other problematic dependencies are bundled
    PROBLEMATIC_LIBS=$(find "$FRAMEWORKS_DIR" -name "*openvino*" -o -name "*protobuf*" 2>/dev/null | grep -v "/Applications" || true)
    if [ -n "$PROBLEMATIC_LIBS" ]; then
        echo "  [WARNING] Found external dependencies that should not be bundled:"
        echo "$PROBLEMATIC_LIBS" | xargs rm -f
        echo "    Removed problematic dylibs"
    fi
else
    echo "  - OpenCV: NOT FOUND"
fi


# --- Copy Python environment ---
echo ""
log_step 6 "Copying Python environment..."

PYTHON_VENV="$PROJECT_ROOT/deps/python_venv"
RESOURCES_DIR="$DIST_DIR/Contents/Resources"

verify_dir "$PYTHON_VENV" "Python venv" || {
    log_warning "Python venv not found. AI features may not work."
    ERROR_COUNT=$((ERROR_COUNT + 1))
}

if [ -d "$PYTHON_VENV" ]; then
    cp -R "$PYTHON_VENV" "$RESOURCES_DIR/python_venv"
    echo "  - Python venv: OK"
fi

# --- Copy scripts ---
echo ""
log_step 7 "Copying scripts..."

if [ -d "src/scripts" ]; then
    ensure_dir "$RESOURCES_DIR/scripts"
    cp -R src/scripts/* "$RESOURCES_DIR/scripts/"
    echo "  - Scripts: OK"
else
    log_warning "Scripts folder not found."
fi

# --- Copy images ---
echo ""
log_step 8 "Copying resources..."

if [ -d "src/images" ]; then
    ensure_dir "$RESOURCES_DIR/images"
    cp -R src/images/* "$RESOURCES_DIR/images/"
    echo "  - Images: OK"
fi

# --- Copy translations ---
if [ -d "$BUILD_DIR/translations" ]; then
    ensure_dir "$RESOURCES_DIR/translations"
    cp -R "$BUILD_DIR/translations"/* "$RESOURCES_DIR/translations/"
    echo "  - Translations: OK"
fi


# --- Resolve & Fix Libraries ---
echo ""
log_step 9 "Resolving and Fixing Libraries..."

EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"

# 1. Recursive copy of missing dependencies
echo "  - Recursively collecting dependencies..."
# Loop multiple times to handle deep chains
for i in {1..2}; do
    for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
        if [ -f "$dylib" ]; then
            copy_dylib_with_dependencies "$dylib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
        fi
    done
done

# 2. Fix dylib IDs and internal dependencies
echo "  - Fixing dylib IDs and paths..."
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        fix_dylib_id_and_deps "$dylib" "$FRAMEWORKS_DIR"
    fi
done

# 3. Fix executable dependencies
if [ -f "$EXECUTABLE" ]; then
    echo "  - Fixing executable dependencies..."
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXECUTABLE" 2>/dev/null || true
    fix_executable_deps "$EXECUTABLE" "$FRAMEWORKS_DIR"
fi

# --- Verify bundled dylibs dependencies ---
echo ""
echo "[STEP 9.1] Verifying bundled dependencies..."

MISSING_DEPS=0


# Final verification
echo "  - Checking for remaining missing dependencies..."
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        # Get all dependencies with @rpath reference
        UNRESOLVED=$(otool -L "$dylib" 2>/dev/null | grep "@rpath" | grep -v "^$dylib:" | grep -v "@rpath/Qt" | grep -v "@rpath/lib" || true)
        if [ -n "$UNRESOLVED" ]; then
            while IFS= read -r dep_line; do
                DEP_NAME=$(echo "$dep_line" | awk '{print $1}' | sed 's|@rpath/||')
                if [ -n "$DEP_NAME" ] && [ "$DEP_NAME" != "@rpath" ]; then
                    if [ ! -f "$FRAMEWORKS_DIR/$DEP_NAME" ]; then
                        echo "  [WARNING] Unresolved: $dylib -> $DEP_NAME"
                        MISSING_DEPS=$((MISSING_DEPS + 1))
                    fi
                fi
            done <<< "$UNRESOLVED"
        fi
    fi
done

if [ $MISSING_DEPS -gt 0 ]; then
    echo "  [WARNING] Found $MISSING_DEPS unresolved dependencies"
    echo "           Some AI features may not work. Check logs above."
else
    echo "  - All bundled dylib dependencies resolved"
fi

# --- Check for critical libraries ---
echo ""
echo "[STEP 9.2] Verifying critical libraries..."

if [ ! -f "$FRAMEWORKS_DIR/libraw.dylib" ]; then
    echo "  [ERROR] libraw.dylib NOT FOUND in bundle!"
    echo "         RAW image file support will NOT work!"
    echo "         Make sure: brew install libraw"
    ERROR_COUNT=$((ERROR_COUNT + 1))
else
    echo "  - libraw.dylib: OK"
fi

# --- Ad-hoc Code Signing ---
echo ""
log_step 9.5 "Applying ad-hoc code signing..."

check_command codesign && {
    codesign --force --deep -s - "$DIST_DIR"
    echo "  - Ad-hoc signed: OK"
} || {
    log_warning "codesign not found (skip)"
}

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
