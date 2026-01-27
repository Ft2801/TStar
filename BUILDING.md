# Building TStar from Source

This guide provides detailed instructions for building TStar on Windows using MinGW.

## Prerequisites

### Required Software

| Software | Version | Download |
|----------|---------|----------|
| Qt6 | 6.5 or later | [qt.io](https://www.qt.io/download-qt-installer) |
| CMake | 3.16 or later | [cmake.org](https://cmake.org/download/) |
| MinGW-w64 | GCC 11+ | Included with Qt installation |
| Git | Any recent | [git-scm.com](https://git-scm.com/download/win) |

### Qt Installation

1. Download the Qt Online Installer
2. Select **Qt 6.x.x** → **MinGW 64-bit**
3. Under **Developer and Designer Tools**, select:
   - CMake (if not already installed)
   - MinGW 13.x.x 64-bit (or latest available)
4. Note your installation path (e.g., `C:\Qt\6.7.0\mingw_64`)

## Dependencies Setup

TStar requires the following libraries. Place them in a `deps/` folder in the project root:

```
TStar/
├── deps/
│   ├── cfitsio/
│   │   ├── include/
│   │   │   └── fitsio.h
│   │   └── lib/
│   │       └── libcfitsio.a
│   ├── opencv/
│   │   ├── include/
│   │   └── lib/
│   ├── gsl/
│   │   ├── include/
│   │   └── lib/
│   ├── libraw/ (Optional - for RAW image support)
│   │   ├── libraw/
│   │   │   └── libraw.h
│   │   ├── lib/
│   │   └── bin/
│   ├── lz4/ (Optional - for XISF LZ4 compression)
│   │   ├── include/
│   │   └── lib/
│   └── zstd/ (Optional - for XISF Zstd compression)
│       ├── include/
│       └── lib/
├── src/
├── CMakeLists.txt
└── ...
```

### CFITSIO

**Option 1: Pre-built (Recommended)**
- Download pre-built MinGW binaries from [CFITSIO releases](https://heasarc.gsfc.nasa.gov/fitsio/)
- Extract to `deps/cfitsio/`

**Option 2: Build from Source**
```bash
# In MSYS2/MinGW terminal
wget https://heasarc.gsfc.nasa.gov/FTP/software/fitsio/c/cfitsio-4.3.0.tar.gz
tar -xzf cfitsio-4.3.0.tar.gz
cd cfitsio-4.3.0
./configure --prefix=/path/to/deps/cfitsio
make && make install
```

### OpenCV

- Download OpenCV for Windows from [opencv.org](https://opencv.org/releases/)
- Extract and copy `include/` and MinGW-compatible `lib/` to `deps/opencv/`

### GSL (GNU Scientific Library)

- Pre-built MinGW binaries available from [MSYS2](https://www.msys2.org/)
- Or build from source following [GSL documentation](https://www.gnu.org/software/gsl/)

### LibRaw (Optional - RAW Image Support)

**Recommended for full camera RAW support (CR2, NEF, ARW, etc.)**

- Download pre-built Windows binaries from [LibRaw website](https://www.libraw.org/download)
- Extract and place in `deps/libraw/`
- Requires `libraw.dll` in `deps/libraw/bin/`
- CMake will automatically detect and enable LibRaw support with `HAVE_LIBRAW` flag

### LZ4 and Zstd (Optional - XISF Compression)

**Optional dependencies for compressed XISF format support**

- **LZ4**: Fast compression for XISF files
  - Download from [LZ4 releases](https://github.com/lz4/lz4/releases)
  - Extract to `deps/lz4/`
  
- **Zstd**: High-ratio compression for XISF files  
  - Download from [Zstd releases](https://github.com/facebook/zstd/releases)
  - Extract to `deps/zstd/`

CMake will automatically detect these libraries and enable compression features.

### Python Environment Discovery

TStar uses an intelligent fallback chain to locate Python for AI tools and bridge scripts:

**macOS & Linux:**
1. Bundled Python in app bundle: `$APP_DIR/../Resources/python_venv/bin/python3`
2. Development Python: `$APP_DIR/../../deps/python_venv/bin/python3`
3. System Python in PATH: `python3` (found via `QStandardPaths::findExecutable()`)
4. Fallback: `python3` (direct invocation)

**Windows:**
1. Bundled embeddable Python: `$APP_DIR/python/python.exe`
2. Development Python: `$APP_DIR/../deps/python/python.exe`
3. System Python in PATH: `python3` (found via `QStandardPaths::findExecutable()`)
4. Fallback: `python3` (direct invocation)

This approach ensures compatibility across development, distribution, and heterogeneous system configurations.

## Building

### Command Line (Recommended)

1. Open a terminal with Qt environment (e.g., Qt 6.x.x MinGW 64-bit).
2. Use the "All-in-One" build script:
```bash
build_all.bat
```
Alternatively, follow the manual steps:
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/mingw_64" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release -j4
```

## Deployment & Distribution

To create a standalone, portable folder for distribution:

```bash
package_dist.bat
```

**This script automates several critical tasks:**
1. Verifies the `TStar.exe` build.
2. Checks for (and runs) `setup_python_dist.ps1` to ensure the Python environment is ready.
3. Copies all Qt DLLs and plugins.
4. Copies MinGW, GSL, and OpenCV runtime libraries.
5. **Copies optional dependency libraries** (LibRaw, LZ4, Zstd) if they exist.
6. **Bundles the Python environment** into the `dist/TStar/python` subfolder.
7. **Centralizes scripts** from `src/scripts` into `dist/TStar/scripts`.
8. Generates a `README.txt` for the end user.

The resulting folder in `dist/TStar` is completely standalone and can be moved to any machine without pre-installed dependencies.

## Troubleshooting

### Python-related issues
- If the AI tools fail, ensure `deps/python/python.exe` exists.
- If you need to re-install the environment, simply delete `deps/python` and run `package_dist.bat` again.
- C++ code looks for Python in:
  1. `./python/python.exe` (Distribution/Production)
  2. `../deps/python/python.exe` (Development/Build environment)
  3. System `python` (Fallback)

### Missing DLLs at runtime
If you built manually with CMake, you MUST run `package_dist.bat` to collect the dependencies. The old `deploy.bat` is preserved for legacy use but `package_dist.bat` is now the preferred method as it handles Python bundling.

From your MinGW `bin/` directory to the executable folder.

## Build Options

| CMake Option | Default | Description |
|--------------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Set to `Release` for optimized builds |
| `CMAKE_PREFIX_PATH` | — | Path to Qt installation |

## Verified Configurations

| OS | Compiler | Qt Version | Status |
|----|----------|------------|--------|
| Windows 11 | MinGW 13.1 | Qt 6.7.0 | ✅ Tested |
| Windows 10 | MinGW 11.2 | Qt 6.5.0 | ✅ Tested |
| macOS 11+ (Big Sur) - Apple Silicon | Apple Clang | Qt 6.5+ | ✅ Tested |
| macOS 11+ (Big Sur) - Intel | Apple Clang | Qt 6.5+ | ✅ Tested |

---

## Building on macOS

### Prerequisites

| Software | Installation |
|----------|-------------|
| Xcode Command Line Tools | `xcode-select --install` |
| Homebrew | See [brew.sh](https://brew.sh) |
| Qt6 | `brew install qt@6` |
| CMake | `brew install cmake` |
| Ninja (optional) | `brew install ninja` |

### Install Dependencies

```bash
# Required dependencies
brew install qt@6 cmake ninja pkg-config
brew install opencv gsl cfitsio libomp

# Optional - for XISF compression (Recommended)
brew install lz4 zstd

# Optional - for RAW image support (CR2, NEF, ARW, etc.)
brew install libraw

# Python for AI tools
brew install python@3.11
```

**Note on Architecture:**
- **Apple Silicon (M1/M2/M3/M4)**: Homebrew installs to `/opt/homebrew` (arm64)
- **Intel Macs**: Homebrew installs to `/usr/local` (x86_64)
- The build scripts automatically detect your architecture and use the correct paths

### Build Steps

```bash
# 1. Setup Python environment
chmod +x setup_python_macos.sh
./setup_python_macos.sh

# 2. Build the application
chmod +x src/build_macos.sh
./src/build_macos.sh

# Note: STEP 6 of build_macos.sh creates a symlink from src/scripts to the app bundle
# This allows development changes to scripts without rebuilding

# 3. Create distribution package
chmod +x src/package_macos.sh
./src/package_macos.sh

# 4. Create DMG installer (optional)
chmod +x src/build_installer_macos.sh
./src/build_installer_macos.sh
```

### Output

- **App Bundle**: `build/TStar.app`
- **Distribution**: `dist/TStar.app`
- **DMG Installer**: `installer_output/TStar_Setup_X.X.X.dmg`

### Notes

- **Apple Silicon (M1/M2/M3/M4)**: Fully supported with automatic architecture detection
- **Intel Macs**: Fully supported with automatic architecture detection
- **Universal Binaries**: The build system automatically creates architecture-specific builds
- **Homebrew Paths**: Build scripts automatically detect and use the correct Homebrew prefix:
  - Apple Silicon: `/opt/homebrew`
  - Intel: `/usr/local`
- **Gatekeeper**: On first run, right-click the app and select "Open" to bypass unsigned app warning
- **Notarization**: For distribution, consider notarizing with `xcrun notarytool`

### Optional Features

The macOS build supports several optional features that are automatically enabled when dependencies are found:

- **LibRaw**: RAW camera image support (CR2, NEF, ARW, DNG, etc.) - `brew install libraw`
- **LZ4**: Fast XISF compression - `brew install lz4`
- **Zstd**: High-ratio XISF compression - `brew install zstd`

These libraries are automatically detected by CMake and bundled into the app during packaging.

