# GW2 AIO Manager

🎮 **All-in-One Overlay Manager for Guild Wars 2**

[![Build](https://github.com/Foboryus/GW2-AIO/actions/workflows/build.yml/badge.svg)](https://github.com/Foboryus/GW2-AIO/actions)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey.svg)](https://www.guildwars2.com/)

Combines the best features from GW2Radial, TacO, ArcDPS, and Blish-HUD into a single, unified overlay application.



## ✨ Features

### 🎮 Radial Menus
- Quick access to mounts, novelties, and squad markers
- Customizable hotkeys and items
- Condition-based visibility (in combat, on mount, etc.)

### 📊 DPS Tracker
- Real-time combat statistics
- ArcDPS EVTC log parsing
- Optional real-time integration
- Damage graphs and history

### 📍 Marker System
- **100% TacO-compatible** - Use existing marker packs!
- 3D OpenGL rendering with proper depth
- Trail support
- Marker pack browser with downloads

### 🔌 Blish-HUD Modules
- Run existing Blish-HUD modules
- Graphics bridge for rendering
- Full API compatibility layer

### 👥 Multi-Account Launcher
- Launch multiple GW2 instances
- Profile management
- Credential storage (obfuscated)

### 🔄 Additional Features
- Auto-update system
- GW2 API integration (wallet, achievements)
- Discord Rich Presence
- Light/Dark themes
- 4 languages (EN/DE/FR/ES)

## 📥 Installation

### Portable Version (Recommended)
1. Download `GW2AIO_Portable.zip` from [Releases](https://github.com/Foboryus/GW2-AIO/releases)
2. Extract anywhere
3. Run `GW2AIO.exe`

### Installer Version
1. Download `GW2AIO_Setup.exe` from [Releases](https://github.com/Foboryus/GW2-AIO/releases)
2. Run installer
3. Launch from Start Menu

## 🔧 Requirements

- **Windows 10/11** (64-bit)
- **Guild Wars 2** installed
- **.NET 6+ Runtime** (only for Blish modules)

## 🚀 Quick Start

1. Launch GW2 AIO Manager
2. Complete the setup wizard
3. Press **V** for Mount wheel
4. Press **N** for Novelties
5. Press **M** for Markers

## 📁 Marker Packs

Place `.taco` files in:
- **Portable**: `./data/MarkerPacks/`
- **Installed**: `%APPDATA%/GW2AIO/MarkerPacks/`

Popular packs:
- [Tekkit's All-In-One](https://tekkitsworkshop.net/)
- [Reactif's Fractals](https://github.com/ReActif/TacoMarkers)

## 🛠️ Building from Source

```bash
# Prerequisites: Visual Studio 2022, vcpkg

# Install Qt
vcpkg install qtbase:x64-windows qtdeclarative:x64-windows

# Build
cmake -B build -G "Visual Studio 17 2022" ^
      -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed instructions.

## 📜 License

MIT License - see [LICENSE](LICENSE) for details.

This project is not affiliated with ArenaNet.

## 🙏 Credits

- **GW2Radial** by Friendly0Fire
- **GW2TacO** by BoyC
- **Blish-HUD** Team
- **ArcDPS** by deltaconnected
- Qt Framework (LGPL)
- QuaZip (LGPL v2.1+)

## 🤝 Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md).

## 📞 Support

- [GitHub Issues](https://github.com/Foboryus/GW2-AIO/issues)
- [GitHub Discussions](https://github.com/Foboryus/GW2-AIO/discussions)

---

Made with ❤️ for the GW2 community
