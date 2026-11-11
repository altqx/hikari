# ILL clipper2 wrapper

The native library behind ILL's `clipper2.clipper2` module
(`requireffi "clipper2.clipper2.clipper2"`), which ILL, `phos.AssfPlus` and the
scripts built on them need to load.

- Source: <https://github.com/TypesettingTools/ILL-Aegisub-Scripts>, commit
  `f3e581c04d74669bcf724f9fd2e49cf6c5c7607b`, unchanged:
  `ffi-packages/Clipper2/clipper.wrap.cpp` (sha256 `e87dc6f6…4660ff`) and
  `clipper.wrap.h` (sha256 `7c622bec…fa33be`).
- Licence: MIT, [`LICENSE`](LICENSE), the repository's root licence, added in the
  same commit as the wrapper (9b89e41c2e69, 2023-03-07).
- Clipper2 (BSL-1.0) comes from the owned overlay `ports/clipper2`, pinned at
  `866e40201476b9af9fec5ce80de16bf0b03eff7a`: the commit ILL's
  `ffi-packages/build.sh` builds against. It reports `CLIPPER2_VERSION` 1.2.2, as
  the `clipper2.dll` ILL users install does, but is 27 commits past that tag.

`src/helpers/lua/CMakeLists.txt` builds it as `libclipper2.so` / `clipper2.dll`
and stages it as `Include/clipper2/clipper2/`, where requireffi finds it.
