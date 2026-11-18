# Automation 4 library

This is HikariSub's copy of the Aegisub automation library, tracked here rather
than fetched at build time: the upstream repositories do not publish several of
these files (the karaoke template helpers, the `json/` library, `effector`,
Yutils, ASSFoundation's `Common.lua`), and the files they do
publish differ from what HikariSub ships.

It was assembled from these revisions, then HikariSub's own files were laid over
them; `git log` on this directory is the history from there on:

| Source | Revision |
|---|---|
| `TypesettingTools/Aegisub` (`automation/include`, `automation/autoload`) | `ce97a367c611` |
| `TypesettingTools/DependencyControl` (`modules/l0`, `macros`), release v0.9.1 | `c4101aeed1a9` |
| `TypesettingTools/ASSFoundation` | `8fbb4f4261ef` |
| `TypesettingTools/Functional` | `5a28339d6d8b` |
| `TypesettingTools/Yutils` | `364f8fa5a9d9` |
| `TypesettingTools/Aegisub-Motion` (`src`, `Aegisub-Motion.moon`, `LICENSE`) | `ce44cf9131de` |
| `TypesettingTools/SubInspector` | `a346959b24ec` |
| `TypesettingTools/lyger-Aegisub-Scripts` (`modules/LibLyger.moon`) | `f07033106376` |
| `torque/ffi-experiments` (`DMv0.4.0-rqffiv0.1.2`: BadMutex, PreciseTimer, DownloadManager, requireffi) | `691b128703ba` |
| `lost-logarithm/karaOK` (`include/ln/kara.lua`) | `f749c4fa64e7` |

Files the released HikariSub package carries over upstream, and the changes it
makes to the ones upstream has too, came from that package; the noteworthy ones:

- `automation/include/aegisub/lfs.moon` wraps each `err_arg_to_multiple_return`
  implementation individually, because the `pairs` loop upstream uses does not
  survive on LuaJIT.
- `automation/include/aegisub/re.moon` frees the match result buffer with
  `ffi.C.free`.
- `automation/include/lyger/LibLyger.moon` declares `has_script_info` local
  before use.
- The `a-mo/*.moon` modules and `autoload/a-mo.Aegisub-Motion.moon` carry
  the version numbers upstream's `VersionDetemplater.moon` writes into the
  `##__X_VERSION__##` placeholders at that revision (Aegisub-Motion 1.1.2,
  TrimHandler 1.2.4). Upstream's `LICENSE` is `include/a-mo/LICENSE`. Before
  #1216 the copy was upstream `cecb70d5f` (2018-02-14, Aegisub-Motion 1.0.9),
  although this table named `a98331082f80`.
- DependencyControl's own files are the v0.9.1 release. The legacy package had
  mixed its 0.6.4 core with newer modules, which broke its feeds
  (DC1-depctrl-release). Twelve fixes are marked `HikariSub (DC1)`: the
  Toolbox releases the updater lock after Install and Update Script, its log
  level field starts at 2 (lower levels make ordinary messages raise), and
  Package Source Configuration writes into the reloaded config and clears
  the override feed when another kind is chosen;
  `ConfigHandler.getSerializableCopy` keeps a table referenced twice, a
  save keeps another writer's keys on the next save, a schema migration
  rereads and writes the file under one lock, and a corrupted file is set
  aside under the lock it was read with; an `UpdateFeed` built without
  `FeedLoader` loads without a cache;
  `DependencyControl.ConfigHandler` is the class of what `getConfigHandler`
  returns (`ConfigView`), which scripts written for 0.6 compare against; and
  an installed package updates only from its own feeds, and a missing one
  that names its feed takes another feed's build only when the user agrees.
  `FileCache` (marked `HikariSub (#500)`) names entries by the full SHA-1 of
  their key instead of a 7-hex prefix and returns an index only when the key
  stored in it matches, so one feed URL can't pose as another's cache entry.
- `automation/include/moonscript.lua` is MoonScript 0.7.0, from
  `TypesettingTools/Aegisub` `43af875ea01e` (what Aegisub 3.5 ships). The
  released package's 0.3.0 cannot compile DependencyControl 0.9's Toolbox.
  Aegisub-Motion before `897cd7f6a` doesn't compile under 0.7.0
  (DC1-moonscript), so it moved to the revision above together with it.
- `automation/include/ln/kara.lua` is karaOK's `ln.kara`, by logarithm with
  petzku. Upstream states no licence; HikariSub's eight fixes, marked
  `HikariSub (TH5d2)`, are MIT (TH5d2-ln-kara in
  `docs/compatibility-decisions.md`).
- `BM/`, `DM/`, `PT/` are HikariSub's DependencyControl modules; their `.lua` is
  what the released package ships and their DLLs are built from this repository
  (`Thirdparty/DependencyControl/`) and installed into those directories by
  the build (`src/helpers/lua/CMakeLists.txt`).

The modules we build ourselves (`BM/BadMutex.dll`, `PT/PreciseTimer.dll`,
`DM/DownloadManager.dll`) are not tracked here; packaging takes them from the
build output. `SubInspector.dll` is, because this solution builds SubInspector only
on Linux.

## Refreshing from upstream

Bump a revision, copy the files in, and commit: there is no build-time fetching
or patching to keep in step. Dictionaries and the VSFiltermod renderer are still
fetched at build time (`cmake/HikariPackageWindows.cmake`,
`cmake/HikariVsfilterMod.cmake`).
