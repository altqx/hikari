# Renderer font identity — throwaway native probe

Question: can a collector prove which bytes and collection faces libass actually selected? [Prototype renderer-verified font identities and collection diagnostics](https://github.com/altqx/hikari/issues/47) remains open for feasibility and human review. This experiment is not a production patch, complete collector, or approved hook ABI.

Open **[evidence/report.html](evidence/report.html)** directly. It is self-contained: observed diagnostics and small images generated from real libass `ASS_Image` output, not screenshots or Qt samples. **[evidence/observed.json](evidence/observed.json)** preserves both runs' messages, identities, SHA-256 values and comparisons. The page's expandable cards expose the evidence behind each outcome.

## Run on the existing Windows development machine

From this directory:

```powershell
& "$env:LOCALAPPDATA/HikariSub/prototype-runtime/Scripts/python.exe" run.py --source C:/Work/Kainote
```

A normal Python 3.10+ interpreter also works; the script uses only the standard library. It requires the already installed MSVC x64 toolset/Windows SDK and the existing source checkout's `bin/x64/Debug/{Libass,FreeType2,Fribidi,HarfBuzz,Zlib}.lib`. It discovers Visual Studio with `vswhere`. It does not install anything or build the application. A different dependency checkout can be supplied with `--source`.

The driver requires clean libass source at **4a05d8127f525943ebf45fdc6497c9e665947f0d**. It writes fresh, ignored `_run/<UTC>/` files and replaces this directory's two evidence files. It does not modify source/dependency checkouts, register fonts with Windows or Qt, or change font installation. Build output, copied upstream C sources and captured proprietary system-font bytes remain in ignored `_run/`; do not publish that directory. Synthetic fixtures are regenerated from `fixtures.py`.

## Observed result, 2026-09-27 local time

Executed on Windows 11 build 26200 using the installed MSVC 14.51.36231 toolset. Runtime libass reports API `0x01705000`; the actual provider log says **directwrite (with GDI)**. The evidence records platform, compiler, exact source hashes, build-config hash and all supplied archive hashes. This is a Debug-library feasibility run, not a speed or release-build result.

| Observation | What it establishes here |
| --- | --- |
| 25/25 stock/hook frames have equal composite-pixel SHA-256 | The added capture did not change those observed pixels. This is not universal noninterference proof. |
| `attachment_a` and `attachment_b` have identical public `fontselect` lines but distinct hashes/pixels | A display name plus face 0 cannot distinguish same-name font bytes. |
| `collision_ab` uses A; `collision_ba` uses B | First attachment won for these duplicate fixtures. This is characterized behavior, not a universal precedence policy. |
| `system_attachment_collision` selects the original synthetic Arial-named attachment; independent GDI/DirectWrite resolves system `ARIAL.TTF` | A separate platform lookup can give a plausible name/face but the wrong bytes for this document. |
| `collection_a/b` capture the same TTC bytes, opened face 0/1 respectively | Collection identity requires both byte hash and face index. |
| Legacy family and full-name fixture requests render; its PostScript-only and typographic-family requests do not | Preserve authored names; these observed alias results are not a general guarantee for all font formats/providers. |
| Missing family uses the explicitly configured default; missing CJK glyph logs failure; controlled fallback captures two files | Missing request, fallback and missing glyph are different outcomes even with visible output. |
| Bold+italic fixture captures the regular bytes and reports both actual glyph-load simulations | A file hash alone does not describe rendered styling. Cached glyphs do not emit another simulation event. |
| System multilingual frame selects Arial, Yu Gothic UI Semibold TTC face 2, and Segoe UI Emoji | The base request's independent metadata query misses actual fallback dependencies. No linguistic correctness judgment is made. |
| Two live libraries are rendered A/B/A, B is destroyed, and a new B generation is created while old A remains alive | These attachment generations remained distinct in this single-thread interleaving. It is not multithread stress or OS-install refresh. |
| 17/18 captured-set reimports have equal pixels with provider NONE | Reimport of each trace's complete captured file set is exercised in a fresh library with no system provider. Two unresolved alias requests had no captured set to replay. |
| System multilingual reimport differs despite loading all three captured files | Files alone do not recreate DirectWrite's fallback resolver. A collected file list must not be certified complete merely because copying succeeded. |

Equal reimport pixels can also reproduce a missing request/glyph; equality is not automatically success. These are short frames, not whole-document coverage.

## Mechanism and exact limits

Both builds recompile the pristine pinned `ass_fontselect.c` and `ass_font.c`. The hook build adds three diagnostic calls via exact-anchor replacements in `run.py`; other native objects come from the hashed local archives. No large upstream tree or binary is vendored. Source hashes for stock and modified copies are in `observed.json`.

1. At successful selection, `selected_v1` copies the **selected provider's actual stream** (or path bytes), records the provider-local UID, requested character/weight/italic, display/PS name and supplied face index. Python hashes the captured bytes. The probe caps individual captures at 128 MiB, an experiment limit only.
2. After FreeType successfully opens that selection, `opened_face_v1` records its actual `face_index`, collection face count and PS name. Join it to the captured bytes using the UID **inside this library/context**, never globally.
3. At successful glyph loading, `glyph_simulation_v1` records the same conditions that triggered libass's embolden/italicize calls. It is not a complete per-frame/per-glyph provenance stream.

The stock public log returns a display name for stream-backed fonts; it does not export their bytes. [Selector source](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_fontselect.c#L788), [FreeType face opening and simulation](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_font.c#L433), [DirectWrite stream access](https://github.com/libass/libass/blob/4a05d8127f525943ebf45fdc6497c9e665947f0d/libass/ass_directwrite.c#L282). The independent platform query uses GDI → DirectWrite face → font-file loader stream/path. It is explicitly a **candidate**, not proof until compared with the renderer stream.

The existing archive passes pointer `1` to its source-version `%s` log, consistent with the project's bare `CONFIG_SOURCEVERSION` define. Initial probe logging dereferenced it and failed. Both final builds guard that one diagnostic and record `archive_source_label_invalid`; no renderer algorithm was patched for this workaround. Do not infer the shipped application crashes: its logging path was not exercised. Remaining archived objects were not rebuilt, so their binary hashes are provenance, not proof of a reproducible build from the pinned source.

## Inputs, rights and remaining gates

`fixtures.py` creates original rectangle outlines, ASCII and selected Arabic/combining/CJK cmap entries, deliberate name collisions and a two-face TTC. Generated font names/outlines are dedicated to **CC0-1.0**; no font outlines or tables were copied from another font. The generator/probe code follows the repository's code license. Synthetic glyphs test identity and coverage decisions, not credible script shaping. System-font bytes are read only on this machine and excluded from version control; the report contains hashes, metadata and small rendered text samples.

**Unexecuted/inconclusive:** Linux/fontconfig, macOS, variable/named-instance coordinates, OTC/CFF and all real-world alias forms, full-document/style/inline-tag enumeration, attachment parsing from containers, mid-render cancellation, asynchronous font installs/removals, concurrent rendering threads, malformed-font robustness, memory/performance limits and optional CSRI/VSFilter. No variable-instance or security-sandbox claim is made. The hash-before-open experiment also assumes immutable fixture/provider streams during the call; it is not a file-change race solution.

The hook demonstrates a feasible Windows identity seam and the public-log gap. Production work still needs a reviewed callback/lifetime contract, cache-aware provenance, environment/fallback reconstruction, coverage across supported platforms, and regression coverage. **Human review remains open:** are the distinctions between captured identity, known fallback, missing request/glyph and incomplete reimport understandable, and is this narrowly maintained diagnostic seam acceptable? This artifact makes no new architecture decision.

Context: [accepted font contract](https://github.com/altqx/hikari/blob/5feb8b301e4f9d8a0dde1a7cf1ced47357ff8e1b/docs/qt/fonts.md), [completed font research](https://github.com/altqx/hikari/blob/aff69e6c950a1ab03d02eb837ece9d47b90d1dae/docs/research/fonts.md).
