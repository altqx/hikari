# Thirdparty

How HikariSub gets its third-party code, and why each library is where it is.

The two builds source dependencies very differently:

- **Linux** (`CMakeLists.txt`) resolves everything through `pkg-config` against the
  distribution's packages. It reads nothing here except `luabins`,
  `DependencyControl`, the `ffms2` headers, `luajit`/`LuaJIT` and
  `SubInspector`. It builds LuaJIT and DependencyControl's native modules as
  the Windows build does (`cmake/VendoredLuaJIT.cmake`,
  `cmake/DependencyControlModules.cmake`), and SubInspector as the automation
  library's `libSubInspector.so`.
- **Windows** (`HikariSub.sln`) builds its dependencies from source, using the MSVC
  project files under `Thirdparty/Build/`.

## First checkout

```sh
git clone --recurse-submodules https://github.com/altqx/hikari.git
# or, in an existing clone:
git submodule update --init --recursive
```

Then, for the Windows build only:

```powershell
pwsh -File Thirdparty\bootstrap.ps1
```

`bootstrap.ps1` runs the three steps in order and is safe to re-run:

| Script | Does |
|---|---|
| `hydrate.ps1` | Downloads and verifies the dependencies that are archives |
| `build-wxwidgets.ps1` | Builds wxWidgets with the solution it ships |
| `gen-gitparams.ps1` | Writes `HikariSub/gitparams.h` |

Each can also be run on its own.

`build-wxwidgets.ps1` runs `Thirdparty\wxWidgets\build\msw\wx_vc17.sln`, the
solution wxWidgets itself ships, and leaves the static libraries in
`lib\vc_x64_lib`. HikariSub does not maintain
project files for wxWidgets and does not customise its `setup.h`; the stock
configuration is used.

`HikariSub.vcxproj` puts `Thirdparty\wxWidgets\include\msvc` on the include path
ahead of `include`, so `<wx/setup.h>` resolves to wxWidgets' MSVC helper header.
That header locates the built `setup.h` for the current platform and emits
`#pragma comment(lib, ...)` for every wxWidgets library, which is why no wx
libraries are listed in `AdditionalDependencies`.

## Submodules

| Path | Upstream | Pinned at |
|---|---|---|
| `freetype2` | [freetype/freetype](https://github.com/freetype/freetype) | `VER-2-14-3` |
| `harfbuzz` | [harfbuzz/harfbuzz](https://github.com/harfbuzz/harfbuzz) | `14.4.0` |
| `libass` | [libass/libass](https://github.com/libass/libass) | `0.17.5` |
| `Hunspell` | [hunspell/hunspell](https://github.com/hunspell/hunspell) | `v1.7.3` |
| `wxWidgets` | [wxWidgets/wxWidgets](https://github.com/wxWidgets/wxWidgets) | `v3.3.4` |
| `ffms2` | **[altqx/ffms2](https://github.com/altqx/ffms2)** | branch `hikarisub` |
| `xy-VSFilter-xy_sub_filter_rc5` | **[altqx/xy-VSFilter](https://github.com/altqx/xy-VSFilter)** | branch `hikarisub` |
| `SubInspector` | [TypesettingTools/SubInspector](https://github.com/TypesettingTools/SubInspector) | `51c4c7a` (0.5.2) |

The first five are pinned to an upstream release tag with no commits of our
own, so they point straight at upstream. `ffms2` and `xy-VSFilter` carry HikariSub's own changes, so they point at forks whose
`hikarisub` branch is upstream plus those changes — see [`PATCHES.md`](PATCHES.md).
`wxWidgets` is not built untouched on Linux, though: `build-wxwidgets-linux.sh`
applies the wxGTK fixes in `wxWidgets-patches/` to the checkout's working tree
(the pinned commit stays upstream's tag) before it builds. `PATCHES.md` lists
them too.

`SubInspector` is Linux-only: the automation tree ships its Windows build as
`Automation/automation/Include/SubInspector/Inspector/SubInspector.dll`
(`si_getVersion` 0x000501, the `v0.5.1` tag), and `CMakeLists.txt` builds the
same library from this checkout against the app's libass for the Linux package.
It is pinned at the first commit after `v0.5.2`, the one that dropped the old
build's nested submodules: the `v0.5.1` and `v0.5.2` tags still carry them, and
their fontconfig URLs (`git://anongit.freedesktop.org/fontconfig`,
`github.com/freedesktop/fontconfig`) no longer exist, so a recursive checkout of
either tag fails. Its `src/` is `v0.5.2`'s, which differs from `v0.5.1` only in
bounding libass's caches.

All eight are marked `shallow = true` in `.gitmodules`, so `--recurse-submodules`
fetches depth-1 rather than full history.

wxWidgets keeps several of its own third-party libraries (zlib, png, pcre,
scintilla, lexilla, expat…) in **nested** submodules, which is why the checkout
instructions use `--recursive`. Without it the wxWidgets build fails on empty
directories.

To bump one of the upstream five:

```sh
cd Thirdparty/<name>
git fetch --tags origin && git checkout <new-tag>
cd ../.. && git add Thirdparty/<name> && git commit
```

Then check whether the MSVC project in `Thirdparty/Build/<Name>/` needs new
source files — that is the part a version bump usually breaks.

## Archives

A few dependencies are still fetched as pinned, hash-verified archives via
[`dependencies.json`](dependencies.json) and [`hydrate.ps1`](hydrate.ps1),
because a submodule would not work or would not pay:

| Directory | Version | Why not a submodule |
|---|---|---|
| `fribidi` | 1.0.16 | **Its git tag ships none of the generated `lib/*.tab.i` tables or `fribidi-unicode-version.h` that the build's sources `#include`.** They are produced by the `gen.tab` programs at build time; only the release tarball has them pre-generated. |
| `boost` | 1.91.0 | The git repository is a ~500 MB tree of nested submodules |
| `icu` | 78.3 | Very large; only `source/common` and `source/i18n` are used |
| `zlib` | 1.3.2 | Small enough that an archive is simpler |
| `curl` | 8.20.0 | Likewise |
| `ffmpeg` | 7.1.1 | **Not source at all: a prebuilt developer package.** Building FFmpeg for MSVC from source needs MSYS2 and a full compile; this is headers, import libraries and DLLs. |

FFmpeg is a *shared* GPL build, so `avcodec-61.dll` and friends are copied next
to `HikariSub.exe` by a post-build step and have to ship with the application.
The Linux build already links the distribution's shared FFmpeg, so the two
platforms now agree. The package is x64, the only platform HikariSub builds.

`hydrate.ps1` verifies each archive's SHA-256 before extracting and refuses to
continue on a mismatch. It skips what is already present; pass `-Force` to
re-extract or `-Only <names>` for a subset.

## Build glue that lives here

The `Thirdparty/Build/<Name>/` project files are HikariSub's own work, not
upstream's, and are tracked. A few carry configuration upstream would normally
generate:

- `Build/Fribidi/fribidi-config.h` — stands in for the `configure`/meson output.
  It also defines `HAVE_STDLIB_H`, `HAVE_STRING_H`, `STDC_HEADERS` and
  `HAVE_STRINGIZE`, which fribidi 1.x expects from `config.h`. They live here and
  not in a `config.h` on purpose: `Libass.vcxproj` puts `Build\Fribidi` on its
  include path *ahead of* `Build\libass`, so a `config.h` in this directory would
  shadow libass's own and silently disable `CONFIG_ASM`.
- `Build/Libass/config.h` — libass's `CONFIG_*` switches.
- `Build/Libass/Libass.vcxproj` — passes the nasm flags upstream's meson build
  uses (`-Dprivate_prefix=ass -DPIC=1 -DARCH_X86_64=1`) and an `-I`
  pointing at the libass source root so `%include
  "x86/x86inc.asm"` resolves.
- `Build/HarfBuzz/HarfBuzz.vcxproj` — compiles upstream's `src/harfbuzz.cc`
  amalgamation, so a version bump needs no project edit.
- `Build/Zlib/Zlib.vcxproj` — zlib ships no MSVC project. `ffms2` links
  `Zlib.lib`, so this builds it from the hydrated sources.

## Still vendored in-tree

| Directory | Why |
|---|---|
| `luabins` | Three small fixes for modern LuaJIT (`luaL_reg` → `luaL_Reg`, `LUA_LIB`, `LUAI_BITSINT`); upstream is unmaintained |
| `uchardet` | Tracks upstream *master*, which is ahead of the 0.0.8 release by seven language models HikariSub compiles; pinning the release would lose them |
| `BaseClasses` | DirectShow base classes from the Windows SDK samples, locally patched |
| `LuaJIT`/`luajit` | Both builds compile it with `LUAJIT_ENABLE_LUA52COMPAT` and `LUAJIT_DISABLE_BUFFER`: Windows through `Build/LuaJit`, Linux through `cmake/VendoredLuaJIT.cmake`, which merges the two directories into one tree in the build directory |
| `DependencyControl` | The ffi-experiments sources of DependencyControl's native modules (BadMutex, PreciseTimer, DownloadManager). Windows builds them through the solution's projects, Linux through `cmake/DependencyControlModules.cmake`; both write them under `Automation/automation/Include/BM`, `PT` and `DM`, where `requireffi` loads them. One local fix: `CDlM_getStringSHA1`/`CDlM_getFileSHA1` returned the buffer of a freed local `std::string` |
| `DirectX9`, `karahelper` | Windows-only support code |

> `Thirdparty/luajit` and `Thirdparty/LuaJIT` are two directories that differ only
> in case. They hold disjoint file sets and merge into one directory on Windows
> and macOS, which is how the build has been finding all of LuaJIT. This is
> fragile and should be collapsed into a single directory.
