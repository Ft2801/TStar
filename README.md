<div align="center">
  <img src="src/images/Logo.png" alt="TStar Logo" width="200"/>
  <h1>TStar</h1>
  <h3>Professional Astrophotography Processing Suite</h3>
</div>

**Author:** Fabio Tempera

**License:** MIT License

**Contributors:**
* [Tim Dicke](https://github.com/dickett): testing of the MacOS versions
* Miroslav Bakoš: testing of the Windows version

### Live TStar website:
https://ft2801.github.io/TStar-Website/

## Overview

TStar is a powerful, C++17/Qt6-based image processing platform explicitly designed for astrophotography. It combines high-performance signal processing algorithms with modern AI-based restoration tools to help astrophotographers produce scientific-grade images from their raw data.

## Key Features

*   **Native FITS Support**: Full compatibility with 8, 16, 32-bit integer and floating-point FITS files.
*   **MDI Workspace**: Flexible Multi-Document Interface allowing simultaneous editing of multiple images.
*   **Project Management**: Full workspace projects with dedicated working directories and recent-project workflow. Sidebar with console logging and header inspector
*   **Professional Astrometry**: Native + ASTAP plate solving with WCS-aware downstream tools.
*   **Scientific Color Calibration**: PCC and SPCC workflows for physically grounded color correction.
*   **Automatic Annotation Engine**: Catalog overlays (Messier/NGC/IC/LdN/Sh2/Stars/Constellations) with optional WCS grid.
*   **Advanced View Management**: Aspect-ratio-aware subwindows, tiling modes, collapsed-view previews, and magnifier.
*   **Cross-Platform**: Fully supported on Windows, macOS (Intel & Apple Silicon), with optimized performance.

## Tools & Functionalities

### 1. Stretching & Linear to Non-Linear
Tools to transform raw linear data into viewable images.
*   **Auto Stretch (Statistical)**: Automatically stretches the image based on statistical analysis. Ideal for quick previews.
*   **Generalized Hyperbolic Stretch (GHS)**: State-of-the-art stretching with independent control over shadows, midtones, and highlights.
*   **Histogram Transformation**: Classic levels adjustment with real-time logarithmic preview.
*   **Curves Transformation**: Precision spline-based contrast / color adjustment.
*   **ArcSinh Stretch**: Color-preserving stretch that boosts saturation while stretching.
*   **Star Stretch**: Specialized tool for stretching stars independently (often used with star masks).

### 2. Color Calibration & Correction
*   **Photometric Color Calibration (PCC)**: Solves the image plate and calibrates colors based on Gaia/APASS photometric star catalogs.
*   **Spectrophotometric Color Calibration (SPCC)**: Spectral-response based calibration using SED/filter/sensor data for physically coherent color rendering.
*   **PCC Distribution**: Visualizes the color distribution of stars after calibration.
*   **Catalog Background Extraction (CBE)**: Advanced background extraction using catalog reference images (DSS2, etc.) for highly accurate gradient models.
*   **Auto Background Extraction (ABE)**: Models and removes light pollution gradients using polynomial or RBF interpolation.
*   **Background Neutralization**: Removes color casts by aligning background channels.
*   **Temperature / Tint**: Adjust manual white balance by shifting color tones and green/magenta tints.
*   **Magenta Correction**: Dedicated magenta cast suppression for deep-sky workflows.
*   **Selective Color Correction**: Precise adjustment of CMY/RGB/L/S/C within specific hue ranges.
*   **SCNR (Subtractive Chromatic Noise Reduction)**: Removes generic Green/Magenta color noise.
*   **Saturation**: Adjust color intensity with luminance preservation options.
*   **Workspace Color Management**: Consistent color behavior across tools/sessions in the project workspace.

### 3. AI & Restoration
*   **Cosmic Clarity**: Deep-learning based noise reduction and sharpening.
*   **GraXpert Integration**: Seamless bridge to run GraXpert for AI gradient removal.
*   **StarNet++ Integration**: automated star removal to create starless images for separate processing.
*   **Aberration Remover (RAR)**: Corrects chromatic aberration and star halos.

### 4. Channel Management
*   **Extract / Combine Channels**: Split RGB into Mono or combine Mono into RGB.
*   **Linear Fit**: Equalize RGB channel intensities by matching medians.
*   **Debayer**: Convert RAW Bayer pattern images to full color.
*   **Extract / Recombine Luminance**: Independent extraction and blending of the luminance channel.
*   **Remove Pedestal**: Automatically detects and subtracts the minimum pixel value (black floor).
*   **Star Recomposition**: Advanced tool to merge starless and stars images back together with blending modes.
*   **Perfect Palette Picker**: Mapping narrowband data (SHO, HOO, etc.) to artistic color palettes with real-time preview.
*   **Continuum Subtraction**: Enhances narrowband details by subtracting broadband continuum/star light.
*   **Align Channels**: Align multiple open images (narrowband/RGB) using star registration.
*   **NB -> RGB Stars**: Specialized tool to blend narrowband star channels with RGB star data.
*   **Narrowband Normalization**: Balances narrowband channels for compositing with highlight recovery.
*   **Multiscale Decomposition**: Decomposes images into wavelet layers for scale-specific editing.

### 5. Masking
*   **Mask Generation**: Create masks based on Luminance, Chrominance, Star Detection, or Specific Color Hues.
*   **Manual Masking**: Draw masks using polygons or shapes.
*   **Mask Tools**: Invert, Blur, and Overlay visibility controls.

### 6. Image Pipeline (Preprocessing)
*   **Image Calibration**: Calibrate raw light frames using master Bias, Dark, and Flat frames.
*   **Image Registration**: Star-based alignment with sub-pixel accuracy (translation, rotation, scale).
*   **Image Stacking**: Noise reduction using Average, Median, Kappa-Sigma, or Winsorized Sigma stacking.
*   **Fast Drizzle Mode**: Accelerated drizzle option for undersampled datasets.

### 7. Utilities & Effects
*   **Plate Solving**: Native + ASTAP astrometric solving for WCS metadata, with catalog/database-aware workflows.
*   **PixelMath**: Powerful expression engine for arithmetic operations between images.
*   **Star Analysis**: Measure FWHM, eccentricity, and other star profile metrics.
*   **Star Halo Removal**: Tool to detect and subtract halos around bright stars, improving image clarity.
*   **Morphology**: Modify shape and size of image structures (Erosion, Dilation, etc.).
*   **Image Annotator**: Manual + automatic annotation tool with catalog overlays and optional WCS RA/Dec grid; supports burn-in export.
*   **CLAHE**: Contrast Limited Adaptive Histogram Equalization for local contrast enhancement.
*   **Wavescale HDR**: Multiscale High Dynamic Range compression.
*   **Aberration Inspector**: 3x3 grid display for evaluating optical quality across the field.
*   **Blink Comparator**: Fast visual alternation between two views for registration/noise/detail checks.
*   **Correction Brush**: Interactive artifact removal using Content-Aware AI or standard median sampling.
*   **Rotate & Crop**: Precision cropping with aspect ratio constraints and batch processing support.
*   **RAW to FITS Converter**: Batch convert camera RAW files (Canon, Nikon, Sony, etc.) to FITS.
*   **FITS Header Editor**: View and modify FITS metadata keywords.
*   **RAW Editor**: Lightroom-style light/color controls for image development.
*   **AstroSpike**: Generates artificial diffraction spikes for aesthetic effect.

### 9. Workspace & Views
*   **Workspace Projects**: Per-project working directory, state persistence, and recent projects.
*   **Aspect Ratio Aware Windows**: New views open with image-native proportions.
*   **Tiling Modes**: Tile all views (grid, horizontal, vertical).
*   **Collapsed View Previews**: Right panel thumbnails for minimized/shaded windows.
*   **Hide Minimized Views Toggle**: Keep workspace clean while preserving quick access.
*   **Cursor Magnifier**: Follow-cursor loupe for precise focus/detail checks.

### 8. Scripting & Automation
*   **TStar Scripts**: Built-in processing workflows for common tasks.
*   **Script Runner**: Write and run custom TStar scripts (.tss) for complex multi-step automation.

## Translations
TStar is available in multiple languages:
*   🇬🇧 **English (en)** - Default
*   🇩🇪 **German (de)**
*   🇪🇸 **Spanish (es)**
*   🇫🇷 **French (fr)**
*   🇮🇹 **Italian (it)**

*Translations are located in the `translations/` folder.*

## Installation

### Windows
TStar is distributed as a portable Windows x64 application.
1. Download the latest release.
2. Extract the archive.
3. Run `TStar.exe`.

*Note: Python environments for AI tools (StarNet, etc.) and required MSVC Redistributable DLLs are bundled internally. Very old processors (Intel 3rd gen or older, ancient AMDs) lacking AVX instructions may not support certain AI features.*

## Building from Source

### Windows
See [BUILDING.md](BUILDING.md) for detailed Windows build instructions using MinGW.

### macOS
See [BUILDING.md](BUILDING.md) for detailed macOS build instructions. Both Intel and Apple Silicon architectures are fully supported (macOS 12+ recommended, but older versions are supported).

For quick start:
```bash
chmod +x ./src/build_macos.sh
./src/build_macos.sh
```

## License
Copyright © 2026 Fabio Tempera.

This project is licensed under the MIT License - see the `LICENSE` file for details.

## Acknowledgments
*   **Qt Framework** for the UI engine.
*   **CFITSIO** for FITS file handling.
*   **OpenCV** for image processing internals.
*   **CCfits** for C++ FITS wrappers.
