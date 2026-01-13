
# =============================================================================
# TStar macOS Build Utilities
# Shared functions for build_macos.sh, package_macos.sh, build_installer_macos.sh
# =============================================================================

# --- Version Management ---
get_version() {
    local VERSION="1.0.0"
    if [ -f "changelog.txt" ]; then
        VERSION=$(grep -E "^Version [0-9.]+" changelog.txt | head -1 | awk '{print $2}' | tr -d '\r')
        if [ -z "$VERSION" ]; then
            VERSION="1.0.0"
        fi
    fi
    echo "$VERSION"
}

# --- Homebrew Utilities ---
get_homebrew_prefix() {
    local prefix=$(brew --prefix 2>/dev/null || echo "")
    if [ -z "$prefix" ]; then
        # Fallback for ARM64
        if [ -d "/opt/homebrew" ]; then
            prefix="/opt/homebrew"
        # Fallback for Intel
        elif [ -d "/usr/local" ]; then
            prefix="/usr/local"
        fi
    fi
    echo "$prefix"
}

# Detect if running under Rosetta (Intel on ARM)
is_rosetta() {
    local arch=$(arch 2>/dev/null || echo "unknown")
    if [ "$arch" == "i386" ]; then
        return 0  # Running under Rosetta
    fi
    return 1  # Native ARM64
}

# --- Qt Detection (Robust) ---
detect_qt_prefix() {
    local qt_prefix=""
    
    # Try qt@6 first (newer convention)
    qt_prefix=$(brew --prefix qt@6 2>/dev/null || echo "")
    
    # Fallback to qt
    if [ -z "$qt_prefix" ]; then
        qt_prefix=$(brew --prefix qt 2>/dev/null || echo "")
    fi
    
    # If symlink is broken, search Cellar
    if [ -z "$qt_prefix" ] || [ ! -d "$qt_prefix/bin" ]; then
        local cellar_qt=$(find /opt/homebrew/Cellar/qt* -maxdepth 2 -name "bin" -type d 2>/dev/null | head -1)
        if [ -n "$cellar_qt" ]; then
            qt_prefix=$(dirname "$cellar_qt")
        fi
    fi
    
    # Final fallback for Intel
    if [ -z "$qt_prefix" ] && [ -d "/usr/local/opt/qt@6" ]; then
        qt_prefix="/usr/local/opt/qt@6"
    fi
    
    echo "$qt_prefix"
}

# Find macdeployqt in Qt installation
find_macdeployqt() {
    local qt_prefix="$1"
    local macdeployqt=""
    
    if [ -f "$qt_prefix/bin/macdeployqt" ]; then
        macdeployqt="$qt_prefix/bin/macdeployqt"
    else
        # Try PATH
        macdeployqt=$(which macdeployqt 2>/dev/null || echo "")
    fi
    
    echo "$macdeployqt"
}

# --- Architecture Detection ---
detect_build_architecture() {
    # First, check if the executable exists and has an architecture
    local executable="$1"
    if [ -n "$executable" ] && [ -f "$executable" ]; then
        # Use file command to get architecture
        local file_output=$(file "$executable" 2>/dev/null || echo "")
        if echo "$file_output" | grep -q "x86_64"; then
            echo "x86_64"
            return 0
        elif echo "$file_output" | grep -q "arm64"; then
            echo "arm64"
            return 0
        fi
    fi
    
    # Fallback to native architecture
    local native_arch=$(arch)
    if [ "$native_arch" == "arm64" ] || [ "$native_arch" == "aarch64" ]; then
        echo "arm64"
    else
        echo "x86_64"
    fi
}

# Check if dylib has matching architecture
dylib_matches_arch() {
    local dylib="$1"
    local target_arch="$2"
    
    if [ ! -f "$dylib" ]; then
        return 1
    fi
    
    local file_output=$(file "$dylib" 2>/dev/null || echo "")
    
    if [ "$target_arch" == "arm64" ]; then
        # Accept arm64 but NOT x86_64
        if echo "$file_output" | grep -q "arm64" && ! echo "$file_output" | grep -q "x86_64"; then
            return 0
        fi
    else
        # x86_64: Accept x86_64 but NOT arm64
        if echo "$file_output" | grep -q "x86_64" && ! echo "$file_output" | grep -q "arm64"; then
            return 0
        fi
    fi
    
    return 1
}

# Recursively copy all dependencies of a dylib
copy_dylib_with_dependencies() {
    local dylib="$1"
    local dest_dir="$2"
    local target_arch="$3"
    local processed_dylibs="${4:-}"
    
    # Avoid infinite loops
    if echo "$processed_dylibs" | grep -q "$(basename "$dylib")"; then
        return 0
    fi
    processed_dylibs="$processed_dylibs $(basename "$dylib")"
    
    # Get all dependencies
    local deps=$(otool -L "$dylib" 2>/dev/null | grep -v "^$dylib:" | grep "\.dylib" | awk '{print $1}' | sort -u || true)
    
    for dep in $deps; do
        # Skip system dylibs
        if echo "$dep" | grep -qE "^(/usr/lib|/System|@executable_path)"; then
            continue
        fi
        
        # Skip frameworks
        if echo "$dep" | grep -q "\.framework"; then
            continue
        fi
        
        # Handle @rpath references
        if echo "$dep" | grep -q "@rpath"; then
            continue  # Will be handled by install_name_tool
        fi
        
        # Check if already bundled
        local dep_basename=$(basename "$dep")
        if [ ! -f "$dest_dir/$dep_basename" ]; then
            # Try to find and copy from Homebrew
            local found=0
            for brew_path in /opt/homebrew /usr/local; do
                if [ -f "$brew_path/lib/$dep_basename" ]; then
                    if dylib_matches_arch "$brew_path/lib/$dep_basename" "$target_arch"; then
                        cp "$brew_path/lib/$dep_basename" "$dest_dir/" 2>/dev/null && found=1
                        # Recursively copy its dependencies
                        if [ $found -eq 1 ]; then
                            copy_dylib_with_dependencies "$dest_dir/$dep_basename" "$dest_dir" "$target_arch" "$processed_dylibs" || true
                        fi
                    fi
                    break
                fi
            done
            
            if [ $found -eq 0 ] && ! echo "$dep" | grep -qE "libSystem\.B|libobjc\.A|libstdc|libc\+\+"; then
                # Only warn if it's not a system library we expect to be unavailable
                true  # Silent skip for now
            fi
        fi
    done
}

# --- Dependency Utilities ---
copy_dylib() {
    local lib_name="$1"
    local brew_pkg="$2"
    local dest_dir="$3"
    local target_arch="${4:-}"  # Optional: target architecture (x86_64 or arm64)
    
    # If no target arch specified, detect from the executable
    if [ -z "$target_arch" ]; then
        if [ -d "$dest_dir/../MacOS" ]; then
            # Find the executable in the app bundle
            local executable=$(find "$dest_dir/../MacOS" -name "TStar" -o -name "*.app" 2>/dev/null | head -1)
            if [ -z "$executable" ]; then
                executable=$(find "$dest_dir/../MacOS" -type f -executable 2>/dev/null | head -1)
            fi
            if [ -n "$executable" ]; then
                target_arch=$(detect_build_architecture "$executable")
            fi
        fi
    fi
    
    # Fallback to native architecture
    if [ -z "$target_arch" ]; then
        target_arch=$(detect_build_architecture "")
    fi
>>>>>>> Stashed changes
    
    local prefix=$(brew --prefix "$brew_pkg" 2>/dev/null || echo "")
    
    # Fallback to Cellar if symlink broken
    if [ ! -d "$prefix/lib" ]; then
        local cellar_path="/opt/homebrew/Cellar/$brew_pkg"
        if [ -d "$cellar_path" ]; then
            local version_path=$(find "$cellar_path" -maxdepth 1 -mindepth 1 -type d | head -1)
            if [ -n "$version_path" ]; then
                prefix="$version_path"
            fi
        fi
        
        # Intel fallback
        if [ ! -d "$prefix/lib" ] && [ -d "/usr/local/Cellar/$brew_pkg" ]; then
            local version_path=$(find "/usr/local/Cellar/$brew_pkg" -maxdepth 1 -mindepth 1 -type d | head -1)
            if [ -n "$version_path" ]; then
                prefix="$version_path"
            fi
        fi
    fi
    
    if [ -n "$prefix" ] && [ -d "$prefix/lib" ]; then
        # Find all matching dylibs and check architecture
        local dylibs=$(find "$prefix/lib" -name "${lib_name}*.dylib" -type f 2>/dev/null | sort)
        
        for dylib in $dylibs; do
            if dylib_matches_arch "$dylib" "$target_arch"; then
                cp "$dylib" "$dest_dir/" 2>/dev/null
                echo "  - $lib_name: OK ($target_arch)"
                return 0
            fi
        done
        
        # If no exact match found, warn and skip
        if [ -n "$dylibs" ]; then
            # Check what architectures were found
            local found_archs=$(for dylib in $dylibs; do file "$dylib" 2>/dev/null | grep -o "x86_64\|arm64" | sort -u; done | tr '\n' '/' | sed 's|/$||')
            echo "  - $lib_name: ARCH MISMATCH (target: $target_arch, found: $found_archs)"
        else
            echo "  - $lib_name: NOT FOUND"
        fi
    else
        echo "  - $lib_name: NOT FOUND"
    fi
    return 1
}

# --- Python Detection ---
find_compatible_python() {
    local COMPAT_VERSIONS=("3.13" "3.12" "3.11")
    local PYTHON_CMD=""
    local HOMEBREW_PREFIX=$(get_homebrew_prefix)
    
    # Try versioned commands in PATH
    for ver in "${COMPAT_VERSIONS[@]}"; do
        if command -v "python$ver" &> /dev/null; then
            PYTHON_CMD="python$ver"
            break
        fi
    done
    
    # Try Homebrew paths
    if [ -z "$PYTHON_CMD" ]; then
        for ver in "${COMPAT_VERSIONS[@]}"; do
            local brew_python="$HOMEBREW_PREFIX/opt/python@$ver/bin/python$ver"
            if [ -x "$brew_python" ]; then
                PYTHON_CMD="$brew_python"
                break
            fi
        done
    fi
    
    # Fallback to python3 (if compatible)
    if [ -z "$PYTHON_CMD" ] && command -v python3 &> /dev/null; then
        local p3_ver=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null)
        local p3_major=$(echo "$p3_ver" | cut -d. -f1)
        local p3_minor=$(echo "$p3_ver" | cut -d. -f2)
        
        if [ "$p3_major" -eq 3 ] && [ "$p3_minor" -ge 11 ] && [ "$p3_minor" -le 13 ]; then
            PYTHON_CMD="python3"
        fi
    fi
    
    echo "$PYTHON_CMD"
}

# --- Logging Utilities ---
log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_warning() {
    echo "[WARNING] $1"
}

log_step() {
    echo ""
    echo "[STEP $1] $2"
}

# --- File Utilities ---
safe_rm_rf() {
    local path="$1"
    if [ -d "$path" ] || [ -L "$path" ]; then
        rm -rf "$path"
    fi
}

ensure_dir() {
    local dir="$1"
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
    fi
}

# --- Validation ---
check_command() {
    local cmd="$1"
    if ! command -v "$cmd" &> /dev/null; then
        return 1
    fi
    return 0
}

verify_file() {
    local file="$1"
    local description="$2"
    
    if [ ! -f "$file" ]; then
        log_error "$description not found: $file"
        return 1
    fi
    return 0
}

verify_dir() {
    local dir="$1"
    local description="$2"
    
    if [ ! -d "$dir" ]; then
        log_error "$description not found: $dir"
        return 1
    fi
    return 0
}
