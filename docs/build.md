# Building HikariSub legacy from source

HikariSub currently has two supported source-build paths:

- **Windows**: the upstream Visual Studio solution (`HikariSub.sln`). This is the full-featured build that uses DirectShow, DirectSound, Direct3D 9/D3DX9, and the Windows COM/Shell APIs.
- **Linux**: This build uses wxGTK and system packages where possible. Some Windows-only runtime backends are still compatibility layers or partial ports, but the project can be configured, compiled, linked, and smoke-tested on Linux.

Both start from a recursive clone -- most third-party code is a git submodule,
and wxWidgets keeps its own dependencies in nested submodules:

```bash
git clone --recurse-submodules https://github.com/altqx/hikari.git
cd HikariSub
```

---

## Windows build

```powershell
pwsh -File Thirdparty\bootstrap.ps1
msbuild HikariSub.sln /m /p:Configuration=Release /p:Platform=x64
```

`HikariSub.exe` is written to `x64\Release`.

If the clone was not recursive, run `git submodule update --init --recursive`
first -- `bootstrap.ps1` does this too, but a non-recursive checkout otherwise
fails inside the wxWidgets build on empty directories.

### Required tools

| Tool | Why |
|---|---|
| **Visual Studio 2022 or newer** | Desktop development with C++, Windows 10/11 SDK, and MFC for C++ (the VSFilter renderer needs it). The projects build with whichever MSVC toolset the installed Visual Studio defaults to. |
| **NASM** | Assembly in libass and LuaJIT. Must be on `PATH` (`nasm -v`) |
| **YASM** | Assembly in the VirtualDub libraries the VSFilter renderer builds on. Must be on `PATH` (`yasm --version`) |
| **Git** | Submodules, and the commit recorded in the title bar |
| **DirectX SDK (June 2010)** | D3DX9. Expected at `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)` |

Optionally **gettext** for `msgfmt` and **Python 3**, which together compile
`Locale\*.po` into the `Locale\<lang>\LC_MESSAGES\hikarisub.mo` files the program
loads. Without either the step prints a notice and is skipped, and the build
has no translations.

### What bootstrap.ps1 does

1. `git submodule update --init --recursive`
2. `hydrate.ps1` — downloads the dependencies that are archives rather than
   submodules (FFmpeg, Boost, ICU, fribidi, zlib, curl), verifying each
   archive's SHA-256 against [`Thirdparty/dependencies.json`](../Thirdparty/dependencies.json)
   and refusing to continue on a mismatch
3. `build-wxwidgets.ps1` — builds wxWidgets 3.3.4 using `wx_vc17.sln`, the
   solution wxWidgets itself ships
4. `gen-gitparams.ps1` — writes `HikariSub/gitparams.h`

Every step is re-runnable: hydrate skips what is already extracted, and the
wxWidgets build is incremental. Pass `-Force` to re-extract, or
`-SkipSubmodules` / `-SkipWxWidgets` to skip a step.

For a Debug build, wxWidgets needs its Debug libraries too:

```powershell
pwsh -File Thirdparty\bootstrap.ps1 -Configuration Debug
msbuild HikariSub.sln /m /p:Configuration=Debug /p:Platform=x64
```

### Where the dependencies come from

Most are git submodules pinned to a release tag; the rest are hash-pinned
archives. [`Thirdparty/README.md`](../Thirdparty/README.md) has the full table and
explains why each one is where it is.

FFmpeg is a prebuilt developer package — headers, MSVC import libraries and
runtime DLLs. Building it from source previously required MSYS2 and a
from-scratch FFmpeg compile; that is no longer part of the build. The DLLs are
copied next to `HikariSub.exe` by a post-build step, so they must ship with the
application.

> **x64 only.** The solution has no 32-bit (`Win32`) configurations: they
> were never shipped or built in CI, and their libass had no assembly.

### If the build cannot find something

Check that `bootstrap.ps1` completed — most failures are a step that was
skipped or a submodule that was not checked out. Beyond that, the include and
library directories live in `HikariSub\HikariSub.vcxproj` under
**C/C++ > General > Additional Include Directories** and
**Linker > General > Additional Library Directories**.

---

## Linux build

The Linux build uses CMake and system packages for most dependencies. wxGTK is built from the same pinned wxWidgets 3.3.4 submodule as the Windows build, and libass from the same `Thirdparty/libass` submodule, the altqx fork, built as a shared library the package bundles. It requires GCC, FFMS2, FFmpeg, Hunspell, uchardet, libcurl, ICU, Boost, GStreamer 1.x, and OpenGL development packages, plus what libass needs: FreeType, FriBidi, HarfBuzz, Fontconfig, NASM and autotools.

### 1. Install dependencies on Ubuntu/Debian

```bash
sudo apt update
sudo apt install --no-install-recommends -y \
  build-essential \
  cmake \
  ninja-build \
  gzip \
  tar \
  git \
  pkg-config \
  libgtk-3-dev \
  libfreetype-dev \
  libfribidi-dev \
  libharfbuzz-dev \
  libfontconfig-dev \
  nasm \
  autoconf \
  automake \
  libtool \
  libffms2-dev \
  libhunspell-dev \
  hunspell-en-us \
  libuchardet-dev \
  libcurl4-openssl-dev \
  libicu-dev \
  libboost-filesystem-dev \
  libboost-locale-dev \
  libboost-regex-dev \
  libboost-system-dev \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libgl1-mesa-dev \
  libgtk-3-dev \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-pulseaudio \
  gettext
```

GStreamer backs both video and audio playback on Linux, so its runtime plugins
must be present, not just the `-dev` headers. The `-base` plugins provide
`appsrc`, `audioconvert`, `audioresample` and `playbin`; the `-good` plugins
provide `autoaudiosink`; and an audio sink such as `gstreamer1.0-pulseaudio`
(or `gstreamer1.0-pipewire` / `gstreamer1.0-alsa`) is needed to actually output
sound. Without these plugins the audio/video pipeline cannot be created.

Optional but useful for headless smoke tests:

```bash
sudo apt install --no-install-recommends -y xvfb
```

### 2. Package names on other Linux distributions

The exact package names vary by distribution. Install the equivalent development packages for:

- C and C++ compiler toolchain (`gcc`, `g++`, `make`)
- CMake 3.21 or newer
- GNU tar and gzip (used by the reproducible Linux archive target)
- pkg-config
- GTK 3 and OpenGL development packages for the pinned wxWidgets 3.3.4 build
- FreeType, FriBidi, HarfBuzz and Fontconfig, plus NASM, autoconf, automake and
  libtool, for the libass fork (not the distribution's libass)
- FFMS2
- Hunspell
- A Hunspell dictionary (the runtime copy step looks for `en_US.aff` and `en_US.dic`)
- GNU gettext (`msgfmt`) for compiling translations
- Python 3, which drives the translation compile step (`tools/compile_catalogs.py`)
- uchardet
- libcurl
- ICU (`icu-uc` and `icu-i18n` pkg-config modules)
- Boost filesystem, locale, regex, and system
- FFmpeg development libraries: libavformat, libavcodec, libavutil
- OpenGL/Mesa development headers
- GTK 3 development headers
- GStreamer 1.x: the core (`gstreamer-1.0`) plus the `-base` libraries
  (`gstreamer-app-1.0`, `gstreamer-audio-1.0`, `gstreamer-video-1.0`), and at
  runtime the base and good plugin sets plus an audio sink (pulse/pipewire/alsa)

For Fedora-like systems, the package set is approximately:

```bash
sudo dnf install \
  gcc gcc-c++ make cmake ninja-build gzip tar git pkgconf-pkg-config \
  freetype-devel fribidi-devel harfbuzz-devel fontconfig-devel nasm autoconf automake libtool \
  ffms2-devel hunspell-devel hunspell-en-US uchardet-devel \
  libcurl-devel libicu-devel boost-devel ffmpeg-devel mesa-libGL-devel gtk3-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-base gstreamer1-plugins-good gettext
```

For Arch-like systems, the package set is approximately:

```bash
sudo pacman -S --needed \
  base-devel cmake ninja gzip tar git pkgconf freetype2 fribidi harfbuzz fontconfig nasm ffms2 \
  hunspell hunspell-en_us uchardet curl icu boost ffmpeg mesa gtk3 \
  gstreamer gst-plugins-base gst-plugins-good gettext
```

LuaJIT is not taken from the distribution. The build compiles the copy in `Thirdparty/luajit` and `Thirdparty/LuaJIT` with GNU make and links it statically, with the same options as the Windows build: the Lua 5.2 extensions on (`LUAJIT_ENABLE_LUA52COMPAT`: `table.pack`, `__pairs`, `__ipairs` and others, which DependencyControl's Functional, ASSFoundation and many other automation libraries use) and `string.buffer` off (`LUAJIT_DISABLE_BUFFER`). Distribution LuaJIT packages are built without the Lua 5.2 extensions.

### 3. Verify dependency discovery

Before configuring HikariSub, confirm that pkg-config can find the required libraries:

```bash
pkg-config --modversion \
  ffms2 \
  hunspell \
  uchardet \
  libcurl \
  icu-uc \
  icu-i18n \
  libavformat \
  libavcodec \
  libavutil \
  gstreamer-1.0 \
  gstreamer-video-1.0 \
  gstreamer-audio-1.0 \
  gstreamer-app-1.0
```

Build wxWidgets from the pinned submodule, then verify it. The script first
applies HikariSub's wxGTK fixes from `Thirdparty/wxWidgets-patches/` to the
submodule (see `Thirdparty/PATCHES.md`):

```bash
Thirdparty/build-wxwidgets-linux.sh
build-wx-linux/prefix/bin/wx-config --version
build-wx-linux/prefix/bin/wx-config --libs core,base,adv,aui,html,xml,gl,stc,net
```

The version must be 3.3.4. HikariSub's CMake configure uses this private `wx-config` by default and rejects a different wxWidgets version. If a command fails, install the missing `-dev`/`-devel` package or adjust `PKG_CONFIG_PATH` so that pkg-config can locate the corresponding `.pc` file.

For a Debug build, run `Thirdparty/build-wxwidgets-linux.sh --debug` and
configure HikariSub with `-DCMAKE_BUILD_TYPE=Debug`. The Debug build uses its
own wxWidgets libraries under `build-wx-linux-debug/prefix`.

Build libass from the `Thirdparty/libass` submodule, the altqx fork the Windows
build uses too:

```bash
Thirdparty/build-libass-linux.sh
```

It installs the shared library into `build-libass-linux/prefix`, where the
configure step requires it (`-DHIKARISUB_LIBASS_PREFIX=` names another prefix).
The configure step rejects a libass without the fork's API, such as the
distribution's, and the package bundles the fork's `libass.so.9`.

### 4. Configure and build

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j$(nproc)
```

The executable is created at:

```text
build-linux/hikarisub
```

To create a clean Linux runtime archive after building:

```bash
cmake --build build-linux --target hikarisub_linux_package
```

The archive and its SHA-256 file are written under `build-linux/dist/`. The
package target uses an explicit allowlist: it includes the executable,
manifest-recorded runtime libraries, freshly compiled translations, bitmap
resources, the bundled Automation tree with DependencyControl's native modules
(`libBadMutex.so`, `libPreciseTimer.so` and `libDownloadManager.so` under
`Automation/automation/Include/BM`, `PT` and `DM`), the project license, and the
configured English dictionary when one is available. It deliberately excludes CMake internals and any Config,
Automation, or Themes files created by local runs. Host graphics/windowing
libraries, Fontconfig, GLib, and GStreamer with its plugins remain system
requirements; this archive is not a universal AppImage.

The package also carries `libSubInspector.so`, which ASSFoundation scripts use
to measure rendered lines. To check that the automation's `Inspector.moon`
loads it and gets bounds from it (needs `luajit` and a font), extract the
archive and run:

```bash
tar -xzf build-linux/dist/hikarisub-linux-x86_64.tar.gz -C /tmp
tests/subinspector/check-package.sh /tmp/hikarisub-linux-x86_64
```

### 5. Run HikariSub

On a normal desktop session:

```bash
./build-linux/hikarisub
```

For a headless smoke test, use Xvfb and a timeout:

```bash
timeout 8s xvfb-run -a ./build-linux/hikarisub
```

Exit code `124` from the command above is expected when `timeout` stops an otherwise running GUI application after 8 seconds.

### 6. Clean or rebuild

To rebuild incrementally:

```bash
cmake --build build-linux -j$(nproc)
```

To force a clean reconfigure:

```bash
rm -rf build-linux
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j$(nproc)
```

### 7. Current Linux runtime notes

On Linux the build behaves the same as the Windows build except for the following.

Media backends (Windows uses DirectShow / DirectSound / Direct3D):

- General video playback uses **GStreamer** in place of DirectShow: `playbin`
  decodes the file and an `appsink` hands BGRA frames to the app, which
  composites the libass subtitles and progress bar and presents them through the
  shared wxWidgets paint path. The frame-accurate FFMS2 path used for
  typesetting, timing, and visual editing is unchanged.
- Audio plays through **GStreamer** (`appsrc → audioconvert → audioresample →
  autoaudiosink`); the playback position is read from the pipeline, so it
  follows the audio that is heard. There is no DirectSound path.
- GStreamer discovers plugins from the runtime system registry and standard
  plugin paths. The base and good plugin sets (and an audio sink) must be
  installed on the machine that runs HikariSub.

Other Linux differences:

- Move-to-trash uses freedesktop `gio trash`, so the `gio` tool (glib2) must be present at runtime; if it is missing, deleting a loaded video is a no-op instead of an unrecoverable hard delete.
- The file-association `("Skojarzenia")` options tab is not shown. On Linux associations
  are declared by the `.desktop` file rather than set per extension at runtime. Run
  `./install-desktop-integration.sh` from the extracted archive to register the menu
  entry, icons and file types under `~/.local/share`; `--uninstall` removes them. A
  prefix install (`cmake --install`) installs the same data, but not the executable:
  HikariSub still locates its resources relative to the binary, so it has to run from its
  own directory.

Under Wayland specifically (these work normally on X11):

- Cursor-anchored dialogs (colour picker, font/style/hotkey dialogs, etc.) open centred instead of at the pointer a Wayland client cannot position its own windows.
- "Fullscreen on a specific monitor" falls back to fullscreen on the current output.
- Auto-pause-on-minimize and the B-key minimize do nothing a Wayland client cannot minimize itself.
- The screen-pixel colour eyedropper is unavailable Wayland forbids reading pixels outside the app's own surface.


## Publishing to the AUR (maintainers)

The release job publishes each release to the AUR: `hikarisub-beta-bin` always,
`hikarisub-stable-bin` too for a stable version. It needs an AUR account whose
SSH key is the repository secret `AUR_SSH_PRIVATE_KEY`. Without one, or when the
AUR refuses the key, the release still comes out and the job only warns. To
publish later:

1. Register at <https://aur.archlinux.org/register> and add the public key
   (`~/.ssh/aur_ed25519.pub`) under My Account, SSH Public Key.
2. Run the AUR publish workflow for the newest release, for example
   `gh workflow run aur-publish.yml -f tag=v0.0.2-rc.1` (add `-f dry_run=true`
   to see the commits without pushing them).

It takes the release's tar.gz from GitHub, checks it against the release's
`SHA256SUMS`, builds and installs each package in an Arch container, and pushes
it. It refuses an older tag when a newer release of the same channel exists, and
a version lower than the one on the AUR. Each package moves to 0.1.0 and up on
its own (ADR-0021): `hikarisub-beta-bin` once 0.1.0-beta.1 is published, and
`hikarisub-stable-bin` once the first stable 0.1.0 or higher is. A legacy
release is then skipped for that package.
