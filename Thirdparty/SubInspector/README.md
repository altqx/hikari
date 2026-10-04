# SubInspector

The native library behind ASSFoundation's `SubInspector.Inspector` module
(`Automation/automation/Include/SubInspector/Inspector.moon`), which measures
rendered subtitle bounds through libass.

- Source: <https://github.com/TypesettingTools/SubInspector>, tag `v0.5.1`,
  commit `931377c41cd89d6e7ecd117f581020d3a7f1bfc6`, unchanged:
  `src/SubInspector.c` (sha256 `45b71046…49228`), `src/SubInspector.h`
  (`cbe1baf7…ebad68`), `src/crc32table.h` (`5db5ba5e…1d60`).
- Licence: MIT, [`COPYING`](COPYING) (sha256 `89807acf…a473e`).
- Version: the legacy package's `Include/SubInspector/Inspector/SubInspector.dll`
  is this release: its `si_getVersion` returns `0x000501` (SI_VERSION in
  v0.5.1), built with MSVC and `NO_ZLIB` from the project's own solution,
  libass linked in statically.

The legacy app shipped that DLL for Windows only, so ASSFoundation failed on
Linux. A33-subinspector-linux (2026-10-05) builds it for Linux too:
`src/helpers/lua/CMakeLists.txt` compiles these sources as
`libSubInspector.so` against the rewrite's libass (`NO_ZLIB`, as the legacy
build), exporting only the `si_*` functions, and stages it as
`Include/SubInspector/Inspector/libSubInspector.so`, where requireffi finds
it. Windows keeps the legacy DLL as it shipped.

v0.5.1 built its libass with a patch that turns off the rasterizer's
approximated line scale (`deps/patches/0001-Don-t-use-approximation-in-rasterizer.patch`);
the Linux build uses the rewrite's libass 0.17.5 as it is, so bounds and
hashes can differ from the Windows DLL's by that rasterizer's precision.
