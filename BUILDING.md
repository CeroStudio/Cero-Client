# Building CeroClient from source

This document was verified directly against the repository's source code
(`build.py`, `tools/build/*.py`, `installer/*`) and the official release
workflow (`.github/workflows/release.yml`) — not just against the README.

CeroClient is made up of several distinct pieces:

| Component | Language | Role |
|---|---|---|
| `src/`, `include/` | C / C++ (+ Obj-C++ on macOS) | Native launcher, webview, IPC, AES-GCM encryption |
| `agent/` | Java (Gradle) | Mixins, MCP/ProGuard-to-Tiny remapping, custom classloader |
| `bootstrapper/` | Rust (edition 2021) | Installation and auto-update |
| `server/` | Go | Auth, friends, WebSocket messaging — built/deployed via Docker, independent of everything else |
| `assets/` | HTML / CSS / JS | User interface, packaged into a `.dat` bundle |
| `tools/`, `build.py`, `run.py` | Python (≥ 3.9) | Build orchestration, asset packaging |
| `installer/unix/` | Nim | Linux/BSD/macOS installer (uses **GTK4**, distinct from the client's GTK3) |
| `installer/winnt/` | NSIS | Windows installer |

The full pipeline (`python3 build.py`) runs, in order: dependency check →
directory setup → Java agent build → Rust bootstrapper build → asset
packaging → native launcher build → finalization. `run.py` does the same
(minus the bootstrapper) and then launches the built client directly.

---

## Common Prerequisites

- **Git**
- **Python ≥ 3.9** — `requirements.txt` currently lists no external
  dependencies, but running it is still good practice:
  ```sh
  pip install -r requirements.txt
  ```
- **Rust stable** via [rustup](https://rustup.rs/)
- **pkg-config**
- A C/C++ compiler (`cc`/`c++` on Unix, `gcc`/`g++` under MinGW)

`build.py` checks for these tools at startup (`check_deps.py`) and
**automatically clones** the [`webview/webview`](https://github.com/webview/webview)
library into `third_party/webview/` if it isn't already present.

**Java (JDK)**: `build_agent.py` looks for `JAVA_HOME`, then falls back to
scanning standard install locations (JDK 8, 17, 21, or other — in that order
of preference). The official CI uses **JDK 17 (Temurin)** on every platform;
that's the version to prefer even though the compiled bytecode targets
Java 8.

**Node.js / npm (optional but recommended)**: asset packaging
(`package.json`: `terser`, `tailwindcss`, `clean-css-cli`,
`html-minifier-terser`) minifies and obfuscates the JS/CSS, and
`tools/build/build_tailwind.py` compiles `assets/tailwind.config.js` into
`assets/style/tailwind.css` (replacing the old `cdn.tailwindcss.com`
`<script>` tag, which was blocking page load on every launch). If Node.js
isn't installed, **neither step fails the build** — both print a warning
and fall back to what's already committed in the repo (unminified JS, and
a baseline `tailwind.css` that may be missing recently-added utility
classes). Install Node.js if you're touching `assets/` at all:

- **Linux**: `sudo apt-get install -y nodejs npm` (or use
  [nvm](https://github.com/nvm-sh/nvm) for a specific version)
- **macOS**: `brew install node`
- **Windows (MSYS2 MINGW64 shell)**: install Node.js **natively** from
  [nodejs.org](https://nodejs.org/) (not via `pacman`, same reasoning as
  Python below — `pacman -S nodejs` puts it under the MSYS layer, which
  can behave inconsistently with `npx`/`npm` invoked via `subprocess.run`
  from Python). After installing, restart the MINGW64 shell so `PATH`
  picks it up, and confirm with `node -v` / `npm -v`.

```sh
git clone https://github.com/CeroWorks/Cero-Client.git
cd Cero-Client
npm install   # optional, for JS minification/obfuscation + Tailwind CSS build
```

---

## Linux

```sh
sudo apt-get update
sudo apt-get install -y \
    build-essential pkg-config git \
    libwebkit2gtk-4.1-dev libcurl4-openssl-dev
```

> `libwebkit2gtk-4.1-dev` pulls in `libgtk-3-dev` as a dependency: it's this
> GTK **3** (`gtk+-3.0`) that's used to compile the native launcher. If
> `webkit2gtk-4.1` isn't found by `pkg-config`, the build automatically
> falls back to `webkit2gtk-4.0`.

System tray support is optional — the launcher builds fine without it, just
without a tray icon:

```sh
sudo apt-get install -y libayatana-appindicator3-dev
# or, on older distributions:
sudo apt-get install -y libappindicator3-dev
```

Install Rust (see above), then build:

```sh
pip install -r requirements.txt
npm install        # optional
python3 build.py
```

Produced artifacts:

```
bin/linux/client/CeroClient
bin/linux/bootstrapper/ceroclient-bootstrapper
bin/assets/assets.dat
```

### Linux ARM64

Identical, except CI installs `nim` via `apt` rather than the x86_64
tarball (only relevant if you're also building the installer — see below).

---

## Windows

The launcher is built under **MSYS2 / MINGW64** — make sure you open the
**"MSYS2 MINGW64"** shell from the Start menu, not "MSYS2 UCRT64" or
"MSYS2 MSYS" (the official CI explicitly uses `msystem: MINGW64`).

> **Gotcha confirmed in real testing:** `check_deps.py` detects Windows via
> `sys.platform == "win32"`. If you install Python via
> `pacman -S python3 python3-pip` (a plain **MSYS**-layer package, no
> `mingw-w64-` prefix), that Python runs with `sys.platform == "msys"`
> rather than `"win32"` — the script then falls into the Unix branch and
> looks for `cc`/`c++` instead of `gcc`/`g++`, failing with
> `✗ cc is required.` even though MinGW-w64 is properly installed.
> **Install Python natively from [python.org](https://www.python.org/downloads/)**
> ("Add python.exe to PATH"), not via `pacman`, so that `sys.platform`
> correctly reports `"win32"`.

> **Important, confirmed in real testing:** on Windows the build invokes
> the specific binary **`mingw32-make`** (see `build_launcher.py`), not
> `make`. The MSYS `make` package (`/usr/bin/make`) does **not** provide
> this binary — install `mingw-w64-x86_64-make`, which provides
> `mingw32-make.exe`, or the compile step fails with `[WinError 2] The
> system cannot find the file specified`.

```sh
pacman -S --needed \
    git zip \
    mingw-w64-x86_64-make \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-curl \
    mingw-w64-x86_64-libssh2 \
    mingw-w64-x86_64-nghttp2 \
    mingw-w64-x86_64-libpsl \
    mingw-w64-x86_64-brotli \
    mingw-w64-x86_64-zstd \
    mingw-w64-x86_64-openssl \
    mingw-w64-x86_64-gtk4
```

Install **Python** (natively, "Add python.exe to PATH"), **JDK 17
(Temurin)** and **Rust** (rustup) outside of MSYS2, making sure they stay
visible from the MINGW64 shell (start MSYS2 with `MSYS2_PATH_TYPE=inherit`
if needed).

Download the WebView2 SDK:

```bat
setup_webview.bat
```

> This script does not check whether the download succeeded. If
> `nuget.org` was unreachable, `webview2_sdk/` can stay empty despite the
> "WebView2 SDK ready." message. Verify
> `webview2_sdk/build/native/include`.

Build, from the MINGW64 shell:

```sh
export PYTHONUTF8=1
export WEBVIEW2_INCLUDE="$(pwd)/webview2_sdk/build/native/include"
export WEBVIEW2_LIB="$(pwd)/webview2_sdk/build/native/x64"
pip install -r requirements.txt
python build.py
```

### NSIS installer (Windows)

Windows doesn't use `installer/unix/build_installer.py` — it uses NSIS:

1. Install NSIS (`choco install nsis -y` or manually).
2. Install the **ZipDLL** plugin at
   `C:\Program Files (x86)\NSIS\Plugins\x86-unicode\ZipDll.dll`
   (required by `installer/winnt/installer.nsi`, which uses it to extract
   the bootstrapper at install time).
3. Make sure the icon is present at
   `installer\winnt\assets\favicon.ico` (otherwise copy it from
   `assets\favicon.ico`).
4. Run:
   ```powershell
   & "C:\Program Files (x86)\NSIS\makensis.exe" installer/winnt/installer.nsi
   ```

---

## macOS

The macOS client **is genuinely built and released in CI** (Apple Silicon
only, no Intel/x86_64 release), via `src/ui/ui_macos.mm`, which links
`WebKit`/`Cocoa`/`AppKit`/`Foundation` natively — **no GTK on the macOS
client side**.

> That said, the project notes macOS is "largely untested, due to a lack of
> testers": the build works in CI, but quality support isn't guaranteed.

### macOS implementation notes

- **UI assets**: WKWebView only accepts a custom scheme handler on the
  configuration used to create the view, and `webview` builds that internally.
  `ui_macos.mm` therefore hooks `-[WKWebView initWithFrame:configuration:]`
  (`ui_macos_prepare()`, called before `webview_create()`) to register
  `cero://` and serve files from `assets.dat`.
- **Menu bar**: `webview` creates no menu, so `ui_macos.mm` installs one
  (Cmd+Q, Cmd+W hide, Cmd+M, Cut/Copy/Paste/Select All, ...).
- **Tray**: an `NSStatusItem` in the menu bar (Show / Quit). The red window
  button hides the window (like WM_CLOSE on Windows); clicking the Dock icon
  shows it again.
- **Java**: Adoptium's macOS archives extract to `Contents/Home/bin/java`;
  on Apple Silicon the arm64 JRE is downloaded (x64/Rosetta only as a
  fallback when no arm64 build exists for that Java major).
- **`-XstartOnFirstThread`** is added for Minecraft 1.13+ (LWJGL 3 / GLFW).
- Browser opening uses `open` instead of `xdg-open`.
- The `macOS check` workflow builds on `macos-14`, starts the launcher for
  20 seconds and uploads the log and a screenshot (`macos-smoke-test`
  artifact). Run it from the Actions tab when no Mac is at hand.

### Known limitations (macOS)

- Minecraft **1.18 and older on Apple Silicon**: Mojang ships no arm64 LWJGL
  natives for those versions, so they need an x64 JRE (Rosetta 2) plus x64
  natives. Not implemented yet (needs a per-architecture runtime directory).
- The client is distributed as a bare binary, not a `.app` bundle: no proper
  Dock name/icon at launch and Gatekeeper will quarantine downloads until the
  binary is signed and notarized (needs an Apple Developer ID).

```sh
brew install python@3.12 curl pkg-config git nim
brew install --cask temurin@17
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"

pip install -r requirements.txt
python3 build.py
```

---

## FreeBSD

Supported and built in CI on FreeBSD 14.2:

```sh
env IGNORE_OSVERSION=yes pkg install -y \
    python3 pkgconf webkit2-gtk_41 curl git openjdk17 \
    gmake bzip2 rust gtk4 nim zip

printf "Name: bzip2\nDescription: bzip2\nVersion: 1.0.8\nLibs: -lbz2\n" \
    > /usr/local/libdata/pkgconfig/bzip2.pc

ln -sf /usr/local/bin/pkg-config /usr/bin/pkg-config
ln -sf "$(command -v nim)" /usr/bin/nim

export PATH="/usr/local/bin:/usr/bin:$PATH"
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/libdata/pkgconfig

python3 build.py
python3 installer/unix/build_installer.py
```

---

## Unix-like installer (Linux / FreeBSD / macOS)

`installer/unix/build_installer.py` compiles an installer written in
**Nim**. Unlike the client (GTK3), it requires **GTK4**:

```sh
python3 installer/unix/build_installer.py
```

Prerequisites checked by the script itself:
- `nim` on `PATH`
- `pkg-config` available
- `gtk4` detected via `pkg-config --exists gtk4`

On Linux x86_64, CI installs Nim **2.0.16** from the official tarball; on
ARM64/FreeBSD/macOS it uses the system package manager (`apt`/`pkg`/`brew`).

Produced artifact: `installer/unix/cero-installer`.

---

## Development (quick build + run)

```sh
pip install -r requirements.txt
python3 run.py
```

`run.py` builds the launcher, the agent and the assets (skipping the
bootstrapper), then launches the produced binary directly.

---

## The server (Go)

`server/` is a separate service (auth, friends, WebSocket), built and
published as a Docker image via `server/Dockerfile` by a dedicated CI
workflow (`.github/workflows/server.yml`) — entirely independent from the
client/installer pipeline described above.

---

## Troubleshooting

- **`pkg-config` can't find `webkit2gtk-4.1`**: the build automatically
  falls back to `webkit2gtk-4.0` — no action needed, just a
  `Using webkit2gtk-4.0` message.
- **No tray icon**: `ayatana-appindicator3-0.1` /
  `appindicator3-0.1` are missing — the build still succeeds, the tray is
  simply disabled (`Tray: NOT detected - disabled`).
- **`terser not found in local node_modules`**: run `npm install` at the
  repo root; without it, the packaged JS is neither minified nor obfuscated
  (the build still works).
- **`✗ npx introuvable` during "Building Tailwind CSS..."** *(confirmed in
  real testing, Windows/MSYS2)* — Node.js isn't installed, or was installed
  via `pacman` instead of natively. This step now **warns and continues**
  rather than failing the whole build, falling back to the `tailwind.css`
  already committed in the repo — but that fallback can be stale if you've
  added new utility classes since it was last generated. Install Node.js
  natively (see Prerequisites above) and re-run `npm install`, then rebuild
  to regenerate it. On Windows, `where node` / `where npm` in the MINGW64
  shell should resolve to a native Windows install, not something under
  `/mingw64` or MSYS.
- **`JAVA_HOME not found`**: Gradle will fall back to the system `PATH`
  Java; set `JAVA_HOME` to a JDK 17 install if the agent build fails.
- **Windows: `[WinError 2] The system cannot find the file specified`
  during compilation** *(confirmed in real testing)* — the `mingw32-make`
  binary is missing. The MSYS `make` package doesn't provide it; install
  `mingw-w64-x86_64-make`.
- **Windows: `✗ cc is required.`** *(confirmed in real testing)* — you're
  likely using the MSYS-layer Python (`pacman -S python3`) instead of
  native Windows Python. `sys.platform` then reports `"msys"` instead of
  `"win32"`, so `check_deps.py` looks for `cc`/`c++` (missing) instead of
  `gcc`/`g++`. Install Python from python.org and rebuild with that Python.
  Also confirm you're in the **MSYS2 MINGW64** shell, not UCRT64/MSYS —
  `mingw-w64-x86_64-*` packages install into `/mingw64`, which isn't on a
  UCRT64 shell's `PATH`.
- **Windows: WebView2 not found**: verify that
  `webview2_sdk/build/native/include` actually exists — `setup_webview.bat`
  doesn't detect failed downloads.
- **Windows: `makensis` fails on a missing plugin**: the ZipDLL plugin must
  be copied manually, NSIS doesn't bundle it by default.

---

## License

CeroClient is distributed under the **GNU General Public License v3.0
only** (see [`LICENSE`](LICENSE)). CeroClient does not include or
distribute Minecraft itself or proprietary assets owned by Mojang or
Microsoft; users are required to obtain and use Minecraft in accordance
with Mojang's terms.