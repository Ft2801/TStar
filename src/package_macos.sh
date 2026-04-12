#!/bin/bash
# =============================================================================
# TStar Distribution Packager for macOS
# =============================================================================
#
# Creates a standalone, redistributable .app bundle inside dist/ with all
# runtime dependencies (Qt frameworks, Homebrew dylibs, Python venv, scripts,
# data catalogs, ASTAP, images, translations) embedded and install-name-fixed
# so the application works on machines without Homebrew or developer tools.
#
# Equivalent of package_dist.bat on Windows.
#
# Usage:
#   ./src/package_macos.sh              -- interactive mode
#   ./src/package_macos.sh --silent     -- silent mode (for CI pipelines)
# =============================================================================

set -e

# =============================================================================
# Argument parsing
# =============================================================================

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

# =============================================================================
# Environment setup
# =============================================================================

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

# =============================================================================
# Configuration
# =============================================================================

BUILD_DIR="build"
DIST_DIR="dist/TStar.app"
APP_BUNDLE="$BUILD_DIR/TStar.app"
ERROR_COUNT=0

VERSION=$(get_version)
if [ $SILENT_MODE -eq 0 ]; then
    echo "[INFO] Packaging version: $VERSION"
fi

# =============================================================================
# STEP 1 -- Verify that the build output exists
# =============================================================================

echo ""
log_step 1 "Verifying build..."

verify_dir "$APP_BUNDLE" "TStar.app" || {
    echo "Please run ./src/build_macos.sh first."
    exit 1
}
echo "  - TStar.app: OK"

# =============================================================================
# STEP 2 -- Prepare a clean distribution folder
# =============================================================================

echo ""
log_step 2 "Preparing distribution folder..."

safe_rm_rf "dist"
ensure_dir "dist"

# =============================================================================
# STEP 3 -- Copy the app bundle into dist/
# =============================================================================

echo ""
echo "[STEP 3] Copying app bundle..."

cp -R "$APP_BUNDLE" "$DIST_DIR"
echo "  - App bundle copied"

# -- Generate application icon if missing -------------------------------------

ICON_RES_DIR="$DIST_DIR/Contents/Resources"
ICON_TARGET="$ICON_RES_DIR/TStar.icns"

if [ ! -f "$ICON_TARGET" ]; then
    ICON_SRC_PNG="src/images/Logo.png"
    if [ -f "$ICON_SRC_PNG" ] \
       && command -v sips >/dev/null 2>&1 \
       && command -v iconutil >/dev/null 2>&1; then

        ICONSET_DIR="$(mktemp -d)/TStar.iconset"
        mkdir -p "$ICONSET_DIR"

        # Generate all required icon sizes from the source PNG
        sips -z   16   16 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_16x16.png"       >/dev/null 2>&1 || true
        sips -z   32   32 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_16x16@2x.png"   >/dev/null 2>&1 || true
        sips -z   32   32 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_32x32.png"       >/dev/null 2>&1 || true
        sips -z   64   64 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_32x32@2x.png"   >/dev/null 2>&1 || true
        sips -z  128  128 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_128x128.png"     >/dev/null 2>&1 || true
        sips -z  256  256 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_128x128@2x.png" >/dev/null 2>&1 || true
        sips -z  256  256 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_256x256.png"     >/dev/null 2>&1 || true
        sips -z  512  512 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_256x256@2x.png" >/dev/null 2>&1 || true
        sips -z  512  512 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_512x512.png"     >/dev/null 2>&1 || true
        sips -z 1024 1024 "$ICON_SRC_PNG" --out "$ICONSET_DIR/icon_512x512@2x.png" >/dev/null 2>&1 || true

        ensure_dir "$ICON_RES_DIR"
        if iconutil -c icns "$ICONSET_DIR" -o "$ICON_TARGET" >/dev/null 2>&1; then
            echo "  - App icon generated: Contents/Resources/TStar.icns"
        else
            echo "  [WARNING] Failed to generate TStar.icns from Logo.png"
        fi

        rm -rf "$(dirname "$ICONSET_DIR")"
    else
        echo "  [WARNING] Skipping app icon generation (missing Logo.png, sips, or iconutil)"
    fi
fi

# =============================================================================
# STEP 4 -- Deploy Qt frameworks via macdeployqt
# =============================================================================

echo ""
log_step 4 "Running macdeployqt..."

QT_PREFIX=$(detect_qt_prefix)
MACDEPLOYQT=$(find_macdeployqt "$QT_PREFIX")

if [ -f "$MACDEPLOYQT" ]; then
    EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
    TARGET_ARCH=$(detect_build_architecture "$EXECUTABLE")

    LIBPATH_ARGS="-libpath=$QT_PREFIX/lib"

    # Add architecture-appropriate Homebrew lib path
    if [ "$TARGET_ARCH" == "arm64" ]; then
        if [ -d "/opt/homebrew/lib" ]; then
            LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/opt/homebrew/lib"
        fi
    else
        if [ -d "/usr/local/lib" ]; then
            LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/usr/local/lib"
        fi
    fi

    # Run macdeployqt, filtering harmless warnings
    "$MACDEPLOYQT" "$DIST_DIR" \
        -verbose=1 \
        $LIBPATH_ARGS \
        2>&1 | grep -v "Cannot resolve rpath" \
             | grep -v "using QList" \
             | grep -v "No such file or directory" || true

    echo "  - Qt frameworks deployed"
else
    echo "[WARNING] macdeployqt not found. Qt frameworks not bundled."
    echo "  Install Qt6 with: brew install qt@6"
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# =============================================================================
# STEP 5 -- Copy Homebrew dynamic libraries into Frameworks
# =============================================================================

echo ""
log_step 5 "Copying Homebrew libraries..."

EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
BUILD_ARCH=$(detect_build_architecture "$EXECUTABLE")
echo "  - Target architecture: $BUILD_ARCH"

FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"
ensure_dir "$FRAMEWORKS_DIR"

# -- ZLIB (with multi-strategy fallback) --------------------------------------

copy_dylib "libz.1.dylib" "zlib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libz.dylib"   "zlib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || {
    ZLIB_FOUND=0

    for ZLIB_PREFIX in /opt/homebrew/opt/zlib /usr/local/opt/zlib /opt/homebrew /usr/local; do
        for ZLIB_LIB in "$ZLIB_PREFIX/lib/libz.1.dylib" "$ZLIB_PREFIX/lib/libz.dylib"; do
            if [ -f "$ZLIB_LIB" ]; then
                cp -L "$ZLIB_LIB" "$FRAMEWORKS_DIR/" 2>/dev/null \
                    && ZLIB_FOUND=1 \
                    && echo "  - ZLIB: OK (from $ZLIB_PREFIX)" \
                    && break 2 || true
            fi
        done
    done

    # Last resort: system libz (may be blocked by SIP on newer macOS)
    if [ $ZLIB_FOUND -eq 0 ]; then
        if [ -f "/usr/lib/libz.1.dylib" ]; then
            cp "/usr/lib/libz.1.dylib" "$FRAMEWORKS_DIR/libz.1.dylib" 2>/dev/null || true
        elif [ -f "/usr/lib/libz.dylib" ]; then
            cp "/usr/lib/libz.dylib" "$FRAMEWORKS_DIR/libz.dylib" 2>/dev/null || true
        fi
    fi
}

# -- Core scientific and utility libraries ------------------------------------

copy_dylib "libgsl"        "gsl"     "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libgslcblas"   "gsl"     "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libcfitsio"    "cfitsio" "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "liblz4"        "lz4"     "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libzstd"       "zstd"    "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libomp"        "libomp"  "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libbrotlicommon" "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libbrotlidec"   "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libraw"        "libraw"  "$FRAMEWORKS_DIR" "$BUILD_ARCH"
copy_dylib "libmd4c"       "md4c"    "$FRAMEWORKS_DIR" "$BUILD_ARCH"

# -- Linear algebra (OpenBLAS) ------------------------------------------------

copy_dylib "libopenblas.0" "openblas" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libopenblas"   "openblas" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- GCC / Fortran runtimes (required by OpenBLAS, GSL, etc.) -----------------

copy_dylib "libgcc_s.1.1" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libgcc_s.1"   "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libgfortran.5" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libgfortran"   "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- Image format libraries ---------------------------------------------------

copy_dylib "libpng16"  "libpng"  "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libpng"    "libpng"  "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libjpeg"   "jpeg"          "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libjpeg.9" "libjpeg-turbo" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libtiff"       "libtiff" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libtiff.6"     "libtiff" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libwebp"       "libwebp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libwebpdemux"  "libwebp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- Font and text rendering --------------------------------------------------

copy_dylib "libfreetype" "freetype" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libharfbuzz" "harfbuzz" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- Colour management and additional math libraries --------------------------

copy_dylib "liblcms2.2" "little-cms2" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "liblapack"  "lapack"      "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libjasper"  "jasper"      "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- Transitive dependencies frequently missed by macdeployqt -----------------

copy_dylib "libdbus-1.3"  "dbus"    "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libtbb.12" "tbb" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libtbb"    "tbb" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libopenjp2.7" "openjpeg" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libopenjp2"   "openjpeg" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# -- OpenCV modules -----------------------------------------------------------

OPENCV_PREFIX=$(brew --prefix opencv 2>/dev/null || echo "")
if [ ! -d "$OPENCV_PREFIX/lib" ]; then
    for base in /opt/homebrew /usr/local; do
        if [ -d "$base/Cellar/opencv" ]; then
            OPENCV_PREFIX=$(find "$base/Cellar/opencv" -maxdepth 2 -name "lib" -type d 2>/dev/null \
                            | head -1)
            if [ -n "$OPENCV_PREFIX" ]; then
                OPENCV_PREFIX=$(dirname "$OPENCV_PREFIX")
                break
            fi
        fi
    done
fi

if [ -n "$OPENCV_PREFIX" ] && [ -d "$OPENCV_PREFIX/lib" ]; then
    OPENCV_MODULES="core imgproc imgcodecs photo features2d calib3d video videoio objdetect flann"
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

    # Remove any problematic transitive dependencies that should not be bundled
    PROBLEMATIC_LIBS=$(find "$FRAMEWORKS_DIR" -name "*openvino*" -o -name "*protobuf*" \
                       2>/dev/null | grep -v "/Applications" || true)
    if [ -n "$PROBLEMATIC_LIBS" ]; then
        echo "  [WARNING] Found external dependencies that should not be bundled:"
        echo "$PROBLEMATIC_LIBS" | xargs rm -f
        echo "    Removed problematic dylibs"
    fi
else
    echo "  - OpenCV: NOT FOUND"
fi

# =============================================================================
# STEP 6 -- Copy Python virtual environment
# =============================================================================

echo ""
log_step 6 "Copying Python environment..."

PYTHON_VENV="$PROJECT_ROOT/deps/python_venv"
RESOURCES_DIR="$DIST_DIR/Contents/Resources"

verify_dir "$PYTHON_VENV" "Python venv" || {
    log_warning "Python venv not found. AI features may not work."
    ERROR_COUNT=$((ERROR_COUNT + 1))
}

if [ -d "$PYTHON_VENV" ]; then

    # Dereference all symlinks so the venv is fully self-contained
    if command -v rsync &> /dev/null; then
        rsync -aL "$PYTHON_VENV/" "$RESOURCES_DIR/python_venv/"
    else
        cp -RL "$PYTHON_VENV" "$RESOURCES_DIR/python_venv"
    fi
    echo "  - Python venv: OK"

    # -- Bundle Python.framework so the copied python3 binary can find it -----
    #
    # Homebrew Python is a framework build: the python3 binary links against
    # Python.framework/Versions/X.Y/Python at an absolute Cellar path that
    # does NOT exist on the end-user's machine, causing a dyld crash.
    # Fix: copy the framework shared library into Contents/Frameworks/ and
    # rewrite the reference inside the bundled python3 binary.

    BUNDLED_PYTHON="$RESOURCES_DIR/python_venv/bin/python3"

    if [ -f "$BUNDLED_PYTHON" ]; then
        FRAMEWORK_LINK=$(otool -L "$BUNDLED_PYTHON" 2>/dev/null \
            | grep "Python.framework" | awk '{print $1}' | head -1 || true)

        # Skip if the reference is already relative (@loader_path / @rpath)
        if echo "$FRAMEWORK_LINK" | grep -q "^@"; then
            echo "  - python3: Python.framework reference already relative (OK)"
            FRAMEWORK_LINK=""
        fi

        # Search for the framework library if the original Cellar path is stale
        if [ -n "$FRAMEWORK_LINK" ] && [ ! -f "$FRAMEWORK_LINK" ]; then
            FRAMEWORK_VERSION_SEARCH=$(echo "$FRAMEWORK_LINK" \
                | grep -oE 'Versions/[^/]+' | head -1 | cut -d/ -f2)
            echo "  [INFO] Python.framework not at original path, searching..."

            FOUND_FW=""
            for SEARCH_BASE in /opt/homebrew /usr/local; do
                FOUND_FW=$(find "$SEARCH_BASE" -name "Python" \
                    -path "*/Python.framework/Versions/$FRAMEWORK_VERSION_SEARCH/Python" \
                    2>/dev/null | head -1 || true)
                [ -n "$FOUND_FW" ] && break
            done

            if [ -n "$FOUND_FW" ]; then
                echo "  [INFO] Found Python.framework at: $FOUND_FW"
                FRAMEWORK_LINK="$FOUND_FW"
            else
                echo "  [ERROR] Python.framework not found anywhere! python3 will NOT work."
                echo "          Rebuild the venv: ./setup_python_macos.sh"
                ERROR_COUNT=$((ERROR_COUNT + 1))
                FRAMEWORK_LINK=""
            fi
        fi

        # Copy the framework and patch the bundled python3 binary
        if [ -n "$FRAMEWORK_LINK" ] && [ -f "$FRAMEWORK_LINK" ]; then
            FRAMEWORK_VERSION=$(echo "$FRAMEWORK_LINK" \
                | grep -oE 'Versions/[^/]+' | head -1 | cut -d/ -f2)

            PYTHON_FW_DEST="$FRAMEWORKS_DIR/Python.framework/Versions/$FRAMEWORK_VERSION"
            mkdir -p "$PYTHON_FW_DEST"
            cp "$FRAMEWORK_LINK" "$PYTHON_FW_DEST/Python"

            # Copy stdlib, headers, and bin so Python can find its standard library
            FRAMEWORK_ROOT=$(dirname "$FRAMEWORK_LINK")
            for sub in lib bin include; do
                [ -d "$FRAMEWORK_ROOT/$sub" ] \
                    && cp -RL "$FRAMEWORK_ROOT/$sub" "$PYTHON_FW_DEST/" || true
            done

            # Fix the framework library's install name
            install_name_tool -id \
                "@rpath/Python.framework/Versions/$FRAMEWORK_VERSION/Python" \
                "$PYTHON_FW_DEST/Python" 2>/dev/null || true

            # Rewrite the absolute Cellar path inside the python3 binary
            NEW_FW_PATH="@loader_path/../../../Frameworks/Python.framework/Versions/$FRAMEWORK_VERSION/Python"
            install_name_tool -change "$FRAMEWORK_LINK" "$NEW_FW_PATH" \
                "$BUNDLED_PYTHON" 2>/dev/null || true

            # Add rpath so .so extensions can find libraries in Contents/Frameworks
            install_name_tool -add_rpath "@loader_path/../../../Frameworks" \
                "$BUNDLED_PYTHON" 2>/dev/null || true

            # Update pyvenv.cfg with bundle-relative paths
            CFG_FILE="$RESOURCES_DIR/python_venv/pyvenv.cfg"
            if [ -f "$CFG_FILE" ]; then
                sed -i '' 's|^home = .*|home = ../../Frameworks/Python.framework/Versions/Current/bin|' "$CFG_FILE"
                sed -i '' 's|^executable = .*|executable = ../../Frameworks/Python.framework/Versions/Current/bin/python3|' "$CFG_FILE"
            fi

            # Create a valid macOS framework structure (required by codesign)
            mkdir -p "$PYTHON_FW_DEST/Resources"
            cat > "$PYTHON_FW_DEST/Resources/Info.plist" << PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>org.python.python</string>
    <key>CFBundleName</key>
    <string>Python</string>
    <key>CFBundleVersion</key>
    <string>${FRAMEWORK_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${FRAMEWORK_VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleExecutable</key>
    <string>Python</string>
</dict>
</plist>
PLIST_EOF

            # Create framework symlinks
            ln -sf "$FRAMEWORK_VERSION" \
                "$FRAMEWORKS_DIR/Python.framework/Versions/Current" 2>/dev/null || true
            ln -sf "Versions/Current/Python" \
                "$FRAMEWORKS_DIR/Python.framework/Python" 2>/dev/null || true
            ln -sf "Versions/Current/Resources" \
                "$FRAMEWORKS_DIR/Python.framework/Resources" 2>/dev/null || true

            # Copy Python.app stub (required to avoid posix_spawn errors)
            if [ -d "$FRAMEWORK_ROOT/Resources/Python.app" ]; then
                cp -R "$FRAMEWORK_ROOT/Resources/Python.app" "$PYTHON_FW_DEST/Resources/"

                APP_PYTHON="$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS/Python"
                if [ -f "$APP_PYTHON" ]; then
                    install_name_tool -change "$FRAMEWORK_LINK" \
                        "@executable_path/../../../../Python" "$APP_PYTHON" 2>/dev/null || true
                    install_name_tool -add_rpath "@executable_path/../../../../" \
                        "$APP_PYTHON" 2>/dev/null || true
                fi
            else
                # Fallback: create a minimal Python.app stub
                mkdir -p "$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS"
                ln -sf "../../../../../../../Resources/python_venv/bin/python3" \
                    "$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS/Python" 2>/dev/null || true
            fi

            echo "  - Python.framework/$FRAMEWORK_VERSION: bundled & patched in python3"
        fi

        # -- Rewrite Homebrew absolute paths in Python extension modules -------
        #
        # .so and .dylib files under the venv reference Homebrew-installed
        # libraries (e.g. libopenblas, libgfortran) at absolute paths that do
        # not exist on a clean target Mac.  Rewriting to @rpath/<lib> allows
        # dyld to find them via the rpath chain anchored on python3.

        echo "  - Rewriting Homebrew paths in Python extension modules (.so)..."
        SO_FIXED=0
        SO_MISSING=0

        find "$RESOURCES_DIR/python_venv" "$FRAMEWORKS_DIR/Python.framework" \
             \( -name "*.so" -o -name "*.dylib" \) | while read -r so_file; do

            chmod +w "$so_file" 2>/dev/null || true
            deps=$(otool -L "$so_file" 2>/dev/null \
                   | grep -v "^$so_file:" | awk '{print $1}')

            for dep in $deps; do
                if echo "$dep" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
                    dep_name=$(basename "$dep")
                    install_name_tool -change "$dep" "@rpath/$dep_name" \
                        "$so_file" 2>/dev/null || true
                    SO_FIXED=$((SO_FIXED + 1))

                    # Copy the referenced dylib into Frameworks if not already present
                    if [ ! -f "$FRAMEWORKS_DIR/$dep_name" ] && [ -f "$dep" ]; then
                        cp "$dep" "$FRAMEWORKS_DIR/$dep_name" 2>/dev/null || true
                    fi
                fi
            done
        done
        echo "  - Extension module path fix: done"
    fi
fi

# =============================================================================
# STEP 7 -- Copy application scripts
# =============================================================================

echo ""
log_step 7 "Copying scripts..."

# 7.1 Python bridge scripts (src/scripts)
if [ -d "src/scripts" ]; then
    ensure_dir "$RESOURCES_DIR/scripts"
    cp -R src/scripts/* "$RESOURCES_DIR/scripts/"
    echo "  - Python bridge scripts: OK"
else
    log_warning "Python bridge scripts folder (src/scripts) not found."
fi

# 7.2 TStar scripts (scripts)
if [ -d "scripts" ]; then
    ensure_dir "$RESOURCES_DIR/scripts"
    cp -R scripts/* "$RESOURCES_DIR/scripts/"
    echo "  - TStar scripts (.tss): OK"
else
    log_warning "TStar scripts folder (scripts) not found."
fi

# 7.2.1 UI scripts (JS-Scripts)
if [ -d "JS-Scripts" ]; then
    ensure_dir "$RESOURCES_DIR/JS-Scripts"
    cp -R JS-Scripts/* "$RESOURCES_DIR/JS-Scripts/"
    echo "  - JS-Scripts folder (JS): OK"
else
    log_warning "JS-Scripts folder not found."
fi

# 7.3 Data catalogs and SPCC resources
echo ""
echo "[STEP 7.3] Copying Data Catalogs and SPCC Resources..."
if [ -d "data" ]; then
    ensure_dir "$RESOURCES_DIR/data"
    cp -R data/* "$RESOURCES_DIR/data/"
    echo "  - data folder (catalogs, SPCC): OK"
else
    log_warning "Data folder (data) not found."
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# 7.4 Minigame resources
echo ""
echo "[STEP 7.4] Copying Minigame Resources..."
if [ -d "src/minigame" ]; then
    ensure_dir "$RESOURCES_DIR/minigame"
    cp -R src/minigame/* "$RESOURCES_DIR/minigame/"
    echo "  - minigame folder: OK"
else
    log_warning "Minigame folder (src/minigame) not found (optional)."
fi

# =============================================================================
# STEP 7.5 -- Bundle ASTAP plate-solver (optional)
# =============================================================================

echo ""
echo "[STEP 7.5] Copying ASTAP CLI (optional)..."

ASTAP_DST_DIR="$RESOURCES_DIR/deps"
ensure_dir "$ASTAP_DST_DIR"

ASTAP_SRC=""

# Check multiple known ASTAP locations
if [ -x "$DIST_DIR/Contents/MacOS/astap" ]; then
    ASTAP_SRC="$DIST_DIR/Contents/MacOS/astap"
    echo "  - Using pre-bundled ASTAP from Contents/MacOS"
elif [ -x "/Applications/ASTAP.app/Contents/MacOS/astap" ]; then
    ASTAP_SRC="/Applications/ASTAP.app/Contents/MacOS/astap"
elif [ -x "/usr/local/bin/astap" ]; then
    ASTAP_SRC="/usr/local/bin/astap"
elif [ -x "/opt/homebrew/bin/astap" ]; then
    ASTAP_SRC="/opt/homebrew/bin/astap"
fi

if [ -n "$ASTAP_SRC" ]; then
    cp "$ASTAP_SRC" "$ASTAP_DST_DIR/astap"
    chmod +x "$ASTAP_DST_DIR/astap" 2>/dev/null || true
    echo "  - astap: OK ($(basename "$ASTAP_SRC"))"

    # Copy star databases if available
    if [ -d "/Applications/ASTAP.app/Contents/Resources/Databases" ]; then
        cp -R "/Applications/ASTAP.app/Contents/Resources/Databases" \
            "$ASTAP_DST_DIR/" 2>/dev/null || true
        if [ -d "$ASTAP_DST_DIR/Databases" ]; then
            echo "  - ASTAP Databases: OK"
        else
            echo "  [WARNING] ASTAP Databases: copy may have failed"
        fi
    fi
else
    echo "  - [WARNING] ASTAP not found on this Mac, skipping"
fi

# =============================================================================
# STEP 8 -- Copy resource assets
# =============================================================================

echo ""
log_step 8 "Copying resources..."

# Images
if [ -d "src/images" ]; then
    ensure_dir "$RESOURCES_DIR/images"
    cp -R src/images/* "$RESOURCES_DIR/images/"
    echo "  - Images: OK"
fi

# Translations
if [ -d "$BUILD_DIR/translations" ]; then
    ensure_dir "$RESOURCES_DIR/translations"
    cp -R "$BUILD_DIR/translations"/* "$RESOURCES_DIR/translations/"
    echo "  - Translations: OK"
fi

# =============================================================================
# STEP 9 -- Resolve and fix library install names
# =============================================================================

echo ""
log_step 9 "Resolving and Fixing Libraries..."

EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"

# -- 9a. Recursively collect missing transitive dependencies -------------------

echo "  - Recursively collecting dependencies..."
for i in {1..3}; do

    # Scan all dylibs in Frameworks
    for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
        if [ -f "$dylib" ]; then
            copy_dylib_with_dependencies "$dylib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
        fi
    done

    # Scan Qt plugins
    if [ -d "$DIST_DIR/Contents/PlugIns" ]; then
        find "$DIST_DIR/Contents/PlugIns" -name "*.dylib" | while read -r plugin_path; do
            if [ -f "$plugin_path" ]; then
                copy_dylib_with_dependencies "$plugin_path" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
            fi
        done
    fi

    # Scan framework binaries
    for framework in "$FRAMEWORKS_DIR"/*.framework; do
        if [ -d "$framework" ]; then
            framework_name=$(basename "$framework" .framework)

            # Standard Qt layout: Versions/A/<Name>
            if [ -f "$framework/Versions/A/$framework_name" ]; then
                copy_dylib_with_dependencies \
                    "$framework/Versions/A/$framework_name" \
                    "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
            fi

            # Non-standard layout (e.g. Python.framework: Versions/3.x/Python)
            if [ -d "$framework/Versions" ]; then
                for ver_dir in "$framework/Versions"/*/; do
                    ver_name=$(basename "$ver_dir")
                    [ "$ver_name" = "A" ] && continue
                    ver_binary="$ver_dir$framework_name"
                    if [ -f "$ver_binary" ]; then
                        copy_dylib_with_dependencies \
                            "$ver_binary" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
                    fi
                done
            fi
        fi
    done
done

# -- 9b. Fix dylib IDs and internal dependency paths --------------------------

echo "  - Fixing dylib IDs and paths..."
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        fix_dylib_id_and_deps "$dylib" "$FRAMEWORKS_DIR"
    fi
done

# -- 9c. Fix the main executable's dependency paths ---------------------------

if [ -f "$EXECUTABLE" ]; then
    echo "  - Fixing executable dependencies..."
    install_name_tool -add_rpath "@executable_path/../Frameworks" \
        "$EXECUTABLE" 2>/dev/null || true
    fix_executable_deps "$EXECUTABLE" "$FRAMEWORKS_DIR"
fi

# -- 9d. Final sweep: rewrite all remaining Homebrew absolute paths -----------

echo "  - Final sweep: rewriting any remaining Homebrew paths in executable and dylibs..."
rewrite_homebrew_paths "$EXECUTABLE"

for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        rewrite_homebrew_paths "$dylib"
    fi
done

# -- 9e. Fix dependencies inside .framework bundles ---------------------------

echo "  - Fixing dependencies inside .framework bundles..."
for framework in "$FRAMEWORKS_DIR"/*.framework; do
    if [ -d "$framework" ]; then
        framework_name=$(basename "$framework" .framework)

        # Standard Qt layout
        framework_binary="$framework/Versions/A/$framework_name"
        if [ -f "$framework_binary" ]; then
            rewrite_homebrew_paths "$framework_binary"
        fi

        # Non-standard version directories
        if [ -d "$framework/Versions" ]; then
            for ver_dir in "$framework/Versions"/*/; do
                ver_name=$(basename "$ver_dir")
                [ "$ver_name" = "A" ] && continue
                ver_binary="$ver_dir$framework_name"
                if [ -f "$ver_binary" ]; then
                    rewrite_homebrew_paths "$ver_binary"
                fi
            done
        fi
    fi
done

# -- 9f. Fix dependencies inside Qt plugins ----------------------------------

echo "  - Fixing dependencies inside Qt Plugins..."
PLUGINS_DIR="$DIST_DIR/Contents/PlugIns"
if [ -d "$PLUGINS_DIR" ]; then
    find "$PLUGINS_DIR" -name "*.dylib" | while read -r plugin_path; do
        if [ -f "$plugin_path" ]; then
            rewrite_homebrew_paths "$plugin_path"
            install_name_tool -add_rpath "@executable_path/../Frameworks" \
                "$plugin_path" 2>/dev/null || true
        fi
    done
fi

# =============================================================================
# STEP 9.1 -- Verify bundled dependency integrity
# =============================================================================

echo ""
echo "[STEP 9.1] Verifying bundled dependencies..."

MISSING_DEPS=0
HOMEBREW_REFS=0

# Scans a single binary for unresolved @rpath references and leftover
# absolute Homebrew paths.
verify_binary_file() {
    local bin_file="$1"

    # Check for unresolved @rpath references (excluding Qt frameworks)
    UNRESOLVED=$(otool -L "$bin_file" 2>/dev/null \
        | grep "@rpath" \
        | grep -v "^$bin_file:" \
        | grep -v "@rpath/Qt" || true)

    if [ -n "$UNRESOLVED" ]; then
        while IFS= read -r dep_line; do
            DEP_NAME=$(echo "$dep_line" | awk '{print $1}' | sed 's|@rpath/||')
            if [ -n "$DEP_NAME" ] && [ "$DEP_NAME" != "@rpath" ]; then
                if [ ! -f "$FRAMEWORKS_DIR/$DEP_NAME" ]; then
                    echo "  [WARNING] Unresolved @rpath: $(basename "$bin_file") -> $DEP_NAME"
                    MISSING_DEPS=$((MISSING_DEPS + 1))
                fi
            fi
        done <<< "$UNRESOLVED"
    fi

    # Check for absolute Homebrew paths
    BREW_REFS=$(otool -L "$bin_file" 2>/dev/null \
        | grep -v "^$bin_file:" \
        | awk '{print $1}' \
        | grep -E "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))" || true)

    if [ -n "$BREW_REFS" ]; then
        while IFS= read -r brew_ref; do
            echo "  [WARNING] Absolute Homebrew path in $(basename "$bin_file"): $brew_ref"
            HOMEBREW_REFS=$((HOMEBREW_REFS + 1))
        done <<< "$BREW_REFS"
    fi
}

# Verify all bundled dylibs
echo "  - Checking for unresolved @rpath references..."
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        verify_binary_file "$dylib"
    fi
done

# Verify plugins
if [ -d "$DIST_DIR/Contents/PlugIns" ]; then
    find "$DIST_DIR/Contents/PlugIns" -name "*.dylib" | while read -r plugin_path; do
        if [ -f "$plugin_path" ]; then
            verify_binary_file "$plugin_path"
        fi
    done
fi

# Verify framework binaries
for framework in "$FRAMEWORKS_DIR"/*.framework; do
    if [ -d "$framework" ]; then
        framework_name=$(basename "$framework" .framework)

        if [ -f "$framework/Versions/A/$framework_name" ]; then
            verify_binary_file "$framework/Versions/A/$framework_name"
        fi

        if [ -d "$framework/Versions" ]; then
            for ver_dir in "$framework/Versions"/*/; do
                ver_name=$(basename "$ver_dir")
                [ "$ver_name" = "A" ] && continue
                ver_binary="$ver_dir$framework_name"
                if [ -f "$ver_binary" ]; then
                    verify_binary_file "$ver_binary"
                fi
            done
        fi
    fi
done

# Verify the main executable
if [ -f "$EXECUTABLE" ]; then
    EXEC_BREW_REFS=$(otool -L "$EXECUTABLE" 2>/dev/null \
        | grep -v "^$EXECUTABLE:" \
        | awk '{print $1}' \
        | grep -E "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))" || true)

    if [ -n "$EXEC_BREW_REFS" ]; then
        while IFS= read -r brew_ref; do
            echo "  [WARNING] Absolute Homebrew path in TStar executable: $brew_ref"
            HOMEBREW_REFS=$((HOMEBREW_REFS + 1))
        done <<< "$EXEC_BREW_REFS"
    fi
fi

# Report verification results
if [ $MISSING_DEPS -gt 0 ]; then
    echo "  [WARNING] Found $MISSING_DEPS unresolved @rpath dependencies"
else
    echo "  - All @rpath dylib dependencies resolved"
fi

if [ $HOMEBREW_REFS -gt 0 ]; then
    echo "  [WARNING] Found $HOMEBREW_REFS absolute Homebrew path(s) remaining!"
    echo "            App may NOT work on systems without Homebrew."
else
    echo "  - No absolute Homebrew paths detected (portable)"
fi

# =============================================================================
# STEP 9.2 -- Verify critical libraries are present in the bundle
# =============================================================================

echo ""
echo "[STEP 9.2] Verifying critical libraries..."

# LibRaw (RAW image support)
if [ ! -f "$FRAMEWORKS_DIR/libraw.dylib" ]; then
    echo "  [ERROR] libraw.dylib NOT FOUND in bundle!"
    echo "          RAW image file support will NOT work!"
    echo "          Make sure: brew install libraw"
    ERROR_COUNT=$((ERROR_COUNT + 1))
else
    echo "  - libraw.dylib: OK"
fi

# md4c (Qt markdown support)
if [ ! -f "$FRAMEWORKS_DIR/libmd4c.0.dylib" ]; then
    echo "  [ERROR] libmd4c.0.dylib NOT FOUND in bundle!"
    echo "          Qt markdown support will NOT work!"
    echo "          Make sure: brew install md4c"
    ERROR_COUNT=$((ERROR_COUNT + 1))
else
    echo "  - libmd4c.0.dylib: OK"
fi

# ZLIB (critical -- app will not start without it)
ZLIB_FOUND=0
for zlib_name in libz.dylib libz.1.dylib libz libz.1; do
    if [ -f "$FRAMEWORKS_DIR/$zlib_name" ]; then
        ZLIB_FOUND=1
        break
    fi
done

if [ $ZLIB_FOUND -eq 0 ]; then
    echo "  [CRITICAL ERROR] ZLIB NOT FOUND in bundle!"
    echo "                   App will NOT work without ZLIB!"
    echo "                   Make sure: brew install zlib"
    ERROR_COUNT=$((ERROR_COUNT + 1))
else
    echo "  - ZLIB: OK"
fi

# Image format libraries (non-critical but important)
for imglib in libpng libjpeg libtiff libwebp; do
    if find "$FRAMEWORKS_DIR" -name "$imglib*" 2>/dev/null | head -1 | grep -q .; then
        echo "  - $imglib: OK"
    else
        echo "  [WARNING] $imglib NOT FOUND - some image formats may fail"
    fi
done

# =============================================================================
# STEP 9.4 -- Verify the bundled Python interpreter actually starts
# =============================================================================

echo ""
log_step "9.4" "Verifying bundled Python..."

BUNDLED_PY_CHECK="$DIST_DIR/Contents/Resources/python_venv/bin/python3"
FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"

if [ -f "$BUNDLED_PY_CHECK" ]; then

    # Test 1: Basic interpreter startup
    TEST_OUT=$("$BUNDLED_PY_CHECK" -c "import sys; print('OK')" 2>&1) || true
    if [ "$TEST_OUT" = "OK" ]; then
        echo "  - Bundled python3: OK"
    else
        echo "  [ERROR] Bundled python3 FAILED to start!"
        echo "          Error output:"
        "$BUNDLED_PY_CHECK" --version 2>&1 || true
        echo "          --- DEBUG INFO ---"
        echo "          otool -L of python binary:"
        otool -L "$BUNDLED_PY_CHECK" | sed 's/^/          /'
        echo "          otool -L of Python framework:"
        PYTHON_FW_SO="$FRAMEWORKS_DIR/Python.framework/Versions/Current/Python"
        [ -f "$PYTHON_FW_SO" ] && otool -L "$PYTHON_FW_SO" | sed 's/^/          /'
        echo "          python3 path resolution debug:"
        DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 \
            "$BUNDLED_PY_CHECK" --version 2>&1 | tail -n 50 | sed 's/^/          /'
        echo "          ------------------"
        echo "          The app will NOT be able to run AI tools on a clean Mac."
        echo "          Possible fixes:"
        echo "            1. Rebuild the venv: ./setup_python_macos.sh"
        echo "            2. Then repackage:   ./src/package_macos.sh"
        ERROR_COUNT=$((ERROR_COUNT + 1))
    fi

    # Test 2: numpy import (exercises dynamic library loading for libopenblas etc.)
    if "$BUNDLED_PY_CHECK" -c "import numpy" 2>/dev/null; then
        echo "  - Bundled numpy: OK"

        # Verify that numpy/scipy are NOT linking against Accelerate.framework
        # (NEWLAPACK symbols may be missing on macOS < 13.3)
        if find "$DIST_DIR/Contents/Resources/python_venv" -name "*.so" \
                -exec otool -L {} + 2>/dev/null | grep -q "Accelerate.framework"; then
            echo "  [WARNING] Python extensions link against Accelerate.framework!"
            echo "            This may cause crashes on macOS < 13.3 due to missing NEWLAPACK symbols."
            echo "            Please ensure numpy<1.26.0 is used to force OpenBLAS instead."
            ERROR_COUNT=$((ERROR_COUNT + 1))
        else
            echo "  - Accelerate framework check: OK (No NEWLAPACK link detected)"
        fi
    else
        echo "  - Bundled numpy: NOT available (some AI tools may fail)"
        echo "          Error output:"
        DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 \
            "$BUNDLED_PY_CHECK" -c "import numpy" 2>&1 || true
    fi
else
    echo "  [WARNING] Bundled python3 not found, cannot verify."
fi

# =============================================================================
# STEP 9.5 -- Ad-hoc code signing
# =============================================================================

echo ""
log_step 9.5 "Applying ad-hoc code signing..."

check_command codesign && {

    # Sign Python.framework first (inside-out signing order)
    PYTHON_FW="$FRAMEWORKS_DIR/Python.framework"
    if [ -d "$PYTHON_FW" ]; then
        codesign --force --sign - "$PYTHON_FW" 2>&1 | grep -v '^$' || true
    fi

    # Sign all other .framework bundles
    for fw in "$FRAMEWORKS_DIR"/*.framework; do
        [ -d "$fw" ] || continue

        # Sign inner .app bundles (e.g. Python.app) before the framework itself
        find "$fw" -name "*.app" -type d 2>/dev/null | while read -r inner_app; do
            if [ -d "$inner_app/Contents/MacOS" ]; then
                find "$inner_app/Contents/MacOS" -type f 2>/dev/null | while read -r inner_bin; do
                    codesign --force --sign - "$inner_bin" 2>&1 | grep -v '^$' || true
                done
            fi
            codesign --force --sign - "$inner_app" 2>&1 | grep -v '^$' || true
        done

        [ "$fw" = "$PYTHON_FW" ] && continue
        codesign --force --sign - "$fw" 2>&1 | grep -v '^$' || true
    done

    # Explicitly sign all .so and .dylib files in the Python venv and framework
    # (codesign --deep skips binaries in Resources and non-standard framework dirs)
    PYTHON_VENV_DEST="$DIST_DIR/Contents/Resources/python_venv"
    PYTHON_FW_DEST="$DIST_DIR/Contents/Frameworks/Python.framework"

    echo "  - Explicitly signing Python extension binaries..."
    for search_dir in "$PYTHON_VENV_DEST" "$PYTHON_FW_DEST"; do
        if [ -d "$search_dir" ]; then
            find "$search_dir" \( -name "*.so" -o -name "*.dylib" \) | while read -r so_file; do
                codesign --force --sign - "$so_file" 2>/dev/null || true
            done
        fi
    done

    # Sign Python executables in the venv bin directory
    if [ -d "$PYTHON_VENV_DEST" ]; then
        find "$PYTHON_VENV_DEST/bin" -type f 2>/dev/null | while read -r bin_file; do
            codesign --force --sign - "$bin_file" 2>/dev/null || true
        done
    fi

    # Final deep-sign of the entire app bundle
    codesign --force --deep -s - "$DIST_DIR" 2>&1 | grep -v '^$' || true
    echo "  - Ad-hoc signed: OK"

} || {
    log_warning "codesign not found (skip)"
}

# =============================================================================
# STEP 10 -- Generate README and copy changelog
# =============================================================================

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
cp "changelog.txt" "dist/" 2>/dev/null || true

# =============================================================================
# Final summary
# =============================================================================

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