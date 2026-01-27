# Contributing to TStar

Thank you for your interest in contributing to TStar! This document provides guidelines for contributing.

## How to Contribute

### Reporting Bugs

1. Check [existing issues](../../issues) to avoid duplicates
2. Use the bug report template
3. Include:
   - TStar version
   - Operating system
   - Steps to reproduce
   - Expected vs actual behavior
   - Sample images if relevant (use small test files)

### Suggesting Features

1. Open an issue with the feature request template
2. Describe the use case and expected behavior
3. Consider if it fits the project scope (astrophotography processing)

### Code Contributions

1. **Fork** the repository
2. **Create a branch** for your feature: `git checkout -b feature/your-feature`
3. **Write code** following the style guidelines below
4. **Test** your changes thoroughly
5. **Commit** with clear messages
6. **Push** and create a Pull Request

## Code Style

### C++ Guidelines

- **Standard**: C++17
- **Naming**:
  - Classes: `PascalCase`
  - Functions/Methods: `camelCase`
  - Member variables: `m_camelCase`
  - Constants: `UPPER_SNAKE_CASE`
- **Formatting**: 4 spaces indentation, no tabs
- **Qt**: Follow Qt naming conventions for signals/slots

### Optional Features

TStar supports several optional features that are enabled when dependencies are detected:

- **LibRaw Support**: RAW camera image format support (CR2, NEF, ARW, DNG, etc.)
  - Enabled with `HAVE_LIBRAW` flag when LibRaw is found
  - Windows: Place in `deps/libraw/`
  - macOS: `brew install libraw`
  
- **XISF Compression**: Advanced compression for XISF astronomical image format
  - **LZ4**: Fast compression - enabled with `HAVE_LZ4` flag
  - **Zstd**: High-ratio compression - enabled with `HAVE_ZSTD` flag
  - Windows: Place in `deps/lz4/` and `deps/zstd/`
  - macOS: `brew install lz4 zstd`

When contributing code that uses these features, always guard usage with the appropriate preprocessor flags:
```cpp
#ifdef HAVE_LIBRAW
    // LibRaw-specific code
#endif

#ifdef HAVE_LZ4
    // LZ4 compression code
#endif

#ifdef HAVE_ZSTD
    // Zstd compression code
#endif
```

### Python Guidelines

- **Usage**: Only for bridge/worker scripts or AI integrations.
- **Portability**: Must be compatible with Python 3.11.
- **Dependencies**: New dependencies must be added to `setup_python_dist.ps1` (Windows) or `setup_python_macos.sh` (macOS) and approved.
- **Location**: All scripts must reside in `src/scripts`.
- **Style**: Follow PEP 8 where possible.
- **Python Discovery**: C++ code searches for Python in bundled → development → system PATH locations. Ensure scripts work with bundled virtualenv paths.

### Translations

Translations are managed via Python dictionaries in `tools/trans_data.py`. If you add a new user-facing string in C++, add its translation entries to this file. The `translate_manager.py` script uses this dictionary to generate `.ts` files for Qt Linguist.

### macOS Development

When developing on macOS, be aware of architecture-specific considerations:

- **Architecture Detection**: Build scripts automatically detect whether you're on Apple Silicon (arm64) or Intel (x86_64)
- **Homebrew Paths**: Dependencies are pulled from architecture-specific Homebrew paths:
  - Apple Silicon: `/opt/homebrew`
  - Intel: `/usr/local`
- **Testing Across Architectures**: If you have access to both Intel and Apple Silicon Macs:
  - Test builds on both architectures when modifying low-level code or dependencies
  - Verify that the `package_macos.sh` script correctly bundles libraries for your architecture
  - Check that the app launches and functions correctly on both platforms
- **Architecture Verification**: You can check the built executable's architecture:
  ```bash
  lipo -info build/TStar.app/Contents/MacOS/TStar
  # or
  file build/TStar.app/Contents/MacOS/TStar
  ```


### Example

```cpp
class ImageProcessor {
public:
    void processImage(const ImageBuffer& buffer);
    
private:
    int m_threadCount;
    static const int MAX_ITERATIONS = 100;
};
```

### Commit Messages

- Use present tense: "Add feature" not "Added feature"
- First line: 50 chars max, imperative mood
- Body: Wrap at 72 chars, explain *what* and *why*

```
Add GHS histogram live preview

Implement real-time histogram updates during GHS parameter
adjustment to provide immediate visual feedback.
```

## Pull Request Process

1. Update documentation if needed
2. Add yourself to contributors if desired
3. Ensure the build passes
4. Request review from maintainers
5. Address feedback promptly

## Development Tips

- Use Qt Creator for the best development experience
- Run with debug symbols to catch issues early
- Test with various image types (FITS 16-bit, 32-bit float, TIFF)

## Questions?

Open a discussion or issue — we're happy to help!
