
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

    
    # Determine search paths based on target architecture priority
    local search_paths=()
    if [ "$target_arch" == "x86_64" ]; then
        # Intel target: prioritize /usr/local
        search_paths=("/usr/local" "/opt/homebrew")
    else
        # ARM/Native target: prioritize /opt/homebrew
        search_paths=("/opt/homebrew" "/usr/local")
    fi

    # 1. Try standard brew prefix first (if matches architecture)
    local prefix=$(brew --prefix "$brew_pkg" 2>/dev/null || echo "")
    if [ -n "$prefix" ]; then
        # Check if this prefix matches our target architecture
        # /usr/local -> x86_64, /opt/homebrew -> arm64
        local prefix_is_compatible=0
        if [[ "$prefix" == "/usr/local"* ]] && [ "$target_arch" == "x86_64" ]; then prefix_is_compatible=1; fi
        if [[ "$prefix" == "/opt/homebrew"* ]] && [ "$target_arch" == "arm64" ]; then prefix_is_compatible=1; fi
        
        if [ $prefix_is_compatible -eq 1 ]; then
            # Add to front of search paths if not already there
            if [[ ! " ${search_paths[@]} " =~ " ${prefix} " ]]; then
                search_paths=("$prefix" "${search_paths[@]}")
            fi
        fi
    fi

    # Iterate through search paths
    local found_any_arch=""
    local checked_paths=""

    for base_path in "${search_paths[@]}"; do
        if [ ! -d "$base_path" ]; then continue; fi
        checked_paths="$checked_paths $base_path"

        # Define candidate paths for this base
        local candidates=()
        
        # Method A: Standard library path
        if [ -d "$base_path/lib" ]; then
             # Standard search
             candidates+=($(find "$base_path/lib" -name "${lib_name}*.dylib" -type f 2>/dev/null | grep -v ".dSYM" | sort))
        fi
        
        # Method B: Cellar fallback (specific version folders)
        if [ -d "$base_path/Cellar/$brew_pkg" ]; then
             candidates+=($(find "$base_path/Cellar/$brew_pkg" -name "${lib_name}*.dylib" -type f 2>/dev/null | grep -v ".dSYM" | sort))
        fi

        # Method C: Special catch-all for difficult libs (like libraw)
        if [ "$brew_pkg" == "libraw" ]; then
             # Use maxdepth to avoid deep recursion but check relevant lib dirs
             candidates+=($(find "$base_path" -maxdepth 4 -name "${lib_name}*.dylib" -type f 2>/dev/null | grep -v ".dSYM" | sort))
        fi

        for dylib in "${candidates[@]}"; do
            if [ -f "$dylib" ]; then
                 if dylib_matches_arch "$dylib" "$target_arch"; then
                    # COPY AND RENAME: Ensure we copy to the base name (e.g. libraw.dylib)
                    # This fixes issues where the bundle expects libraw.dylib but we copied libraw.23.dylib
                    cp "$dylib" "$dest_dir/${lib_name}.dylib" 2>/dev/null
                    
                    # If it was a versioned file, we might want the versioned name too for compatibility
                    # but typically the base name is enough if ID is fixed.
                    if [ "$(basename "$dylib")" != "${lib_name}.dylib" ]; then
                        # Also copy as original name just in case something links exclusively to it
                        cp "$dylib" "$dest_dir/" 2>/dev/null
                    fi
                    
                    echo "  - $lib_name: OK ($target_arch) [from: $base_path]"
                    return 0
                 else
                    # Found, but wrong architecture. Keep looking but remember for error message.
                    local found_arch=$(file "$dylib" 2>/dev/null | grep -o "x86_64\|arm64" | head -1)
                    found_any_arch="$found_arch"
                 fi
            fi
        done
    done

    # If we get here, valid library was not found
    if [ -n "$found_any_arch" ]; then
        echo "  - $lib_name: ARCH MISMATCH (target: $target_arch, found: $found_any_arch in checked paths)"
    else
        echo "  - $lib_name: NOT FOUND (checked: $checked_paths)"
    fi
    return 1
}

# --- Library Fixup Utilities ---
fix_dylib_id_and_deps() {
    local dylib_path="$1"
    local frameworks_dir="$2"
    
    if [ ! -f "$dylib_path" ]; then
        return
    fi
    
    # Ensure writable
    chmod +w "$dylib_path"
    
    local dylib_name=$(basename "$dylib_path")
    
    # 1. Set the ID to @rpath/...
    install_name_tool -id "@rpath/$dylib_name" "$dylib_path" 2>/dev/null || true
    
    # 2. Fix dependencies
    local deps=$(otool -L "$dylib_path" 2>/dev/null | grep -v "^$dylib_path:" | awk '{print $1}')
    
    for dep in $deps; do
        local dep_name=$(basename "$dep")
        
        # If this dependency exists in our frameworks dir, repoint to @rpath
        if [ -f "$frameworks_dir/$dep_name" ]; then
            if [ "$dep" != "@rpath/$dep_name" ]; then
                install_name_tool -change "$dep" "@rpath/$dep_name" "$dylib_path" 2>/dev/null || true
            fi
        fi
    done
}

fix_executable_deps() {
    local exec_path="$1"
    local frameworks_dir="$2"
    
    if [ ! -f "$exec_path" ] || [ ! -d "$frameworks_dir" ]; then
        return
    fi
    
    # Ensure writable
    chmod +w "$exec_path"
    
    local deps=$(otool -L "$exec_path" 2>/dev/null | grep -v "^$exec_path:" | awk '{print $1}')
    
    for dep in $deps; do
        local dep_name=$(basename "$dep")
        
        # If this dependency exists in our frameworks dir, repoint to @rpath
        if [ -f "$frameworks_dir/$dep_name" ]; then
            if [ "$dep" != "@rpath/$dep_name" ]; then
                install_name_tool -change "$dep" "@rpath/$dep_name" "$exec_path" 2>/dev/null || true
                echo "    - Repointed $dep_name to bundled version"
            fi
        fi
    done
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
