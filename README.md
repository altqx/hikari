# HikariSub

HikariSub is a fork of [Kainote](https://github.com/bjakja/Kainote) by Marcin Drob, continued as a separate project from Kainote commit `6e1bbb15`. It is distributed under the same GNU GPL v3 license, and the original copyright notices are kept in the source.

HikariSub is a subtitle editor and player for everything from quick fixes to full typesetting. It has a built-in video player, with **FFMS2** for frame-accurate typesetting and timing, **DirectShow** for ordinary playback on Windows, and **GStreamer** on Linux.

## Features

### Formats

* **Native formats**: opens and saves ASS, SRT, MicroDVD, MPL2 and TMPlayer. SSA is converted to ASS on load.
* **Conversion**: converts between any of the supported formats, with a chosen script resolution when converting to ASS.
* **MKV**: loads subtitle tracks straight from MKV files and extracts their attached fonts.
* **Right-to-left text**: handles RTL text in the grid, the editor and the spellchecker.

### Workspace

* **Tabs**: opens any number of subtitle files in tabs, each with its own video and audio.
* **Subtitle comparison**: compares two tabs side by side by times, styles, selected styles, selections or visible lines, and highlights matching and mismatched lines.
* **Sessions and autosave**: restores the last session or a saved session file. Autosaves keep a configurable number of copies and have their own browser.
* **Layouts**: switches between all panes, video and subtitles, audio and subtitles, video only or subtitles only.
* **History**: undo and redo, a browsable history window, and undo back to the last save.
* **Hotkeys**: every action can be remapped, with separate global, grid, editor, audio and video scopes, and automation macros can have hotkeys too.
* **Themes**: every colour in the interface can be themed. The bundled themes can be copied and edited in the settings.
* **Localisation**: the interface is available in English, Polish, Korean, Thai and Tamil.
* **Windows integration**: registers file associations for subtitle and video types, and checks for updates.

### Subtitle grid

* **Filtering**: hides comments, selected lines or chosen styles, or shows only unconfirmed or untranslated lines, or only lines visible on the current frame. Filters can be inverted and applied automatically on load.
* **Trees**: groups lines into named trees with descriptions that can be selected and copied as a unit.
* **Columns**: any column can be hidden, and a characters-per-second column is available.
* **Sorting**: sorts all or selected lines by start time, end time, style, actor, effect or layer.
* **Line operations**: insert before or after at the video time or frame, duplicate, join (keeping the first or last text), merge with the neighbouring line, swap, and copy or paste individual columns.
* **Splitting**: splits a line at the video time, or into frames, characters, words or line wraps.
* **Select lines**: selects by text or regular expression in any field, then copies, cuts, deletes or moves the matches.
* **Find and replace**: searches with regular expressions in chosen fields, in the current file, in all open tabs, or in every subtitle file in a folder, with a results window.
* **Clean-up**: fixes common minor errors and removes unused styles.

### Editing and translation

* **Editor**: ASS tags, karaoke templates, brackets and misspelled words are highlighted, and there are up to 20 configurable tag buttons.
* **Bulk tagging**: applies tags and changes to many selected lines at once.
* **Translation mode**: shows the original text next to the translation, can show the original on the video, and jumps to the next untranslated or unconfirmed line.
* **Translation alignment**: when pasting a translation over a timed original, lines can be added, deleted or joined on either side until the two match.
* **Confirmation workflow**: lines stay unconfirmed until you commit them, and there are shortcuts to jump between them.
* **Spellchecking**: Hunspell with inline suggestions and a spellcheck dialog, plus a replacer for common mistakes that lists each match for review.
* **Styles**: a style manager with reusable style catalogs, a live preview and font catalogs.
* **Script tools**: resamples subtitles with or without stretching, warns when the script and video resolutions differ, edits ASS file properties, and can hide tags for plain-text reading.

### Timing

* **Time shifting**: shifts by time or frames, forwards or backwards, on all lines, selected lines, lines from the selection onwards, or chosen styles. It can move start times, end times or both, supports profiles, and can stay in sync across tabs.
* **Post-processor**: adds lead-in and lead-out, corrects overlapping or improper end times, extends short gaps, and snaps to keyframes.
* **Keyframes**: loads keyframe files, jumps between keyframes, and snaps start and end times to the nearest one.
* **Quick timing**: makes a line's times continuous with the next or previous line, takes start and end times from the video, and changes the FPS.

### Audio

* **Displays**: waveform or spectrum, with optional speech-frequency enhancement, keyframe and second markers, and a time readout next to the cursor.
* **Playback controls**: plays before or after the markers, the first or last 500 ms, to the end, or the active line, and can follow the audio during playback.
* **Sources**: loads audio files or audio from a video, optionally into RAM, with an audio delay setting, a disk cache, and a blank 2h30m track for timing without audio.
* **Snapping**: snaps to keyframes and to other lines, with linked volume and horizontal stretch sliders.
* **Karaoke**: splits syllables automatically and edits them on the waveform.

### Video

* **Player**: frame-accurate FFMS2 playback with optional GPU colour conversion, DirectShow on Windows and GStreamer on Linux.
* **Subtitle renderers**: libass, or xy-VSFilter through CSRI.
* **Zoom**: zooms into the video, including in fullscreen, for precise clips and drawings.
* **Fullscreen player**: chapters, next and previous file, volume, aspect ratio and a progress bar.
* **Frames**: saves a frame as PNG or copies it to the clipboard, with or without subtitles.
* **Line sync**: seeks the video when you click or edit a line, and can play the line afterwards. When to seek and what to play are both configurable.
* **Dummy video**: a placeholder video for typesetting without the real source.

### Visual typesetting

* **Position**: drags `\pos` with an alignment pointer, or places the text inside a drawn rectangle.
* **Movement**: edits `\move` directly, or sets it from two points on different frames.
* **Rotation**: Z and X/Y rotation around `\org`, including an angle taken from two points, and can keep drawings in place.
* **Scaling**: scales by dragging or by drawing a rectangle, optionally keeping proportions.
* **Clips**: rectangular and vector `\clip`/`\iclip` built from lines, B-splines and separate points, with inversion.
* **Vector drawings**: edits `\p` drawings, with a library of reusable ASS drawings.
* **Move all**: moves positions, `\move` points, `\org`, clips and drawings together across lines.
* **All tags (Hydra)**: edits any listed tag visually, with gradients and multiply modes for text and lines.
* **Shifters**: the position shifter and the scale-and-rotation shifter apply one adjustment across many lines.

### Fonts

* **Font collector**: checks that every used font is installed, copies the fonts to a folder, packs them into a ZIP or muxes them into an MKV with mkvmerge.
* **Font sources**: extracts fonts from a loaded MKV and can use an external fonts folder.

### Automation

* **Automation 4**: runs Automation 4 Lua scripts on LuaJIT, ships the Aegisub automation library, and works with [DependencyControl](https://github.com/TypesettingTools/DependencyControl).
* **Script management**: autoloads scripts, maps macro hotkeys, reruns the last script, and has a compatibility mode for older scripts.

### Platforms

* **Windows x64**: the full-featured build, including DirectShow, DirectSound and Direct3D 9.
* **Linux**: built on wxGTK and GStreamer.

## Building from Source

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

### Windows build

```powershell
pwsh -File Thirdparty\bootstrap.ps1
msbuild HikariSub.sln /m /p:Configuration=Release /p:Platform=x64
```

`HikariSub.exe` is written to `x64\Release`.

If the clone was not recursive, run `git submodule update --init --recursive`
first -- `bootstrap.ps1` does this too, but a non-recursive checkout otherwise
fails inside the wxWidgets build on empty directories.

#### Required tools

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

#### What bootstrap.ps1 does

1. `git submodule update --init --recursive`
2. `hydrate.ps1` — downloads the dependencies that are archives rather than
   submodules (FFmpeg, Boost, ICU, fribidi, zlib, curl), verifying each
   archive's SHA-256 against [`Thirdparty/dependencies.json`](Thirdparty/dependencies.json)
   and refusing to continue on a mismatch
3. `build-wxwidgets.ps1` — builds wxWidgets 3.3.3 using `wx_vc17.sln`, the
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

#### Where the dependencies come from

Most are git submodules pinned to a release tag; the rest are hash-pinned
archives. [`Thirdparty/README.md`](Thirdparty/README.md) has the full table and
explains why each one is where it is.

FFmpeg is a prebuilt developer package — headers, MSVC import libraries and
runtime DLLs. Building it from source previously required MSYS2 and a
from-scratch FFmpeg compile; that is no longer part of the build. The DLLs are
copied next to `HikariSub.exe` by a post-build step, so they must ship with the
application.

> **32-bit.** `Release|Win32` exists but is not exercised. It needs a 32-bit
> FFmpeg developer package placed at `Thirdparty\ffmpegx32` (same `include\`
> and `lib\` layout); `hydrate.ps1` does not fetch one. x64 is the supported
> target.

#### If the build cannot find something

Check that `bootstrap.ps1` completed — most failures are a step that was
skipped or a submodule that was not checked out. Beyond that, the include and
library directories live in `HikariSub\HikariSub.vcxproj` under
**C/C++ > General > Additional Include Directories** and
**Linker > General > Additional Library Directories**.

---

### Linux build

The Linux build uses CMake and system packages for most dependencies. wxGTK is built from the same pinned wxWidgets 3.3.3 submodule as the Windows build. It requires GCC, LuaJIT 2.1 (Lua 5.1-compatible), FFMS2, FFmpeg, libass, Hunspell, uchardet, libcurl, ICU, Boost, GStreamer 1.x, and OpenGL development packages.

#### 1. Install dependencies on Ubuntu/Debian

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
  libass-dev \
  libffms2-dev \
  libluajit-5.1-dev \
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

#### 2. Package names on other Linux distributions

The exact package names vary by distribution. Install the equivalent development packages for:

- C and C++ compiler toolchain (`gcc`, `g++`, `make`)
- CMake 3.21 or newer
- GNU tar and gzip (used by the reproducible Linux archive target)
- pkg-config
- GTK 3 and OpenGL development packages for the pinned wxWidgets 3.3.3 build
- libass
- FFMS2
- LuaJIT 2.1 development headers and library (the `luajit` pkg-config module)
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
  libass-devel ffms2-devel luajit-devel hunspell-devel hunspell-en-US uchardet-devel \
  libcurl-devel libicu-devel boost-devel ffmpeg-devel mesa-libGL-devel gtk3-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-base gstreamer1-plugins-good gettext
```

For Arch-like systems, the package set is approximately:

```bash
sudo pacman -S --needed \
  base-devel cmake ninja gzip tar git pkgconf libass ffms2 luajit \
  hunspell hunspell-en_us uchardet curl icu boost ffmpeg mesa gtk3 \
  gstreamer gst-plugins-base gst-plugins-good gettext
```

HikariSub requires LuaJIT rather than the standard Lua interpreter: its Automation subsystem uses LuaJIT's FFI as well as Lua 5.1 APIs. The CMake configuration therefore checks for the `luajit` pkg-config module.

#### 3. Verify dependency discovery

Before configuring HikariSub, confirm that pkg-config can find the required libraries:

```bash
pkg-config --modversion \
  libass \
  ffms2 \
  luajit \
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

Build wxWidgets from the pinned submodule, then verify it:

```bash
Thirdparty/build-wxwidgets-linux.sh
build-wx-linux/prefix/bin/wx-config --version
build-wx-linux/prefix/bin/wx-config --libs core,base,adv,aui,html,xml,gl,stc,net
```

The version must be 3.3.3. HikariSub's CMake configure uses this private `wx-config` by default and rejects a different wxWidgets version. If a command fails, install the missing `-dev`/`-devel` package or adjust `PKG_CONFIG_PATH` so that pkg-config can locate the corresponding `.pc` file.

For a Debug build, run `Thirdparty/build-wxwidgets-linux.sh --debug` and
configure HikariSub with `-DCMAKE_BUILD_TYPE=Debug`. The Debug build uses its
own wxWidgets libraries under `build-wx-linux-debug/prefix`.

#### 4. Configure and build

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
resources, the project license, and the configured English dictionary when one
is available. It deliberately excludes CMake internals and any Config,
Automation, or Themes files created by local runs. Host graphics/windowing
libraries, codec libraries excluded for licensing reasons, and GStreamer
plugins remain system requirements; this archive is not a universal AppImage.

#### 5. Run HikariSub

On a normal desktop session:

```bash
./build-linux/hikarisub
```

For a headless smoke test, use Xvfb and a timeout:

```bash
timeout 8s xvfb-run -a ./build-linux/hikarisub
```

Exit code `124` from the command above is expected when `timeout` stops an otherwise running GUI application after 8 seconds.

#### 6. Clean or rebuild

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

#### 7. Current Linux runtime notes

On Linux the build behaves the same as the Windows build except for the following.

Media backends (Windows uses DirectShow / DirectSound / Direct3D):

- General video playback uses **GStreamer** in place of DirectShow: `playbin`
  decodes the file and an `appsink` hands BGRA frames to the app, which
  composites the libass subtitles and progress bar and presents them through the
  shared wxWidgets paint path. The frame-accurate FFMS2 path used for
  typesetting, timing, and visual editing is unchanged.
- Audio plays through **GStreamer** (`appsrc → audioconvert → audioresample →
  autoaudiosink`); the playback position is tracked on a wall-clock model. There
  is no DirectSound path.
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
