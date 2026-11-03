# Export as…

**File › Export as…** (`GLOBAL_EXPORT_SUBS_AS`, 5212) writes a copy of the editing target in another format. The open Document stays as it is: still ASS, with the same revision and the same file on disk. The writer is `core::exportDocument` (`src/core/export.cpp`). It reads the editor's draft, so text typed but not yet committed is exported too.

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
| MicroDVD | `.sub` | frames at the chosen rate: the last one used for that file (for a file never exported, the last export's), else the video's, else 23.976 | `{y:i}` `{y:b}` `{y:u}` `{y:s}` and `{c:$BBGGRR}` for a whole row | frame rate, merge overlaps |
| MPL2 | `.txt` | deciseconds | `/` for an italic row | merge overlaps |
| TMPlayer | `.txt` | whole seconds, start only | text only | overlaps are always merged; an empty `hh:mm:ss:` clears the screen when no Line follows at once |

**Merge overlaps** cuts time at every Line start and end that falls inside a run of overlapping Lines. Each piece becomes one cue holding the Lines on screen during it: top-aligned Lines first, then middle, then bottom, then document order.

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
| Position | `\pos`, or alignment other than bottom centre, where it isn't carried |
| Karaoke | karaoke tags, except WebVTT with karaoke timestamps |
| Effect | `Banner;`, `Scroll up;`, `Scroll down;` |
| Merged | shown in a cue with an overlapping Line |

## Save as ASS
Export as leaves the Document alone. To turn an SRT, WebVTT, SBV, MicroDVD, MPL2 or TMPlayer Document into ASS, use **File › Save as…**. For a Document in another format, it lists that format plus **ASS subtitles (\*.ass)**. A name ending in `.ass` also picks ASS. Choosing ASS first asks **Convert “…” to ASS before saving it?** and lists what changes. The list comes from a dry run of `core::convertDocument` with the **Settings › Conversion** options (frame rate or From video, style and catalog, new end times, time per character, tags at the start of each line, resolution): Lines given the conversion style, tags converted, times changed, sorting, and duplicates, empty Lines and markers removed. **Cancel** leaves the Document unconverted and saves nothing. **Convert to ASS** converts as one undoable step, "Subtitles conversion", writes the same list to the Log under "Save as ASS", then saves. If the frame rate is invalid, an error says so and nothing is converted or written. The save dialog on close asks the same way.

Subtitles › Conversion and its five commands (5020–5024, `GLOBAL_CONVERT_TO_*`) were removed in [#611](https://github.com/altqx/hikari/issues/611). Shortcut files and imported settings that bind them still load. The bindings are dropped with a Log notice that points to Save as (ASS) or Export as (other formats).

## Memory
The last format and options are kept per document file in `export.choices` (at most 100 rows, last used first). A document without a remembered choice, or without a file, gets the last export's.

## Tests
- `hikari_core_export_tests`: goldens for every format on `tests/fixtures/export/{plain,karaoke,typesetting}.ass` (`HIKARI_WRITE_EXPORT_GOLDENS=1` rewrites them), read-back through the SRT and line-format loaders, every option, and the report.
- `hikari_ui_export_dialog_tests`: the window, the unchanged Document, replacing a file, the report's links, the Lines choice and the memory.
- `hikari_ui_shell_tests`: `saveAsOffersAssForADocumentOfAnotherFormat`, `anAssFileNameConvertsWithTheDocumentsOwnFilter` and `storedConversionShortcutsAreDroppedWithANotice`; `hikari_application_hotkeys_tests` and `hikari_application_settings_import_tests` cover the retired commands.
