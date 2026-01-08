#!/bin/bash
# =============================================================================
# TStar Installer Builder for macOS
# Equivalent of build_installer.bat + installer.iss for Windows
# =============================================================================
# Creates a DMG disk image with the app and Applications shortcut
# =============================================================================

set -e

echo "==========================================="
echo " TStar Installer Builder (macOS)"
echo "==========================================="
echo ""

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

# --- Read version ---
VERSION=$(get_version)
log_info "Building version: $VERSION"

# --- STEP 0: Verify Prerequisites ---
echo "[STEP 0] Verifying prerequisites..."

check_command hdiutil || {
    log_error "hdiutil not found (should be built into macOS)"
    exit 1
}
echo "  - hdiutil: OK"

# Check for create-dmg (optional, better DMG aesthetics)
CREATE_DMG=""
if check_command create-dmg; then
    CREATE_DMG="create-dmg"
    echo "  - create-dmg: OK (will use for better styling)"
else
    log_warning "create-dmg not found (will use basic hdiutil)"
    echo "  - TIP: Install with 'brew install create-dmg' for prettier DMGs"
fi

# --- STEP 1: Build Application ---
echo ""
log_step 1 "Building application..."

if [ ! -f "build/TStar.app/Contents/MacOS/TStar" ]; then
    ./src/build_macos.sh --lto-on
fi
echo "  - Build: OK"

# --- STEP 2: Create Distribution Package ---
echo ""
log_step 2 "Creating distribution package..."

./src/package_macos.sh --silent
echo "  - Distribution: OK"

# Verify distribution
verify_dir "dist/TStar.app" "Distribution" || exit 1

# --- STEP 3: Clean Previous Output ---
echo ""
log_step 3 "Cleaning previous installer output..."

ensure_dir "installer_output"
rm -f "installer_output/TStar_Setup_"*.dmg
echo "  - Cleaned"

# --- STEP 4: Create DMG ---
echo ""
log_step 4 "Creating DMG installer..."

DMG_NAME="TStar_Setup_${VERSION}.dmg"
DMG_PATH="installer_output/$DMG_NAME"
DMG_TEMP="installer_output/TStar_temp.dmg"

# Prepare staging directory
STAGING_DIR="installer_output/dmg_staging"
safe_rm_rf "$STAGING_DIR"
ensure_dir "$STAGING_DIR"

# Copy app to staging
cp -R "dist/TStar.app" "$STAGING_DIR/"

# Create Applications symlink
ln -s /Applications "$STAGING_DIR/Applications"

# Copy README
cp "dist/README.txt" "$STAGING_DIR/" 2>/dev/null || true

if [ -n "$CREATE_DMG" ]; then
    # Use create-dmg for fancy DMG
    echo "  - Using create-dmg for styled DMG..."
    
    # Check for background image
    BG_IMAGE=""
    if [ -f "src/images/dmg_background.png" ]; then
        BG_IMAGE="--background src/images/dmg_background.png"
    fi
    
    # Check for volume icon
    VOL_ICON=""
    if [ -f "src/images/TStar.icns" ]; then
        VOL_ICON="--volicon src/images/TStar.icns"
    fi
    
    create-dmg \
        --volname "TStar $VERSION" \
        $VOL_ICON \
        --window-pos 200 120 \
        --window-size 600 400 \
        --icon-size 100 \
        --icon "TStar.app" 150 185 \
        --icon "Applications" 450 185 \
        --hide-extension "TStar.app" \
        $BG_IMAGE \
        --app-drop-link 450 185 \
        "$DMG_PATH" \
        "$STAGING_DIR"
else
    # Use basic hdiutil
    echo "  - Using hdiutil for basic DMG..."
    
    # Create temporary DMG
    hdiutil create -volname "TStar $VERSION" \
        -srcfolder "$STAGING_DIR" \
        -ov -format UDRW \
        "$DMG_TEMP"
    
    # Convert to compressed DMG
    hdiutil convert "$DMG_TEMP" \
        -format UDZO \
        -imagekey zlib-level=9 \
        -o "$DMG_PATH"
    
    rm -f "$DMG_TEMP"
fi

# Clean staging
safe_rm_rf "$STAGING_DIR"

# --- STEP 5: Verify DMG ---
echo ""
log_step 5 "Verifying DMG..."

verify_file "$DMG_PATH" "DMG file" || exit 1

DMG_SIZE=$(du -h "$DMG_PATH" | cut -f1)
echo "  - DMG created: $DMG_NAME"
echo "  - Size: $DMG_SIZE"

# --- SUCCESS ---
echo ""
echo "==========================================="
echo " SUCCESS! Installer Build Complete"
echo "==========================================="
echo ""
echo " Output File:"
echo "   $DMG_PATH"
echo ""
echo " Version: $VERSION"
echo " Size: $DMG_SIZE"
echo ""
echo " Next steps:"
echo "   1. Test the DMG on another Mac"
echo "   2. (Optional) Notarize with: xcrun notarytool"
echo "   3. Upload to GitHub Releases"
echo ""
echo "==========================================="
