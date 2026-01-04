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

# --- Read version ---
VERSION="1.0.0"
if [ -f "changelog.txt" ]; then
    VERSION=$(grep -E "^Version [0-9.]+" changelog.txt | head -1 | awk '{print $2}')
    if [ -z "$VERSION" ]; then
        VERSION="1.0.0"
    fi
fi
echo "[INFO] Building version: $VERSION"
echo ""

# --- STEP 0: Verify Prerequisites ---
echo "[STEP 0] Verifying prerequisites..."

# Check for hdiutil (built into macOS)
if ! command -v hdiutil &> /dev/null; then
    echo "[ERROR] hdiutil not found (should be built into macOS)"
    exit 1
fi
echo "  - hdiutil: OK"

# Check for create-dmg (optional, better DMG aesthetics)
CREATE_DMG=""
if command -v create-dmg &> /dev/null; then
    CREATE_DMG="create-dmg"
    echo "  - create-dmg: OK (will use for better styling)"
else
    echo "  - create-dmg: NOT FOUND (will use basic hdiutil)"
    echo "  - TIP: Install with 'brew install create-dmg' for prettier DMGs"
fi

# --- STEP 1: Build Application ---
echo ""
echo "[STEP 1] Building application..."

if [ ! -f "build/TStar.app/Contents/MacOS/TStar" ]; then
    ./src/build_macos.sh
fi
echo "  - Build: OK"

# --- STEP 2: Create Distribution Package ---
echo ""
echo "[STEP 2] Creating distribution package..."

./src/package_macos.sh --silent
echo "  - Distribution: OK"

# Verify distribution
if [ ! -d "dist/TStar.app" ]; then
    echo "[ERROR] Distribution incomplete - TStar.app not found!"
    exit 1
fi

# --- STEP 3: Clean Previous Output ---
echo ""
echo "[STEP 3] Cleaning previous installer output..."

mkdir -p "installer_output"
rm -f "installer_output/TStar_Setup_"*.dmg
echo "  - Cleaned"

# --- STEP 4: Create DMG ---
echo ""
echo "[STEP 4] Creating DMG installer..."

DMG_NAME="TStar_Setup_${VERSION}.dmg"
DMG_PATH="installer_output/$DMG_NAME"
DMG_TEMP="installer_output/TStar_temp.dmg"

# Prepare staging directory
STAGING_DIR="installer_output/dmg_staging"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

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
rm -rf "$STAGING_DIR"

# --- STEP 5: Verify DMG ---
echo ""
echo "[STEP 5] Verifying DMG..."

if [ ! -f "$DMG_PATH" ]; then
    echo "[ERROR] DMG file not created!"
    exit 1
fi

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
