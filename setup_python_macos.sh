#!/bin/bash
# =============================================================================
# TStar Python Environment Setup for macOS
# Equivalent of setup_python_dist.ps1 for Windows
# =============================================================================
# Creates a Python virtual environment with required dependencies
# for AI tools (GraXpert bridge, StarNet converter, etc.)
# =============================================================================

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

# Try Homebrew Python first
PYTHON_CMD=""
if command -v python3.11 &> /dev/null; then
    PYTHON_CMD="python3.11"
elif [ -f "$(brew --prefix python@3.11 2>/dev/null)/bin/python3.11" ]; then
    PYTHON_CMD="$(brew --prefix python@3.11)/bin/python3.11"
elif command -v python3 &> /dev/null; then
    PYTHON_CMD="python3"
else
    echo "[ERROR] Python 3 not found!"
    echo "Install with: brew install python@3.11"
    exit 1
fi

PYTHON_VERSION=$("$PYTHON_CMD" --version)
echo "  - Using: $PYTHON_CMD ($PYTHON_VERSION)"

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

"$PYTHON_CMD" -m venv "$PYTHON_VENV"

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
