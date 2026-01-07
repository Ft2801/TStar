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

### Python Guidelines

- **Usage**: Only for bridge/worker scripts or AI integrations.
- **Portability**: Must be compatible with Python 3.11.
- **Dependencies**: New dependencies must be added to `setup_python_dist.ps1` (Windows) or `setup_python_macos.sh` (macOS) and approved.
- **Location**: All scripts must reside in `src/scripts`.
- **Style**: Follow PEP 8 where possible.
- **Python Discovery**: C++ code searches for Python in bundled → development → system PATH locations. Ensure scripts work with bundled virtualenv paths.

### Translations

Translations are managed via Python dictionaries in `tools/trans_data.py`. If you add a new user-facing string in C++, add its translation entries to this file. The `translate_manager.py` script uses this dictionary to generate `.ts` files for Qt Linguist.


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
