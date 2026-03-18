# Contributing to GW2 AIO Manager

Thank you for your interest in contributing! This document provides guidelines
for contributing to the project.

## Code of Conduct

Be respectful and constructive. We're all here because we love GW2!

## Getting Started

### Prerequisites

- **Windows 10/11** (required for GW2)
- **Visual Studio 2022** with C++ workload
- **Qt 6.6+** (via vcpkg or Qt installer)
- **CMake 3.20+**
- **Git**

### Building

```bash
# Clone repository
git clone https://github.com/Foboryus/GW2-AIO.git
cd gw2-aio

# Install Qt via vcpkg (if not using Qt installer)
vcpkg install qtbase:x64-windows qtdeclarative:x64-windows

# Configure and build
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

### Running Tests

```bash
cd build/tests
ctest -C Release --output-on-failure
```

## How to Contribute

### Reporting Bugs

1. Check if the issue already exists
2. Use the bug report template
3. Include:
   - GW2 AIO version
   - Windows version
   - Steps to reproduce
   - Expected vs actual behavior
   - Screenshots if applicable

### Suggesting Features

1. Check existing feature requests
2. Use the feature request template
3. Explain the use case clearly

### Pull Requests

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Follow the coding style (see below)
4. Add tests if applicable
5. Update documentation
6. Submit PR with clear description

## Coding Style

### C++ Guidelines

```cpp
// Class names: PascalCase
class MarkerRenderer;

// Functions and methods: camelCase
void renderMarkers();

// Member variables: m_prefix
int m_markerCount;

// Constants: SCREAMING_CASE
static constexpr int MAX_MARKERS = 1000;

// Use modern C++17 features
auto value = std::optional<int>{42};
if (auto it = map.find(key); it != map.end()) { }
```

### File Organization

```
src/
├── core/        # Core infrastructure
├── features/    # Feature modules
│   ├── radial/
│   ├── dps/
│   ├── markers/
│   └── modules/
├── ui/          # User interface
└── models/      # Data models
```

### Header Files

- Use `#pragma once`
- Include what you use
- Forward declare when possible
- Keep implementations inline for header-only components

## Feature Areas

### Easy (Good First Issues)
- UI improvements
- Localization
- Documentation
- Bug fixes

### Medium
- New radial menu commands
- Marker behaviors
- Settings options
- API integrations

### Hard
- OpenGL rendering
- .NET interop
- Performance optimization
- Cross-platform support

## Testing

- Add unit tests for new logic
- Test with different GW2 settings
- Test multi-account scenarios
- Check memory usage with large marker packs

## Documentation

- Update README for new features
- Add inline documentation
- Update CHANGELOG.md
- Create wiki pages for complex features

## License

By contributing, you agree that your contributions will be licensed under
the MIT License.

## Questions?

- Open a discussion on GitHub
- Join our Discord (if available)

Thank you for contributing! 🎮
