# Changelog

All notable changes to GW2 AIO Manager will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial project structure and build system
- Core infrastructure (MumbleLink, GW2 detection, addon management)

## [0.1.0-alpha] - 2024-XX-XX

### Added

#### Core Features
- **Radial Menu System** - Mount, novelty, and marker wheels with customizable items
- **DPS Tracker** - Combat statistics from Mumble Link and ArcDPS integration
- **Marker System** - TacO-compatible marker pack support with 3D rendering
- **Blish-HUD Modules** - .NET module hosting with graphics bridge
- **Multi-Account Launcher** - Launch multiple GW2 instances with profiles

#### User Interface
- GW2-styled dark theme throughout
- Transparent overlay window
- System tray integration
- Settings page with tabs
- First-run setup wizard
- Marker pack browser

#### Infrastructure
- Auto-update system via GitHub releases
- Credential storage (obfuscated, DPAPI planned)
- File-based logging with rotation
- Crash handler with minidump generation
- Portable and installed modes
- Command-line arguments for automation

#### Integrations
- GW2 API client for account and character data
- Discord Rich Presence support
- Keybind conflict detection
- Settings backup/restore

#### Localization
- English (complete)
- German (partial)
- French (partial)
- Spanish (partial)

### Known Issues
- OpenGL marker rendering requires modern GPU
- Blish-HUD module compatibility is partial
- Some TacO marker behaviors not yet implemented

## [0.0.1] - Initial Development

### Added
- Project scaffolding
- CMake build system
- Qt6 + C++17 architecture

---

[Unreleased]: https://github.com/Foboryus/GW2-AIO/compare/v0.1.0-alpha...HEAD
[0.1.0-alpha]: https://github.com/Foboryus/GW2-AIO/releases/tag/v0.1.0-alpha
[0.0.1]: https://github.com/Foboryus/GW2-AIO/releases/tag/v0.0.1
