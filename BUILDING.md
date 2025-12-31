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
│   └── gsl/
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

### Python Environment (New!)
TStar now requires a local Python environment for AI tools and specialized processing scripts.

```powershell
# Run the automated setup script
powershell -ExecutionPolicy Bypass -File setup_python_dist.ps1
```

This will download an embeddable Python 3.11 and install necessary libraries (`numpy`, `astropy`, `onnxruntime`, etc.) to the `deps/python` folder.

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
5. **Bundles the Python environment** into the `dist/TStar/python` subfolder.
6. **Centralizes scripts** from `src/scripts` into `dist/TStar/scripts`.
7. Generates a `README.txt` for the end user.

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
