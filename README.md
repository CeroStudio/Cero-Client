<h1 align="center"><img src="./assets/logo.png" width="30" alt="CeroClient icon"> <strong>Cero</strong>Client</h1>

<p align="center">
  CeroClient is a free and open Minecraft client designed to be highly optimized and lightweight.
</p>

<p align="center">
  <img src="https://img.shields.io/github/stars/CeroWorks/Cero-Client?style=flat" alt="Stars">
  <img src="https://img.shields.io/github/license/CeroWorks/Cero-Client?style=flat" alt="License">
  <img src="https://img.shields.io/badge/Minecraft-1.7.10--26.3-green" alt="Versions">
  <img src="https://img.shields.io/github/v/release/CeroWorks/Cero-Client?style=flat" alt="Release">
</p>

<p align="center">
  <img src="./screenshots/launcher1.png" width="800" alt="CeroClient launcher">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C-A8B9CC?style=flat&logo=c&logoColor=black" alt="C">
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++">
  <img src="https://img.shields.io/badge/JavaScript-F7DF1E?style=flat&logo=javascript&logoColor=black" alt="JavaScript">
  <img src="https://img.shields.io/badge/HTML5-E34F26?style=flat&logo=html5&logoColor=white" alt="HTML">
  <img src="https://img.shields.io/badge/CSS3-1572B6?style=flat&logo=css3&logoColor=white" alt="CSS">
  <img src="https://img.shields.io/badge/Go-00ADD8?style=flat&logo=go&logoColor=white" alt="Go">
  <img src="https://img.shields.io/badge/Python-3776AB?style=flat&logo=python&logoColor=white" alt="Python">
  <img src="https://img.shields.io/badge/Java-ED8B00?style=flat&logo=openjdk&logoColor=white" alt="Java">
  <img src="https://img.shields.io/badge/Rust-000000?style=flat&logo=rust&logoColor=white" alt="Rust">
  <img src="https://img.shields.io/badge/Nim-FFE953?style=flat&logo=nim&logoColor=black" alt="Nim">
  <img src="https://img.shields.io/badge/Objective--C++-438EFF?style=flat" alt="Objective-C++">
  <img src="https://img.shields.io/badge/Lua-2C2D72?style=flat&logo=lua&logoColor=white" alt="Lua">
</p>

---
### Translations

- [Français](./translation/md/README_fr.md)

## Features & Tasks

- [x] **Instances**
	- [x] Install a loader (e.g., Forge, Fabric, etc.)
	- [x] Save and manage instances
    - [ ] Download mods from the UI
- [x] **Play Minecraft**
	- [x] Download Manifest
	- [x] Read Metadata
	- [x] Download Libs
	- [x] Download Client
	- [x] Download Assets
	- [x] Start Client
	- [x] Install Fabric
	- [x] Install Forge
    - [x] Install Quilt
	- [x] Start Fabric
	- [x] Start Forge
    - [x] Start Quilt
- [x] **Connect Microsoft Account**
- [x] Create an Installer
- [x] Create an Updater
- [x] Rewrite the Launcher in C/C++
- [ ] Friend & Chat system
    - [x] Send Message
    - [ ] Invite in his world
    - [x] Add Friend
    - [x] Remove Friend
- [ ] Add Android Support `Very hard! (for me)`
- [x] Multi Language Support

---

## Installation

All downloads and instructions for CeroClient are available on our [Website](https://cerostudio.fr/ceroclient) (not updated yet) or from releases.

*Note: macOS is largely untested — we currently have no macOS testers.*

## Architecture

| Component | Language | Role |
|---|---|---|
| `src/`, `include/` | C / C++ | Native launcher, webview UI, IPC, AES-GCM crypto |
| `agent/` | Java | Mixin service, MCP/ProGuard→Tiny remapper, custom classloader |
| `bootstrapper/` | Rust | Installation and auto-update |
| `server/` | Go | Authentication, friends, WebSocket messaging |
| `server/commands/lua/` | Lua | Extensible server commands (kick, status, friends...) |
| `assets/` | HTML/CSS/JS | User interface |
| `tools/`, `build.py` | Python | Build pipeline, asset packaging, JS obfuscation |
| `installer/` | Nim / NSIS | Platform installers (Unix-like/WinNT) |

## Building from Source

If you want to compile CeroClient yourself, you can use the provided build scripts in the repository.

*For further assistance with installing prerequisites and compiling, please refer to `BUILDING.md`.*

**Requirements (Linux):**
* **GCC / G++** ≥ `13.3.0`
* **Python** ≥ `3.9` (Tested : 3.13)
* **Rust / Cargo** (Latest stable)
* **pkg-config**
* **Dependencies:** `gtk+-3.0`, `webkit2gtk-4.1`, `libcurl` (and `ayatana-appindicator3-0.1` or `appindicator3-0.1` for system tray support)

**Requirements (Windows):**
* **MinGW-w64** (GCC / G++)
* **Rust / Cargo** (Latest stable)
* Pre-compiled dependencies in `%USERPROFILE%\mingw-deps\x64-windows`

### Instructions

1. Clone the repository.
2. Install all python dependencies: `pip install -r requirements.txt`
3. Run the build script: `python3 build.py`

*Note: On Windows, WebView2 requires additional setup — run `setup_webview.bat` before building.*

### Development

You can run it easily for debugging or to test a modification.

1. Install all python dependencies: `pip install -r requirements.txt`
2. Run the run script: `python3 run.py`

*Note: On Windows, WebView2 requires additional setup — run `setup_webview.bat` before building.*

---
## License

This project is licensed under the GNU General Public License v3.0 only.
See the [LICENSE](./LICENSE) file for details.

CeroClient does not include or distribute Minecraft itself or proprietary assets owned by Mojang or Microsoft. Users are responsible for obtaining and using Minecraft in accordance with Mojang's terms.

Copyright © 2025–2026 Cero Studio.


