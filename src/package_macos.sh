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
    EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
    TARGET_ARCH=$(detect_build_architecture "$EXECUTABLE")

    LIBPATH_ARGS="-libpath=$QT_PREFIX/lib"
    
    if [ "$TARGET_ARCH" == "arm64" ]; then
        if [ -d "/opt/homebrew/lib" ]; then
            LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/opt/homebrew/lib"
        fi
    else
        if [ -d "/usr/local/lib" ]; then
             LIBPATH_ARGS="$LIBPATH_ARGS -libpath=/usr/local/lib"
        fi
    fi
    
    # Eseguo macdeployqt (rimosso il grep che nascondeva gli errori su /opt/homebrew/opt)
    "$MACDEPLOYQT" "$DIST_DIR" \
        -verbose=1 \
        $LIBPATH_ARGS \
        2>&1 | grep -v "Cannot resolve rpath" | grep -v "using QList" | grep -v "No such file or directory" || true
    echo "  - Qt frameworks deployed"
else
    echo "[WARNING] macdeployqt not found. Qt frameworks not bundled."
    echo "  Install Qt6 with: brew install qt@6"
    ERROR_COUNT=$((ERROR_COUNT + 1))
fi

# --- Copy Homebrew dylibs ---
echo ""
log_step 5 "Copying Homebrew libraries..."

EXECUTABLE="$DIST_DIR/Contents/MacOS/TStar"
BUILD_ARCH=$(detect_build_architecture "$EXECUTABLE")
echo "  - Target architecture: $BUILD_ARCH"

FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"
ensure_dir "$FRAMEWORKS_DIR"

copy_dylib "libz.dylib" "zlib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libz" "zlib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libz.1.dylib" "zlib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libgsl" "gsl" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libgslcblas" "gsl" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libcfitsio" "cfitsio" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "liblz4" "lz4" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libzstd" "zstd" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libomp" "libomp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libbrotlicommon" "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libbrotlidec" "brotli" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libraw" "libraw" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libmd4c" "md4c" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libopenblas.0" "openblas" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libopenblas" "openblas" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# GCC/Fortran dependencies (required by math libraries like OpenBLAS/GSL)
copy_dylib "libgcc_s.1.1" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libgcc_s.1" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libgfortran.5" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libgfortran" "gcc" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libpng16" "libpng" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libpng" "libpng" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libjpeg" "jpeg" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libjpeg.9" "libjpeg-turbo" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libtiff" "libtiff" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libtiff.6" "libtiff" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libwebp" "libwebp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libwebpdemux" "libwebp" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "libfreetype" "freetype" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libharfbuzz" "harfbuzz" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

copy_dylib "liblapack" "lapack" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libjasper" "jasper" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

# ====================================================================
# Transitive dependencies that macdeployqt often misses
# ====================================================================
copy_dylib "libdbus-1.3" "dbus" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libtbb.12" "tbb" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libtbb" "tbb" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
copy_dylib "libopenjp2.7" "openjpeg" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || \
copy_dylib "libopenjp2" "openjpeg" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true

OPENCV_PREFIX=$(brew --prefix opencv 2>/dev/null || echo "")
if [ ! -d "$OPENCV_PREFIX/lib" ]; then
    for base in /opt/homebrew /usr/local; do
        if [ -d "$base/Cellar/opencv" ]; then
            OPENCV_PREFIX=$(find "$base/Cellar/opencv" -maxdepth 2 -name "lib" -type d 2>/dev/null | head -1)
            if [ -n "$OPENCV_PREFIX" ]; then 
                OPENCV_PREFIX=$(dirname "$OPENCV_PREFIX")
                break
            fi
        fi
    done
fi

if [ -n "$OPENCV_PREFIX" ] && [ -d "$OPENCV_PREFIX/lib" ]; then
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
    # Use rsync -aL to dereference ALL symlinks (including bin/python3) so the venv
    # is self-contained inside the bundle and works on machines without the dev's Python.
    if command -v rsync &> /dev/null; then
        rsync -aL "$PYTHON_VENV/" "$RESOURCES_DIR/python_venv/"
    else
        cp -RL "$PYTHON_VENV" "$RESOURCES_DIR/python_venv"
    fi
    echo "  - Python venv: OK"

    # --- Bundle Python.framework so the copied python3 binary can find it ---
    #
    # On macOS, Homebrew Python is a framework build: the python3 binary links against
    # Python.framework/Versions/X.Y/Python at an absolute Cellar path that does NOT
    # exist on the user's machine → dyld crash (exit code 6).
    # Fix: copy the framework shared library into Contents/Frameworks/ and rewrite the
    # reference inside the bundled python3 binary to a @loader_path-relative path.
    BUNDLED_PYTHON="$RESOURCES_DIR/python_venv/bin/python3"
    if [ -f "$BUNDLED_PYTHON" ]; then
        FRAMEWORK_LINK=$(otool -L "$BUNDLED_PYTHON" 2>/dev/null \
            | grep "Python.framework" | awk '{print $1}' | head -1 || true)

        # If FRAMEWORK_LINK already points to @loader_path the binary was already
        # fixed on a previous packaging run — nothing more to do for the binary itself.
        if echo "$FRAMEWORK_LINK" | grep -q "^@"; then
            echo "  - python3: Python.framework reference already relative (OK)"
            FRAMEWORK_LINK=""   # skip the copy/patch block below
        fi

        # If the exact Cellar path no longer exists (Python was updated in Homebrew),
        # search for the framework library under common Homebrew prefixes.
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

        if [ -n "$FRAMEWORK_LINK" ] && [ -f "$FRAMEWORK_LINK" ]; then
            FRAMEWORK_VERSION=$(echo "$FRAMEWORK_LINK" \
                | grep -oE 'Versions/[^/]+' | head -1 | cut -d/ -f2)
            PYTHON_FW_DEST="$FRAMEWORKS_DIR/Python.framework/Versions/$FRAMEWORK_VERSION"
            mkdir -p "$PYTHON_FW_DEST"
            cp "$FRAMEWORK_LINK" "$PYTHON_FW_DEST/Python"

            # Copy stdlib, headers and bin from the framework so Python can find them.
            FRAMEWORK_ROOT=$(dirname "$FRAMEWORK_LINK")
            for sub in lib bin include; do
                [ -d "$FRAMEWORK_ROOT/$sub" ] && cp -R "$FRAMEWORK_ROOT/$sub" "$PYTHON_FW_DEST/" || true
            done

            # Fix the library's own install name so @rpath-based references resolve.
            install_name_tool -id \
                "@rpath/Python.framework/Versions/$FRAMEWORK_VERSION/Python" \
                "$PYTHON_FW_DEST/Python" 2>/dev/null || true

            # Rewrite the absolute Homebrew Cellar path inside the python3 binary.
            # @loader_path for bin/python3 = Contents/Resources/python_venv/bin/
            # ../../..  reaches  Contents/
            NEW_FW_PATH="@loader_path/../../../Frameworks/Python.framework/Versions/$FRAMEWORK_VERSION/Python"
            install_name_tool -change "$FRAMEWORK_LINK" "$NEW_FW_PATH" \
                "$BUNDLED_PYTHON" 2>/dev/null || true
            # Ensure the bundled python3 has an rpath that includes Contents/Frameworks
            # so every .so it loads (numpy, onnxruntime, etc.) can use @rpath/<lib>.
            install_name_tool -add_rpath "@loader_path/../../../Frameworks" \
                "$BUNDLED_PYTHON" 2>/dev/null || true

            # Rewrite pyvenv.cfg so Python finds its stdlib inside the bundle.
            CFG_FILE="$RESOURCES_DIR/python_venv/pyvenv.cfg"
            if [ -f "$CFG_FILE" ]; then
                # Use absolute bundle-relative path via Resources.
                # We write a placeholder; at runtime python3 resolves its own prefix
                # from the binary location, so these values are informational only.
                sed -i '' 's|^home = .*|home = ../../Frameworks/Python.framework/Versions/Current/bin|' "$CFG_FILE"
                sed -i '' 's|^executable = .*|executable = ../../Frameworks/Python.framework/Versions/Current/bin/python3|' "$CFG_FILE"
            fi

            # ----------------------------------------------------------------
            # Create a valid macOS framework structure required by codesign.
            # ----------------------------------------------------------------
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
            ln -sf "$FRAMEWORK_VERSION" \
                "$FRAMEWORKS_DIR/Python.framework/Versions/Current" 2>/dev/null || true
            ln -sf "Versions/Current/Python" \
                "$FRAMEWORKS_DIR/Python.framework/Python" 2>/dev/null || true
            ln -sf "Versions/Current/Resources" \
                "$FRAMEWORKS_DIR/Python.framework/Resources" 2>/dev/null || true

            # Fix for posix_spawn error: Homebrew Python expects a Python.app stub inside Resources
            # to spawn subprocesses or itself. We must copy the actual Python.app bundle if it exists,
            # because a symlink or missing Info.plist causes posix_spawn to fail with "Undefined error: 0".
            if [ -d "$FRAMEWORK_ROOT/Resources/Python.app" ]; then
                cp -R "$FRAMEWORK_ROOT/Resources/Python.app" "$PYTHON_FW_DEST/Resources/"
                # Ensure the executable inside Python.app also has relative rpaths
                APP_PYTHON="$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS/Python"
                if [ -f "$APP_PYTHON" ]; then
                    install_name_tool -change "$FRAMEWORK_LINK" "@executable_path/../../../../Python" "$APP_PYTHON" 2>/dev/null || true
                    install_name_tool -add_rpath "@executable_path/../../../../" "$APP_PYTHON" 2>/dev/null || true
                fi
            else
                # Fallback if Python.app doesn't exist in source framework
                mkdir -p "$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS"
                ln -sf "../../../../../../../Resources/python_venv/bin/python3" \
                    "$PYTHON_FW_DEST/Resources/Python.app/Contents/MacOS/Python" 2>/dev/null || true
            fi

            echo "  - Python.framework/$FRAMEWORK_VERSION: bundled & patched in python3"
        fi

        # ----------------------------------------------------------------
        # Rewrite Homebrew absolute paths in ALL Python extension modules
        # (.so / .dylib under the venv).  These files reference libopenblas,
        # libgfortran, etc. from the developer's Homebrew installation — paths
        # that don't exist on a clean target Mac.
        # After rewriting to @rpath/<lib>, dyld will search the rpath chain
        # anchored on python3 (which now includes Contents/Frameworks/).
        # We also copy any newly-referenced dylibs that aren't yet in Frameworks.
        # ----------------------------------------------------------------
        echo "  - Rewriting Homebrew paths in Python extension modules (.so)..."
        SO_FIXED=0
        SO_MISSING=0
        find "$RESOURCES_DIR/python_venv" \( -name "*.so" -o -name "*.dylib" \) | while read -r so_file; do
            chmod +w "$so_file" 2>/dev/null || true
            deps=$(otool -L "$so_file" 2>/dev/null | grep -v "^$so_file:" | awk '{print $1}')
            for dep in $deps; do
                if echo "$dep" | grep -qE "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))"; then
                    dep_name=$(basename "$dep")
                    install_name_tool -change "$dep" "@rpath/$dep_name" "$so_file" 2>/dev/null || true
                    SO_FIXED=$((SO_FIXED + 1))
                    # Copy the dylib into Frameworks if not already there
                    if [ ! -f "$FRAMEWORKS_DIR/$dep_name" ] && [ -f "$dep" ]; then
                        cp "$dep" "$FRAMEWORKS_DIR/$dep_name" 2>/dev/null || true
                    fi
                fi
            done
        done
        echo "  - Extension module path fix: done"
    fi
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
for i in {1..3}; do
    for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
        if [ -f "$dylib" ]; then
            copy_dylib_with_dependencies "$dylib" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
        fi
    done

    # Add plugins to recursive scan
    if [ -d "$DIST_DIR/Contents/PlugIns" ]; then
        find "$DIST_DIR/Contents/PlugIns" -name "*.dylib" | while read -r plugin_path; do
            if [ -f "$plugin_path" ]; then
                copy_dylib_with_dependencies "$plugin_path" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
            fi
        done
    fi

    # Add framework binaries to recursive scan
    for framework in "$FRAMEWORKS_DIR"/*.framework; do
        if [ -d "$framework" ]; then
            framework_name=$(basename "$framework" .framework)
            # Standard
            if [ -f "$framework/Versions/A/$framework_name" ]; then
                copy_dylib_with_dependencies "$framework/Versions/A/$framework_name" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
            fi
            # Non-standard (like Python)
            if [ -d "$framework/Versions" ]; then
                for ver_dir in "$framework/Versions"/*/; do
                    ver_name=$(basename "$ver_dir")
                    [ "$ver_name" = "A" ] && continue
                    ver_binary="$ver_dir$framework_name"
                    if [ -f "$ver_binary" ]; then
                        copy_dylib_with_dependencies "$ver_binary" "$FRAMEWORKS_DIR" "$BUILD_ARCH" || true
                    fi
                done
            fi
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

# 4. Final sweep: rewrite ALL remaining Homebrew absolute paths in dylibs
echo "  - Final sweep: rewriting any remaining Homebrew paths in executable and dylibs..."
rewrite_homebrew_paths "$EXECUTABLE"
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        rewrite_homebrew_paths "$dylib"
    fi
done

# 5. Fix absolute paths inside Qt Framework binaries (NUOVO)
echo "  - Fixing dependencies inside .framework bundles..."
for framework in "$FRAMEWORKS_DIR"/*.framework; do
    if [ -d "$framework" ]; then
        framework_name=$(basename "$framework" .framework)

        # Standard Qt layout: Versions/A/<Name>
        framework_binary="$framework/Versions/A/$framework_name"
        if [ -f "$framework_binary" ]; then
            rewrite_homebrew_paths "$framework_binary"
        fi

        # Non-standard layout (e.g. Python.framework uses Versions/3.x/Python).
        # Iterate every Versions/* subdir that is not "A".
        if [ -d "$framework/Versions" ]; then
            for ver_dir in "$framework/Versions"/*/; do
                ver_name=$(basename "$ver_dir")
                [ "$ver_name" = "A" ] && continue  # already handled above
                ver_binary="$ver_dir$framework_name"
                if [ -f "$ver_binary" ]; then
                    rewrite_homebrew_paths "$ver_binary"
                fi
            done
        fi
    fi
done

# 6. Fix absolute paths inside Qt Plugins (NUOVO)
echo "  - Fixing dependencies inside Qt Plugins..."
PLUGINS_DIR="$DIST_DIR/Contents/PlugIns"
if [ -d "$PLUGINS_DIR" ]; then
    find "$PLUGINS_DIR" -name "*.dylib" | while read -r plugin_path; do
        if [ -f "$plugin_path" ]; then
            rewrite_homebrew_paths "$plugin_path"
            install_name_tool -add_rpath "@executable_path/../Frameworks" "$plugin_path" 2>/dev/null || true
        fi
    done
fi

# --- Verify bundled dylibs dependencies ---
echo ""
echo "[STEP 9.1] Verifying bundled dependencies..."

MISSING_DEPS=0
HOMEBREW_REFS=0

# Helper to verify a single binary file for unresolved rpaths or absolute homebrew paths
verify_binary_file() {
    local bin_file="$1"
    
    UNRESOLVED=$(otool -L "$bin_file" 2>/dev/null | grep "@rpath" | grep -v "^$bin_file:" | grep -v "@rpath/Qt" || true)
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
    
    BREW_REFS=$(otool -L "$bin_file" 2>/dev/null | grep -v "^$bin_file:" | awk '{print $1}' | grep -E "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))" || true)
    if [ -n "$BREW_REFS" ]; then
        while IFS= read -r brew_ref; do
            echo "  [WARNING] Absolute Homebrew path in $(basename "$bin_file"): $brew_ref"
            HOMEBREW_REFS=$((HOMEBREW_REFS + 1))
        done <<< "$BREW_REFS"
    fi
}

# Check all bundled dylibs
echo "  - Checking for unresolved @rpath references..."
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        verify_binary_file "$dylib"
    fi
done

# Check plugins
if [ -d "$DIST_DIR/Contents/PlugIns" ]; then
    find "$DIST_DIR/Contents/PlugIns" -name "*.dylib" | while read -r plugin_path; do
        if [ -f "$plugin_path" ]; then
            verify_binary_file "$plugin_path"
        fi
    done
fi

# Check frameworks
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

if [ -f "$EXECUTABLE" ]; then
    EXEC_BREW_REFS=$(otool -L "$EXECUTABLE" 2>/dev/null | grep -v "^$EXECUTABLE:" | awk '{print $1}' | grep -E "^(/opt/homebrew|/usr/local/(Cellar|opt|lib))" || true)
    if [ -n "$EXEC_BREW_REFS" ]; then
        while IFS= read -r brew_ref; do
            echo "  [WARNING] Absolute Homebrew path in TStar executable: $brew_ref"
            HOMEBREW_REFS=$((HOMEBREW_REFS + 1))
        done <<< "$EXEC_BREW_REFS"
    fi
fi

if [ $MISSING_DEPS -gt 0 ]; then
    echo "  [WARNING] Found $MISSING_DEPS unresolved @rpath dependencies"
else
    echo "  - All @rpath dylib dependencies resolved"
fi

if [ $HOMEBREW_REFS -gt 0 ]; then
    echo "  [WARNING] Found $HOMEBREW_REFS absolute Homebrew path(s) remaining!"
    echo "           App may NOT work on systems without Homebrew."
else
    echo "  - No absolute Homebrew paths detected (portable)"
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

if [ ! -f "$FRAMEWORKS_DIR/libmd4c.0.dylib" ]; then
    echo "  [ERROR] libmd4c.0.dylib NOT FOUND in bundle!"
    echo "         Qt markdown support will NOT work!"
    echo "         Make sure: brew install md4c"
    ERROR_COUNT=$((ERROR_COUNT + 1))
else
    echo "  - libmd4c.0.dylib: OK"
fi

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

for imglib in libpng libjpeg libtiff libwebp; do
    if find "$FRAMEWORKS_DIR" -name "$imglib*" 2>/dev/null | head -1 | grep -q .; then
        echo "  - $imglib: OK"
    else
        echo "  [WARNING] $imglib NOT FOUND - some image formats may fail"
    fi
done

# --- Verify bundled python3 actually starts (after all dylib patching) ---
echo ""
log_step "9.4" "Verifying bundled Python..."
BUNDLED_PY_CHECK="$DIST_DIR/Contents/Resources/python_venv/bin/python3"
FRAMEWORKS_DIR="$DIST_DIR/Contents/Frameworks"
if [ -f "$BUNDLED_PY_CHECK" ]; then
    # Test 1: Python interpreter runs
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
        DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 "$BUNDLED_PY_CHECK" --version 2>&1 | tail -n 50 | sed 's/^/          /'
        echo "          ------------------"
        echo "          The app will NOT be able to run AI tools on a clean Mac."
        echo "          Possible fixes:"
        echo "            1. Rebuild the venv:  ./setup_python_macos.sh"
        echo "            2. Then repackage:    ./src/package_macos.sh"
        ERROR_COUNT=$((ERROR_COUNT + 1))
    fi

    # Test if it can load numpy (which tests dynamic library loading like libopenblas)
    if "$BUNDLED_PY_CHECK" -c "import numpy" 2>/dev/null; then
        echo "  - Bundled numpy: OK"
    else
        echo "  - Bundled numpy: NOT available (some AI tools may fail)"
        echo "          Error output:"
        DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 "$BUNDLED_PY_CHECK" -c "import numpy" 2>&1 || true
    fi
else
    echo "  [WARNING] Bundled python3 not found, cannot verify."
fi

# --- Ad-hoc Code Signing ---
echo ""
log_step 9.5 "Applying ad-hoc code signing..."

check_command codesign && {
    # Sign Python.framework explicitly first (inside-out signing).
    # codesign --deep cannot sign a framework with a broken structure;
    # by signing the framework binary directly before touching the outer
    # bundle we guarantee the subcomponent has a valid signature.
    PYTHON_FW="$FRAMEWORKS_DIR/Python.framework"
    if [ -d "$PYTHON_FW" ]; then
        codesign --force --sign - "$PYTHON_FW" 2>&1 | grep -v '^$' || true
    fi
    # Sign all other nested .framework bundles explicitly before signing the app
    for fw in "$FRAMEWORKS_DIR"/*.framework; do
        [ -d "$fw" ] || continue
        [ "$fw" = "$PYTHON_FW" ] && continue
        codesign --force --sign - "$fw" 2>&1 | grep -v '^$' || true
    done

    # Explicitly sign all binaries and .so in the python venv
    # codesign --deep skips binaries in Resources, so they must be signed explicitly
    # since they were modified by install_name_tool
    PYTHON_VENV_DEST="$DIST_DIR/Contents/Resources/python_venv"
    if [ -d "$PYTHON_VENV_DEST" ]; then
        echo "  - Explicitly signing Python venv binaries..."
        find "$PYTHON_VENV_DEST" \( -name "*.so" -o -name "*.dylib" \) | while read -r so_file; do
            codesign --force --sign - "$so_file" 2>/dev/null || true
        done
        # Sign python executables. We attempt to sign all files in bin/.
        # Text scripts (like pip) will fail cleanly and be ignored.
        find "$PYTHON_VENV_DEST/bin" -type f 2>/dev/null | while read -r bin_file; do
            codesign --force --sign - "$bin_file" 2>/dev/null || true
        done
    fi

    # Finally sign the whole app bundle (--deep will re-sign already-signed
    # subcomponents, which is fine, but now they are all valid first)
    codesign --force --deep -s - "$DIST_DIR" 2>&1 | grep -v '^$' || true
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
