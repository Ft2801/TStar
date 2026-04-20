
<div align="center">
  <img src="src/images/Logo.png" alt="TStar Logo" width="200"/>
  <h1>TStar</h1>
  <h3>Professional Astrophotography Processing Suite</h3>

  ![License](https://img.shields.io/badge/license-MIT%20%7C%20GPL-blue.svg)
  ![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS-lightgrey.svg)
  ![Qt](https://img.shields.io/badge/Qt-6.5%2B-green.svg)
  ![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
  ![Version](https://img.shields.io/badge/version-1.8.7-orange.svg)
  ![Downloads](https://img.shields.io/github/downloads/Ft2801/TStar/total)
</div>

**Author:** Fabio Tempera

**Website:** [https://ft2801.github.io/TStar-Astronomy-Website/](https://ft2801.github.io/TStar-Astronomy-Website/)

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Tool Search & Discovery](#tool-search--discovery)
- [Tools and Functionalities](#tools-and-functionalities)
  - [File Formats and I/O](#0-file-formats-bit-depths--io)
  - [Stretching and Linear to Non-Linear](#1-stretching--linear-to-non-linear)
  - [Color Calibration and Correction](#2-color-calibration--correction)
  - [AI and Restoration](#3-ai--restoration)
  - [Channel Management](#4-channel-management)
  - [Masking](#5-masking)
  - [Image Pipeline (Preprocessing)](#6-image-pipeline-preprocessing)
  - [Utilities and Effects](#7-utilities--effects)
  - [Workspace and Views](#8-workspace--views)
  - [Scripting and Automation](#9-scripting--automation)
- [Translations](#translations)
- [Installation](#installation)
- [Building from Source](#building-from-source)
- [License](#license)
- [Acknowledgments](#acknowledgments)

## Overview

TStar is a high-performance, cross-platform image processing suite built with C++17 and Qt6, designed
specifically for the demands of astrophotography. It integrates a wide range of professional-grade signal
processing algorithms alongside modern AI-based restoration tools, enabling astrophotographers to produce
scientifically accurate, publication-quality images from raw sensor data.

The application targets both casual astrophotographers and advanced users who require precise numerical
control over every stage of their image processing workflow, from raw frame calibration through stacking,
stretching, color calibration, and final export.

## Key Features

| Feature | Description |
|---|---|
| **Native FITS/XISF Support** | Full read/write compatibility with 8, 16, 32-bit integer and 32-bit floating-point FITS and XISF files |
| **MDI Workspace** | Flexible Multi-Document Interface for simultaneous editing of multiple images |
| **Project Management** | Full workspace projects with dedicated working directories, state persistence, and recent-project workflow |
| **History Panel** | Per-image history panel integrated in the sidebar, showing all applied operations and one-click navigation to any past or future state |
| **Professional Astrometry** | Native plate solver and ASTAP integration with full WCS propagation to downstream tools |
| **Scientific Color Calibration** | PCC and SPCC workflows grounded in photometric and spectrophotometric data |
| **Automatic Image Annotation** | Catalog overlays (Messier, NGC, IC, LdN, Sh2, Stars, Constellations) with optional WCS RA/Dec grid |
| **Advanced View Management** | Aspect-ratio-aware subwindows, tiling modes, collapsed-view previews, and cursor magnifier |
| **AI Integration** | Seamless integration with GraXpert, Cosmic Clarity, and StarNet++ via a bundled Python environment |
| **Cross-Platform** | Fully supported on Windows (MinGW) and macOS (Intel and Apple Silicon) |
| **Scripting Engine** | Built-in TStar Script (.tss) runner for complex multi-step processing automation |
|**Easter egg minigame**| Space themed proprietary browser minigame accessible with Ctrl + M |
|**Tool Search & Discovery**| Integrated search panel for quick location and launch of any tool. |

## Tools and Functionalities

### 1. File Formats, Bit Depths & I/O

TStar provides comprehensive file format support across the entire astrophotography workflow.

**Supported Input Formats:**

| Category | Formats |
|---|---|
| Astronomical | FITS (.fits, .fit), XISF |
| Raster | TIFF/TIF (8/16/32-bit), PNG, JPG/JPEG, BMP |
| Camera RAW | CR2, CR3, CRW, NEF, NRW, ARW, DNG, ORF, ORI, RW2, RAF, PEF, PTX, RAW, RWL, MRW, SRW, ERF, MEF, MOS, X3F |

**Supported Output Formats:** FITS, XISF, TIFF, PNG, JPG

**Additional I/O Capabilities:**

- **Multi-Image Containers** -- FITS multi-extension and XISF multi-image files are fully supported, with per-extension or per-image loading dialogs.
- **Bit-Depth Aware Save** -- 8, 16, and 32-bit integer, as well as 32-bit floating-point save pipelines for scientific formats. Display-oriented export for PNG and JPG.
- **RAW Handling** -- Native RAW loading via LibRaw (when available), with Bayer pattern metadata tracking and a full debayer pipeline.
- **Header and Metadata Preservation** -- FITS/XISF metadata propagation through processing steps, WCS persistence and editing, ICC profile support, and project snapshot persistence.
- **SER File Support** -- Sequential SER video file reading for planetary and lucky imaging workflows.

### 2. Stretching & Linear to Non-Linear

Tools for transforming raw linear sensor data into perceptually viewable images.

| Tool | Description |
|---|---|
| **Statistical Auto Stretch** | Automatically stretches based on image statistics. Suitable for quick previews and as a starting point for manual refinement. |
| **Generalized Hyperbolic Stretch (GHS)** | State-of-the-art parametric stretch with independent control over shadow point, midtone focus, and highlight behavior. |
| **Histogram Transformation** | Classic levels-style adjustment with real-time logarithmic histogram preview. |
| **Curves Transformation** | Precision spline-based contrast and color adjustment using interactive curve handles per channel. |
| **ArcSinh Stretch** | Color-preserving stretch that boosts saturation in proportion to signal intensity while compressing dynamic range. |
| **Star Stretch** | Specialized stretch for isolated star layers, typically used in conjunction with star masks or starless/stars workflow. |

### 3. Color Calibration & Correction

| Tool | Description |
|---|---|
| **Photometric Color Calibration (PCC)** | Plate-solves the image and matches star photometry against Gaia and APASS catalogs for physically grounded color correction. |
| **Spectrophotometric Color Calibration (SPCC)** | Extends PCC with SED-based calibration using filter transmission curves and sensor response data for spectrally coherent results. |
| **PCC Distribution** | Visualizes the color distribution of reference stars following calibration to assess calibration quality. |
| **Catalog Background Extraction (CBE)** | Advanced gradient extraction using DSS2 or similar catalog reference images to build a highly accurate background model. |
| **Auto Background Extraction (ABE)** | Automated polynomial or RBF background model generation for light pollution and gradient removal. |
| **Background Neutralization** | Removes residual color casts by aligning background channel levels to a common reference. |
| **Temperature / Tint** | Manual white balance adjustment via correlated color temperature and green/magenta tint sliders, with shadows and highlights protection. |
| **Magenta Correction** | Dedicated suppression of magenta casts that commonly appear in broadband deep-sky data. |
| **Selective Color Correction** | Precise per-hue-range adjustment of Cyan/Magenta/Yellow, RGB, Lightness, Saturation, and Chrominance. |
| **SCNR** | Subtractive Chromatic Noise Reduction for targeted removal of green or magenta color noise artifacts. |
| **Saturation** | Global saturation adjustment with optional luminance-preservation weighting. |
| **Workspace Color Management** | ICC profile-aware color pipeline applied consistently across all tools and sessions within a workspace project. |

### 4. AI & Restoration

TStar integrates with leading external AI tools through a managed Python bridge. The bundled Python
environment ensures all AI features are self-contained and do not require user-managed dependencies.

| Tool | Description |
|---|---|
| **Cosmic Clarity** | Deep-learning noise reduction and sharpening with separate stellar and nebulosity modes. Model download is integrated into the Settings dialog. |
| **GraXpert** | AI-powered gradient removal. TStar launches GraXpert as a background process and exchanges images via the bridge script. |
| **StarNet++** | Automated star removal to produce starless images for independent background and nebulosity processing. |
| **Aberration Remover (RAR)** | Corrects chromatic aberration, star elongation, and residual optical artifacts using learning-based approaches. |

### 5. Channel Management

| Tool | Description |
|---|---|
| **Extract / Combine Channels** | Split an RGB image into individual monochrome channels or combine separate monochrome frames into an RGB composite. |
| **Linear Fit** | Equalizes RGB channel intensities by matching their median values, useful for correcting channel imbalance before combining. |
| **Debayer** | Converts CFA (Bayer pattern) raw data to full-color RGB with selectable demosaicing algorithms including Bilinear, VNG, AHD, RCD, and Markesteijn for X-Trans sensors. |
| **Extract / Recombine Luminance** | Independently extracts the luminance channel for processing and blends it back using configurable luminance-to-RGB ratios. |
| **Remove Pedestal** | Automatically detects and subtracts the sensor black floor value to eliminate artificial brightness offsets. |
| **Image Blending** | Merge two images with Photoshop-style blending modes and range/feathering masks for precise compositing control. |
| **Star Recomposition** | Combines a starless image with a stars-only image using configurable blending modes and strength parameters. |
| **Perfect Palette Picker** | Maps narrowband data (SHO, HOO, SHH, and custom) to artistic color palettes with real-time preview. |
| **Continuum Subtraction** | Enhances narrowband detail by subtracting broadband continuum/star light contributions from emission line data. |
| **Align Channels** | Star-registration-based alignment of multiple open images (narrowband, RGB) to sub-pixel accuracy. |
| **NB to RGB Stars** | Blends narrowband star data with RGB star channels for natural-looking star colors in narrowband composites. |
| **Narrowband Normalization** | Balances narrowband channels for compositing with configurable highlight recovery to prevent channel clipping. |
| **Multiscale Decomposition** | Decomposes an image into wavelet scale layers for independent per-scale contrast and detail editing. |

### 6. Masking

TStar's masking system supports multi-layer, type-specific masks that integrate with the processing
pipeline across all compatible tools.

**Mask Generation Methods:**

| Type | Description |
|---|---|
| **Luminance Mask** | Generated from the image's luminance channel with adjustable range and fuzziness. |
| **Chrominance Mask** | Targets areas of specific color saturation levels. |
| **Star Mask** | Generated from automatic star detection output, useful for protecting or isolating stars. |
| **Color Mask** | Hue-based selection for Red, Orange, Yellow, Green, Cyan, Blue, Violet, and Magenta ranges with optional inversion. |
| **Range Selection Mask** | Lower and upper luminance limits with fuzziness and linked-limit controls for precise tonal targeting. |
| **Manual Mask** | Polygon and shape drawing tools for custom user-defined regions. |

**Mask Operations:**
- Invert, blur, and overlay visibility controls
- Apply, remove, and preview overlays directly from the toolbar
- Binary and continuous mask types
- Per-tool mask assignment and management

### 7. Image Pipeline (Preprocessing)

TStar includes a complete preprocessing and stacking suite for calibrating and integrating raw light frames.

| Stage | Description |
|---|---|
| **Image Calibration** | Calibrate raw light frames using master Bias, Dark, and Flat frames with configurable subtraction and division order. |
| **Cosmetic Correction** | Hot and cold pixel detection and replacement using threshold-based or statistical criteria. |
| **Image Registration** | Star-based sub-pixel alignment with support for translation, rotation, and scale transformations. |
| **Image Stacking** | Multi-frame integration using Average, Median, Kappa-Sigma Clipping, or Winsorized Sigma Clipping methods. |
| **Drizzle Stacking** | Full drizzle integration and fast drizzle mode for undersampled datasets, improving resolution and sampling. |
| **Normalization** | Frame-level and overlap-based normalization to equalize sky background levels before integration. |
| **Weighting** | Signal-to-noise and quality-based frame weighting for optimal integration contribution. |

### 8. Utilities & Effects

| Tool | Description |
|---|---|
| **Plate Solving** | Native triangle-matching solver and ASTAP integration. Writes full WCS solution to FITS/XISF headers. |
| **PixelMath** | Expression-based arithmetic engine for operations between open images, supporting multi-image formulas. |
| **Star Analysis** | Measures FWHM, eccentricity, SNR, and other star profile metrics across a configurable grid of sample positions. |
| **Star Halo Removal** | Detects and subtracts halos around bright stars to improve image clarity and reduce blooming artefacts. |
| **Morphology** | Applies morphological operations (Erosion, Dilation, Opening, Closing) to modify the shape and extent of image structures. |
| **Image Annotator** | Manual and automatic catalog annotation with Messier, NGC, IC, LdN, Sh2, star names, constellation lines, and optional WCS grid. Supports burn-in export. |
| **CLAHE** | Contrast Limited Adaptive Histogram Equalization for local contrast enhancement without global clipping. |
| **Wavescale HDR** | Multiscale High Dynamic Range compression to manage bright region detail while enhancing faint structures. |
| **Aberration Inspector** | 3x3 grid display for evaluating optical quality and star shape across the full image field. |
| **Blink Comparator** | Fast alternating display of two open views for visual comparison of registration quality, noise, or detail differences. |
| **Correction Brush** | Interactive pixel-level artifact removal using Content-Aware AI or standard median-sampling approaches. |
| **Rotate & Crop** | Precision rotation and cropping with aspect ratio constraints, individual pixel accuracy, and batch processing support. |
| **Image Binning** | Reduces image dimensions by combining adjacent pixels to improve signal-to-noise ratio or reduce file size. |
| **Image Upscale** | Enlarges images using selectable interpolation methods: Nearest Neighbor, Bilinear, Bicubic, or Lanczos4. |
| **WCS Mosaic Builder** | Assembles multiple plate-solved images into a single seamless mosaic.|
| **RAW to FITS Converter** | Batch converts camera RAW files (Canon, Nikon, Sony, and others) to FITS format. |
| **FITS Header Editor** | Full FITS keyword viewer and editor with import from another file support. |
| **AstroSpike** | Generates artificial diffraction spikes around bright stars for aesthetic effect. |
| **RawEditor** | Non-destructive light and color editor (exposure, contrast, highlights, shadows, white balance) for linear image development. |
| **Image Stats Widget** | Panel widget that computes and displays global pixel statistics, FWHM, roundness, and photometry. |
| **Welcome Dialog** | Startup screen displaying a quick start guide and preliminary setup instructions for new users. |

### 9. Workspace & Views

| Feature | Description |
|---|---|
| **Workspace Projects** | Per-project working directory, tool state persistence, and recent project quick-open. Sidebar with console log and header inspector. |
| **Aspect Ratio Aware Windows** | New image views open with the native aspect ratio of the image rather than a fixed window shape. |
| **Tiling Modes** | Tile all views in a grid, horizontally, or vertically with a single command. |
| **Collapsed View Previews** | Right-side panel showing thumbnails of all minimized or collapsed image windows for quick navigation. |
| **Hide Minimized Views Toggle** | Option to completely hide minimized views from the MDI area to keep the workspace uncluttered. |
| **Cursor Magnifier** | Follow-cursor loupe window for precise focus, sharpness, and detail inspection at high zoom. |
| **Display Transform Modes** | Linear, Auto Stretch, Histogram, ArcSinh, Square Root, and Logarithmic display modes selectable per view. |
| **Display Controls in Toolbar** | RGB link/unlink, per-channel inspection (R/G/B), color inversion, false-color visualization, and burn-display-to-buffer. |
| **AutoStretch Target Median** | Configurable target median presets that control how aggressively the display stretch is applied. |
| **Linked Views** | Optional synchronized zoom and pan across all open image windows for side-by-side comparison. |

### 10. Scripting & Automation

TStar includes a built-in scripting engine that enables automated multi-step processing workflows.

| Feature | Description |
|---|---|
| **TStar Scripts (.tss)** | Custom scripts written in the TStar Script language that can invoke any processing tool with configurable parameters. |
| **Script Runner** | Integrated script editor and runner with output logging and error reporting. |
| **Stacking Automation Scripts** | Pre-built scripts for common stacking workflows (calibration, registration, integration). |
| **Script Browser** | File browser for discovering and launching scripts from a centralized or project-local script directory. |

## Translations

TStar is available in multiple languages. Translation files are located in the `translations/` directory.

| Language | Code | Status |
|---|---|---|
| English | `en` | Default |
| German | `de` | Available |
| Spanish | `es` | Available |
| French | `fr` | Available |
| Italian | `it` | Available |

## Installation

### Windows

TStar for Windows x64 is distributed as a self-contained installer package.

1. Download the latest release installer (`TStar_Setup_<version>.exe`) from the releases page.
2. Run the installer and follow the on-screen instructions.
3. Launch TStar from the Start Menu or the desktop shortcut.

No additional runtime dependencies are required. The installer bundles Qt, the Python environment, and all necessary libraries.

### macOS

TStar for macOS is distributed as a signed DMG installer package for both Apple Silicon and Intel architectures.

1. Download the latest macOS release DMG.
2. Select the build that matches your processor:
   - **Apple Silicon (M1/M2/M3/M4):** arm64 build
   - **Intel Mac:** x86_64 build
3. Open the DMG and drag TStar into your Applications folder.
4. On first launch, if macOS Gatekeeper blocks the app, right-click the application and select **Open** to bypass the unsigned app warning.

## Building from Source

Full build instructions for all supported platforms are documented in [BUILDING.md](BUILDING.md).

### Quick Start - Windows

```bash
# Open a Qt MinGW 64-bit terminal, then:
mkdir build && cd build
cmake -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/mingw_64" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release -j4
```

Or use the all-in-one build script:

```bash
src/build_all.bat
```

### Quick Start - macOS

```bash
# Install dependencies first (one-time setup):
./setup_python_macos.sh

# Build the application:
chmod +x ./src/build_macos.sh
./src/build_macos.sh
```

See [BUILDING.md](BUILDING.md) for complete dependency setup, deployment packaging, and DMG creation instructions.

## License

Copyright (c) 2026 Fabio Tempera.

This project's original code is licensed under the MIT License. See the [LICENSE](LICENSE) file for the full text. 
**Note:** Some modules within TStar (e.g., `src/stacking`) incorporate algorithms and code inspired by or derived from [Siril](https://siril.org/), which is released under the **GNU General Public License v3.0 (GPLv3)**. A copy of the GPLv3 license is included in the project as `LICENSE-GPL`, and distribution of compiled binaries incorporating this code must abide by the terms of the GPLv3.

## Acknowledgments

TStar is built on top of the following open-source projects and libraries:

| Library | Purpose |
|---|---|
| [Qt6](https://www.qt.io/) | UI framework and cross-platform abstraction |
| [CFITSIO](https://heasarc.gsfc.nasa.gov/fitsio/) | FITS file I/O |
| [OpenCV](https://opencv.org/) | Image processing and computer vision |
| [GSL](https://www.gnu.org/software/gsl/) | Scientific computing and numerical methods |
| [LibRaw](https://www.libraw.org/) | Camera RAW file decoding |
| [LittleCMS2](https://www.littlecms.com/) | ICC color profile management |
| [LZ4](https://lz4.github.io/lz4/) | Fast compression for XISF |
| [Zstd](https://facebook.github.io/zstd/) | High-ratio compression for XISF |

Additionally, some of the contents and functionalities in TStar have been implemented taking inspiration from [Siril](https://siril.org/) and [Seti Astro Suite](https://www.setiastro.com/). Please see `INSPIRATION.md` for more details.

