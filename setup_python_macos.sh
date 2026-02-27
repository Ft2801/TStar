
set -e

echo ">>> TStar Python Environment Setup (macOS) <<<"

# Move to project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
PROJECT_ROOT="$(pwd)"

DEPS_DIR="$PROJECT_ROOT/deps"
PYTHON_VENV="$DEPS_DIR/python_venv"

echo "Target Directory: $PYTHON_VENV"

# --- 1. Find Python ---
echo ""
echo "[STEP 1] Finding Python..."

# Priority list of compatible Python versions
COMPAT_VERSIONS=("3.13" "3.12" "3.11")
PYTHON_CMD=""

# 1. Try specific versioned commands in PATH
for ver in "${COMPAT_VERSIONS[@]}"; do
    if command -v "python$ver" &> /dev/null; then
        PYTHON_CMD="python$ver"
        break
    fi
done

# 2. Try Homebrew paths directly (ARM and Intel)
if [ -z "$PYTHON_CMD" ]; then
    HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
    for ver in "${COMPAT_VERSIONS[@]}"; do
        BREW_PYTHON="$HOMEBREW_PREFIX/opt/python@$ver/bin/python$ver"
        if [ -x "$BREW_PYTHON" ]; then
            PYTHON_CMD="$BREW_PYTHON"
            break
        fi
    done
fi

# 3. Fallback to generic python3 if it's compatible (< 3.14)
if [ -z "$PYTHON_CMD" ] && command -v python3 &> /dev/null; then
    P3_VER=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
    P3_MAJOR=$(echo "$P3_VER" | cut -d. -f1)
    P3_MINOR=$(echo "$P3_VER" | cut -d. -f2)
    
    # If python3 is 3.11-3.13, we use it. If it's newer (3.14+), we avoid it.
    if [ "$P3_MAJOR" -eq 3 ] && [ "$P3_MINOR" -ge 11 ] && [ "$P3_MINOR" -le 13 ]; then
        PYTHON_CMD="python3"
    fi
fi

if [ -z "$PYTHON_CMD" ]; then
    echo "[ERROR] Compatible Python 3 (3.11, 3.12, or 3.13) not found!"
    echo "Install with: brew install python@3.12"
    exit 1
fi

PYTHON_DISPLAY_VERSION=$("$PYTHON_CMD" --version)
echo "  - Using: $PYTHON_CMD ($PYTHON_DISPLAY_VERSION)"

# --- 2. Prepare Directory ---
echo ""
echo "[STEP 2] Preparing directory..."

mkdir -p "$DEPS_DIR"

if [ -d "$PYTHON_VENV" ]; then
    echo "  - Removing existing venv..."
    rm -rf "$PYTHON_VENV"
fi

# --- 3. Create Virtual Environment ---
echo ""
echo "[STEP 3] Creating virtual environment..."

# --copies: copies the Python executable instead of symlinking it.
# This makes the venv portable (symlinks break when the venv is copied into the app bundle).
"$PYTHON_CMD" -m venv --copies "$PYTHON_VENV"

if [ ! -f "$PYTHON_VENV/bin/python3" ]; then
    echo "[ERROR] Failed to create virtual environment!"
    exit 1
fi

echo "  - Virtual environment created"

# --- 4. Upgrade pip ---
echo ""
echo "[STEP 4] Upgrading pip..."

"$PYTHON_VENV/bin/python3" -m pip install --upgrade pip --quiet

# --- 5. Install Dependencies ---
echo ""
echo "[STEP 5] Installing dependencies..."

PACKAGES=(
    "numpy"
    "tifffile"
    "imagecodecs"
    "astropy"
    "onnxruntime"
)

for pkg in "${PACKAGES[@]}"; do
    echo "  - Installing $pkg..."
    "$PYTHON_VENV/bin/python3" -m pip install "$pkg" --quiet
done

# --- 6. Verify Installation ---
echo ""
echo "[STEP 6] Verifying installation..."

for pkg in "${PACKAGES[@]}"; do
    if "$PYTHON_VENV/bin/python3" -c "import ${pkg//-/_}" 2>/dev/null; then
        echo "  - $pkg: OK"
    else
        echo "  - $pkg: FAILED"
    fi
done

# --- Done ---
echo ""
echo ">>> Python Setup Complete! <<<"
echo ""
echo "Python executable: $PYTHON_VENV/bin/python3"
echo ""
echo "You can now run:"
echo "  ./src/build_macos.sh"
