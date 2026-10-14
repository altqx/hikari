# The forks, and the wxWidgets patches

Three dependencies come from forks. ffms2 and xy-VSFilter carry HikariSub's
own changes: rather than sitting as unattributed copies in this tree, each is a
fork repository whose `hikarisub` branch is *upstream at a known commit, plus
commits that are the changes*. The submodule pins one of those commits. libass
is the altqx fork that HikariSub shares with other applications; see its
section below.

That makes the delta reviewable with ordinary git, and makes rebasing onto a
newer upstream an ordinary rebase.

## ffms2 — [altqx/ffms2](https://github.com/altqx/ffms2), branch `hikarisub`

- **Upstream:** <https://github.com/FFMS/ffms2>
- **Base:** tag `5.0`, plus upstream commit `9417465` ("Add layered decoding
  support") cherry-picked from master
- **HikariSub's own delta:** 61 lines across 3 files, in one commit

| Commit | What |
|---|---|
| `Add layered decoding support` | Upstream cherry-pick, not ours |
| `Add the Kainote metadata/attachment API and linear no-rewind init fix` | The track name/language, chapter, attachment and subtitle-demux API, exported from `ffms.h`, plus an `FFMS_ColorSpaces` enum. `FFMS_VERSION` becomes 5.1.0.0 so a patched ffms2 is distinguishable from stock 5.0. Also skips the `INITIALIZE_SOURCE` stage when `SeekMode < 0`, since that stage seeks and linear no-rewind mode must not. |

The *definitions* for that API are not in the fork at all — they live in this
repository, in `Thirdparty/Build/FFMS2/indexing_additional.cpp`, which both
builds compile.

> **Known hazard.** On Linux, `indexing_additional.cpp` defines out-of-line
> members of `FFMS_Indexer` against the submodule's private header
> `Thirdparty/ffms2/src/core/indexing.h`, then links against the *distribution's*
> `libffms2`. That is only correct while the distribution ships an ffms2 whose
> `FFMS_Indexer` layout matches this header. It matches ffms2 5.0, and nothing
> currently checks. `CMakeLists.txt` should pin a version range for `ffms2`, or
> the API should be upstreamed so the private header is not needed.

## xy-VSFilter — [altqx/xy-VSFilter](https://github.com/altqx/xy-VSFilter), branch `hikarisub`

- **Upstream:** <https://github.com/Cyberbeing/xy-VSFilter>, branch
  `xy_sub_filter_rc5`
- **Base:** commit `532b756` (2023-02-20, the branch tip; upstream has been
  dormant since)
- **HikariSub's delta:** 58 modified files (4248 lines) and 12 added, plus the
  removal of components HikariSub never builds

| Commit | What |
|---|---|
| `Remove upstream components Kainote does not build` | HikariSub builds seven of this repo's projects; the bundled gtest and boost_lib, the test fixtures, unrar, log4cplus's tests and the DX7 subpic backend are never compiled. Boost comes from HikariSub's own hydrated copy. |
| `Kainote's source changes` | CSRI entry points extended with extra colour formats and a direct `SimpleSubPicProvider` path, the subtitle/subpic fixes accumulated since, and the AviSynth interface headers under `include/avisynth/`. |
| `Build inside Kainote.sln` | Project and property files retargeted: toolset, output paths, include paths pointing at HikariSub's hydrated boost. |

The base was identified by comparing the vendored tree against every commit on
that branch and taking the one that maximised exact file matches — 940 of 998.

### Follow-up

The submodule path is still `xy-VSFilter-xy_sub_filter_rc5`, which is just how
GitHub's archive happened to unpack. Renaming it means touching `HikariSub.sln`
and every project that references it, so it is left for a separate change.

## libass — [altqx/libass](https://github.com/altqx/libass), branch `master`

- **Upstream:** <https://github.com/libass/libass>
- **Pin:** `4f5ea43630bb89c9caacaf4742a8af4f3e26edf7` on `master`. The fork
  cuts no tags for its consumers: each one pins a `master` commit, and moving
  the pin is a one-line change here (decision of 2026-10-15).
- **Before the fork:** upstream `4a05d81`, the 0.17.5 release.

The fork is upstream `master` (merged after 0.17.5) plus a performance batch
and a few fork-only APIs; the fork's README lists them. Its commits are checked
frame by frame against the commit before them, so the fork renders as
upstream `master` does. Against 0.17.5, the only rendering change found is
upstream's own: `057a533` and `b085eeb` draw text in symbol-cmap fonts and in
fonts with the legacy Arabic charset (178) on a Unicode cmap, which 0.17.5
draws nothing for. The fork's grapheme-cluster parsing renders as upstream
does since `4f5ea43`.

HikariSub uses none of the fork-only APIs yet. What the fork changes for a
consumer:

- `ASS_Image` lists are valid only until the next `ass_render_frame` on that
  renderer, as upstream documents; the nodes are reused.
- After the first `ass_render_frame` on a track, writing an event's `Start` or
  `Duration`, or inserting or reordering events by hand, needs
  `ass_track_events_modified` before the next render. HikariSub only ever
  parses whole scripts with `ass_read_memory`, so it is not affected.
- A track must not be rendered by two threads at once until it has been
  rendered once after its last change. HikariSub renders under one lock.
- Feature tests use the `LIBASS_HAS_*` macros. Upstream steps the low byte of
  `LIBASS_VERSION` for its own API additions too, so the version cannot tell
  the fork from upstream.

The Windows build compiles libass from `Thirdparty/Build/Libass/Libass.vcxproj`,
which lists libass's sources by hand. `.github/scripts/check-libass-vcxproj.py`
compares it with the C and x86 assembly sources the fork's `libass/meson.build`
and `libass/Makefile_library.am` build for x64 with DirectWrite; the Windows CI
job runs it first, so a pin that adds a source fails there rather than at the
link.

`Thirdparty/Build/Libass/libass_gdi_fontselect.cpp` includes libass's internal
`ass_fontselect.h` and `ass_utils.h`. It is excluded from every configuration,
but a fork change to `ASS_FontProviderFuncs` would break it without notice.

## Rebasing a fork onto newer upstream

```sh
cd Thirdparty/ffms2
git remote add upstream https://github.com/FFMS/ffms2.git
git fetch upstream --tags
git rebase --onto <new-tag> <old-base> hikarisub
# resolve, then:
git push --force-with-lease origin hikarisub
cd ../.. && git add Thirdparty/ffms2 && git commit   # move the submodule pin
```

To see just the delta at any time:

```sh
git -C Thirdparty/ffms2 log --oneline 5.0..hikarisub
git -C Thirdparty/ffms2 diff 5.0..hikarisub
```

## wxWidgets

No longer a fork. `Thirdparty/wxWidgets` used to be a modified wxWidgets 2.9.4 —
a 2012 development snapshot of a series upstream never stabilised — whose delta
was not a set of intentional patches: a tree-wide find-and-replace had commented
out 4152 `wxCHECK`/`wxASSERT`/`wxFAIL` lines across 658 files, including the
macro definitions in `include/wx/debug.h`, where `wxCHECK(cond, rc)` expanded to
nothing while 59 call sites still used it.

It is now a plain submodule at `v3.3.4`, pinned to upstream's tag, built by
wxWidgets' own `wx_vc17.sln` on Windows. Those guards are upstream's again.

The Linux build carries patches for wxGTK bugs that HikariSub hits and that
upstream has not fixed yet. They live in `Thirdparty/wxWidgets-patches/` and
`Thirdparty/build-wxwidgets-linux.sh` applies them to the submodule's working
tree before it configures wxWidgets (a patch that is already applied is
skipped), so after that script has run the submodule shows as modified. CI's
wxWidgets cache key includes the patches, so changing one rebuilds wxGTK.
Windows does not apply them: so far they only patch GTK code.

| Patch | What |
|---|---|
| `0001-gtk-overlay-disconnect-map-event.patch` | `src/gtk/overlay.cpp`, used only on Wayland: an overlay connected to its unmapped top-level window's `map-event` and never disconnected, so an overlay deleted before that window was shown left a handler that later ran on freed memory. The generic `wxCaret` of `HikariTextCtrl` and `DialogueTextEditor` draws through one, and the Style manager crashed in `gtk_widget_translate_coordinates` when it was opened after the style editor. |
| `0002-gtk-printable-key-not-control-key.patch` | `src/gtk/window.cpp`: the key code of a non-letter character comes from what the key with the same hardware code types in a fixed US layout, which is only the same key when the keymap uses the standard evdev codes. A virtual keyboard's keymap does not: wtype gives the first character it types the code of Escape, so typing a digit with it sent `WXK_ESCAPE` and closed the Style manager. The US layout's character is now taken only when it is printable. A virtual key whose code lands on another printable US key still gets that key's code in key down and char hook events (wtype's second character, on code 10, gets `'1'`); the typed text comes from the char event and is right. `tests/WxGtkKeyTests.cpp` checks the patch when the tests are built next to the pinned wxWidgets build (`HIKARISUB_TESTS_WX_PREFIX`, the Release `build-wx-linux/prefix` by default) and `xvfb-run` is installed, so it runs locally and on akari-rvm but not in CI, whose tests job builds no wxWidgets. |

When the submodule moves to a newer wxWidgets, drop each patch upstream has
taken; the script stops with an error on one that no longer applies.

The submodule moved from `v3.3.3` to `v3.3.4` on 2026-10-13. v3.3.4 does not
have the overlay fix: its `src/gtk/overlay.cpp` is the same as v3.3.3's, and
so is master's. `0001` is kept, and it applies to v3.3.4 unchanged. `0002`
is written against v3.3.4; master's `wxTranslateGTKKeyEventToWx()` has the
same code.
