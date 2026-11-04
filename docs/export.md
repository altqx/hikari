# Export as…

**File › Export as…** (`GLOBAL_EXPORT_SUBS_AS`, 5212) writes a copy of the editing target in another format. The open Document stays as it is: still ASS, with the same revision and the same file on disk. The one exception is changed srv3 style options, which are saved in it (below). The writer is `core::exportDocument` (`src/core/export.cpp`). It reads the editor's draft, so text typed but not yet committed is exported too.

## Which Lines go out
- Dialogue Lines only. Comments and unparsed lines are skipped and counted.
- **All**, **Selected** (the grid selection) or **Shown** (the rows the grid's filter and folds show).
- A Line with nothing visible left after conversion (a drawing, empty text) or with no duration is skipped and counted. Plain text keeps zero-length Lines, because it has no times.
- Timed formats are sorted by start time; plain text keeps document order.

## Shared conversion
| ASS | Export |
|---|---|
| `\N` | a new row (`\|` in MicroDVD, MPL2 and TMPlayer; a space in plain text) |
| `\n` | a space, or a new row when `WrapStyle: 2` |
| `\h` | U+00A0 |
| `{…}` without tags | removed |
| an unclosed `{` | kept as text |
| `\i` `\b` `\u` `\s` `\c` `\1c`, `\r`, `\rStyle` | the format's own markup where it has one (below) |
| `\pos`, `\move` (its start), `\an`, `\a`, margins | WebVTT cue settings; SRT `{\anN}` |
| `\k` `\K` `\kf` `\ko` `\kt` | flattened; WebVTT timestamp tags when asked |
| `\p1` and up | the drawing is removed |

Spaces at the start and end of each row are trimmed, and empty rows are dropped, because a blank line ends a cue in SRT, WebVTT and SBV.

## Formats
| Format | Extension | Times | Carries | Options |
|---|---|---|---|---|
| SubRip (SRT) | `.srt` | `00:00:01,000`, CRLF | `<b>` `<i>` `<u>` (nested properly); `<font color>`; `{\anN}` | alignment tags (on), font colours (off), merge overlaps (off) |
| WebVTT | `.vtt` | `00:00:01.000`, LF | `<b>` `<i>` `<u>`; one `::cue(.Style)` rule per Style used (colour, alpha, font, bold, italic, decoration, outline as `text-shadow`, opaque box as `background-color`); colour tags as `::cue(.cRRGGBB)` classes; `line`/`position`/`align` from alignment, `\pos` and margins over PlayRes | positions (on), styles and colours (on), karaoke timestamps (off) |
| YouTube SBV | `.sbv` | `0:00:01.000,0:00:02.000`, CRLF | text only | merge overlaps |
| Plain text | `.txt` | none, LF | text only, one Line per row | times (tab separated), actor names |
| MicroDVD | `.sub` | frames at the chosen rate (the video's when open, else 23.976) | `{y:i}` `{y:b}` `{y:u}` `{y:s}` and `{c:$BBGGRR}` for a whole row | frame rate, merge overlaps |
| MPL2 | `.txt` | deciseconds | `/` for an italic row | merge overlaps |
| TMPlayer | `.txt` | whole seconds, start only | text only | overlaps are always merged; an empty `hh:mm:ss:` clears the screen when no Line follows at once |
| TTML (IMSC1) | `.ttml` | `00:00:01.000`, or frames (`24f`) with `ttp:frameRate`; LF | IMSC1 text profile (`ttp:profile`); one `<style>` per Style used; `<span>` for tag changes (`tts:fontStyle`, `tts:fontWeight`, `tts:textDecoration`, `tts:color`); up to three regions (below) | Netflix timed-text style (off), times in frames (off), frame rate, language |
| DFXP | `.dfxp` | as TTML | as TTML, without the IMSC1 profile designator | as TTML |
| YouTube timed text (srv3) | `.ytt` | milliseconds, YTSubConverter's own conversion (below) | what YTSubConverter 1.6.5 carries | per-style options |

**Merge overlaps** cuts time at every Line start and end that falls inside a run of overlapping Lines. Each piece becomes one cue holding the Lines on screen during it: top-aligned Lines first, then middle, then bottom, then document order.

## TTML and DFXP
Both are TTML1 documents in UTF-8. The TTML one declares the IMSC1 text profile. `xml:lang` is the Language field (empty when left empty).

- **Styles:** each Style used becomes a `<style>`: `tts:fontFamily` (the font, then `proportionalSansSerif`), `tts:fontSize` as a percentage of the default font size (a 15th of PlayResY), `tts:color` with alpha, bold, italic, underline and strikeout. An outline becomes `tts:textOutline` in `em`, at most `0.1em` as IMSC1 requires. An opaque box (border style 3 or 4) becomes a `tts:backgroundColor` style on a `<span>` around the text. TTML1 has no shadow.
- **Regions:** IMSC1 forbids regions that overlap while shown, so the screen is split into a top, a middle and a bottom third of the area inside the margins most Lines use. Each Line goes into the third its alignment names (with `\pos`, the third its y falls in), with `tts:textAlign` left, centre or right. The report lists a Line as **Position** when it was placed this way approximately: it has `\pos`, or margins other than those most Lines use.
- **Times:** milliseconds by default. With **Times in frames**, times are frame counts (`24f`) at the chosen frame rate. `ttp:frameRate` is the whole rate, and NTSC rates (23.976, 29.97, 59.94) add `ttp:frameRateMultiplier="1000 1001"`.
- **Netflix timed-text style:** a stricter profile in the style of Netflix’s timed-text requirements. Times are in frames. Text stays white, in the default font and size, and centred, in a top region (`10% 10%`, `80% 40%`) or a bottom one (`10% 50%`, `80% 40%`). Only italics are kept; the report lists colour tags (**Colour**), bold, underline and strikeout (**Formatting**), and Lines moved to the top or bottom (**Position**).
- **Schema:** CI checks the TTML and DFXP goldens against the W3C TTML1 and IMSC1 schemas with xmllint (`Export.TtmlGoldensMatchTheImsc1Schema`, Linux; the schemas are in `tests/fixtures/export/ttml-schemas`). The core tests also check what the schemas can't: percentages, regions inside the root and never overlapping, and the outline limit.

## YouTube timed text (srv3)
srv3 is a port of [YTSubConverter](https://github.com/arcusmaximus/YTSubConverter) 1.6.5's ASS → srv3 conversion (MIT, credited in About and `licenses/ytsubconverter`). Its output is byte for byte what YTSubConverter writes, BOM included. The core is `core::srv3FromAss` (`src/core/srv3_*.cpp`). `core::exportDocument` feeds it the Lines as the video renderer sees them (`encodeAss` in renderer mode), leaving out `[Fonts]` and `[Graphics]`, because YTSubConverter refuses their encoded lines. The shared conversion above doesn't apply. When YTSubConverter refuses a script, nothing is written, and the window shows its reason.

- **The port:** the strings are UTF-16 and the times are 100 ns ticks. C# `float` maths stays `float`, built with `-ffp-contract=off` on GCC. `Math.Round` is banker's rounding. Dictionary order follows .NET, and regexes run on PCRE2-16. YTSubConverter reads tags as `\\(\d?[a-z]+)` and ignores any name it has no handler for.
- **Goldens:** `tests/fixtures/export/srv3/golden` holds YTSubConverter's output for its samples, its own test files and Hikari's three export fixtures, each plain and with `StyleOptions.xml`. `tools/srv3-goldens/regenerate.sh` rewrites them with the pinned release on a pinned .NET 8 runtime; both are checked by hash. It needs network access and `xvfb-run`, and runs by hand.

### Style options
When srv3 is the format, Options shows one row per style that the chosen Lines use. **Show all styles** lists the rest.
- **Shadow** (Glow, Soft, Hard, Bevel, more than one allowed): how YouTube draws the style's shadow. The toggles are disabled when the style's Shadow is 0. A non-box outline is always drawn as a glow.
- **Karaoke**, with **current-word** text, outline and shadow colours. The swatches open the colour popover without Alpha and with **No colour**.
- **Defaults:** a style named like one of YTSubConverter's 15 built-ins (`YTGlow`, `YTSoftShadowBox`, `__ytbevel`, …) starts from its built-in options and is labelled "YTSubConverter". Every other style starts from a soft shadow with no karaoke, which is what YTSubConverter does without options. A dot and a Reset button mark a row that differs from its default.
- **Storage:** only rows that differ from their default are written, as `[Script Info]` keys numbered in style order and rewritten as one block (`core::setSrv3StyleOptions`):
  ```
  Hikari srv3 1: Karaoke-Romaji,glow+hard,karaoke,#FFE45C,#7A2BD8,
  ```
  - **Fields:** the style, the shadow types (`glow`, `soft`, `hard` and `bevel` joined with `+`, or `none`), `karaoke` or `-`, then the three colours (`#RRGGBB`, empty for No colour).
  - **Why numbered keys:** YTSubConverter reads Script Info as `^([\w ]+):`, so a key with a style name in it would make it refuse the file. It ignores these keys, and so do libass and Aegisub, which keeps them on save.
  - **Keys for missing styles:** keys naming a style the file doesn't have are kept.
- **When they're saved:** Export saves changed options as one undo step ("Set srv3 style options") and marks the file as changed. The done view says how many styles changed. Cancel discards the changes.
- **Following the styles:** renaming a style in the Style manager or the Styles panel renames its key in the same undo step. Deleting a style removes its key (Clean up too), and duplicating a style copies it.

## The report
After writing, the window lists every Line that lost or had something approximated, with its grid row; choosing a row selects that Line. The kinds (`core::ExportLoss`):

| Kind | When |
|---|---|
| Drawing | `\p1` and up |
| Clip | `\clip`, `\iclip` |
| Transform | `\t` |
| Movement | `\move` (WebVTT keeps the start position) |
| Fade | `\fad`, `\fade` |
| Rotation | `\org`, or a non-zero `\fr` `\frx` `\fry` `\frz` `\fax` `\fay` |
| Font | `\fn` `\fs` `\fscx` `\fscy` `\fsp` |
| Outline | borders, shadows, `\be`, `\blur`, alphas, `\2c` `\3c` `\4c` |
| Colour | `\c` or `\1c` where the format or options can't carry it |
| Formatting | `\i` `\b` `\u` `\s` where the format can't carry them (SRT and WebVTT: `\s` only) |
| Position | `\pos`, or alignment other than bottom centre, where it isn't carried; in TTML and DFXP, placed in the nearest region |
| Karaoke | karaoke tags, except WebVTT with karaoke timestamps |
| Effect | `Banner;`, `Scroll up;`, `Scroll down;` |
| Merged | shown in a cue with an overlapping Line |

srv3 reports what YTSubConverter ignores. **Drawing** is `\p1` and up, **Clip** is `\clip` and `\iclip`, and **Rotation** is the `\fr…` tags, `\fax`, `\fay` and `\org`. **Font** covers `\fscx`, `\fscy` and `\fsp`. **Outline** is the border, shadow and blur tags. **Formatting** is `\s1`, **Position** is `\a`, **Karaoke** is `\kf`, `\ko` and `\K`, and **Effect** is Banner and Scroll.

## Memory
The last format and options are kept per document file in `export.choices` (at most 100 rows, last used first). A document without a remembered choice, or without a file, gets the last export's.

## Tests
- `hikari_core_export_tests`: goldens for every format on `tests/fixtures/export/{plain,karaoke,typesetting}.ass`, TTML in three variants (`.ttml`, `.dfxp` in frames, `.netflix.ttml`) (`HIKARI_WRITE_EXPORT_GOLDENS=1` rewrites them); read-back through the SRT and line-format loaders; every option; the report; and the TTML layout rules.
- `Export.TtmlGoldensMatchTheImsc1Schema` (Linux): the TTML and DFXP goldens against the W3C schemas.
- `hikari_core_srv3_tests`: byte-identical output on 21 inputs, plain and with 38 style options, the Hikari fixtures, refusals, and the style-option keys (read, write, defaults). The srv3 cases in `hikari_core_export_tests` cover the Lines choice, attachments, the report and the saved options.
- `hikari_application_style_manager_tests`: srv3 options follow renames, deletions and copies.
- `hikari_ui_export_dialog_tests`: the window, the unchanged Document, replacing a file, the report's links, the Lines choice, the memory, the TTML options (language, frames, Netflix style), and srv3: the conversion and its report, the style table, saving as one undo step, Reset, and Cancel.
