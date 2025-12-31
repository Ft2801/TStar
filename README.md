
# TStar

**Author:** Fabio Tempera

**TStar** is a comprehensive image processing application designed for astrophotography. Built on the Qt6 framework and C++17, it provides a suite of professional tools for signal processing, gradient reduction, and photometric analysis.

## Overview

TStar offers a streamlined workflow for processing high-dynamic-range astronomical data. It combines traditional mathematical transformations with modern AI-assisted tools to facilitate the production of scientific-grade images. The interface is designed to maximize screen real estate for data visualization while providing quick access to complex processing algorithms.

## Key Features

### Image Calibration and Stretching
*   **Generalized Hyperbolic Stretch (GHS):** Advanced non-linear stretching algorithm allowing independent control over shadow protection, highlight preservation, and midtone contrast.
*   **Histogram Transformation:** Standard levels adjustment with real-time preview and logarithmic visualization.
*   **Curves Transformation:** Spline-based tonal manipulation for precise contrast adjustments.

### Background Correction
*   **Automatic Background Extraction (ABE):** Models and subtracts complex light pollution gradients using polynomial and RBF (Radial Basis Function) interpolation.
*   **Background Neutralization:** Aligns color channels to a neutral background reference to remove chromatic casts.
*   **GraXpert Integration:** Seamless support for AI-based gradient removal using external GraXpert models.

### Noise Reduction and sharpening
*   **SCNR (Subtractive Chromatic Noise Reduction):** Removes green or magenta color noise without affecting structural detail.
*   **CosmicClarity Integration:** Deep learning-based denoising and sharpening.
*   **Restoration Filters:** Deconvolution and wavelet-based sharpening tools.

### Star Processing
*   **StarNet++ Integration:** Automated star removal to separate nebulae/galaxies from star fields for independent processing.
*   **Plate Solving:** Astrometric calibration to identify celestial coordinates and objects within the field of view.
*   **Photometric Color Calibration (PCC):** color balances images based on the photometric data of stars from the APASS and Gaia catalogs.

### Utilities
*   **PixelMath:** High-performance expression engine for arithmetic operations between images (e.g., channel combination, masking).
*   **MDI Interface:** Multi-Document Interface allowing multiple images and processing versions to be open simultaneously.
*   **Native FITS Support:** Full compatibility with the FITS standard (8, 16, 32-bit integer and floating point).

### Masking Tools
*   **Mask Creation:** Interactive dialog for drawing freehand polygons or ellipses to define mask regions.
*   **Advanced Mask Generation:** Programmatic mask generation based on Lightness, Chrominance, Star Detection, or specific Color Hue ranges.
*   **Mask Application:** Seamlessly apply masks to protect or isolate regions during image processing. Masks support inversion, opacity control, and "protect" modes.

## Installation

### Windows
TStar is distributed as a standalone portable application for Windows x64 systems.

1.  Download the latest distribution package from the **Releases** section.
2.  Extract the archive to a preferred location.
3.  Run `TStar.exe`.

*Note: Python environments required for scripting and AI tools are now bundled with the application.*

## Building from Source

For developers or Linux users effectively building from source, please refer to the `BUILDING.md` file in the repository root for detailed dependencies and compilation instructions.

### Quick Build (Windows PowerShell)
The repository includes scripts to automate the build process on Windows:

```powershell
.\setup_python_dist.ps1  # Sets up the local Python runtime
.\build_all.bat          # Compiles the application
.\package_dist.bat       # Creates the distribution package
```

## License and Copyright

**Copyright © 2026 Fabio Tempera.**

This software is provided "as is", without warranty of any kind. See the `LICENSE` file for full terms and conditions.

## Acknowledgments
*   NASA's HEASARC for the CFITSIO library.
*   The Qt Company for the Qt Application Framework.
