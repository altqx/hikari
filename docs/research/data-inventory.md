# HikariSub persisted-data inventory

Research for [issue #4](https://github.com/altqx/hikari/issues/4), completed 2026-09-27. Source baseline: [`20d647c4c769ab7f5d383cf3c1c33f03876a94e9`](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9). This is a static source audit, not a claim that malformed files, encodings, or every platform were exercised. The appendices enumerate the complete option, color, action/default-binding and legacy conversion registries at that commit. Source expressions are retained where a friendly interpretation would lose information.

## Conclusions for the importer and document model

Subtitles, autosaved subtitles, `.sty` catalogs, font catalogs, and `.kls` sessions need direct readers which leave their input files unchanged. Options, themes, histories/profiles contained in options, and hotkeys need a one-shot importer into the new settings model. Preserve the original directory and bytes, record import provenance, and report unknown settings/bindings and unavailable paths rather than silently discarding them. Rules and the personal dictionary are authored user data and need lossless import or direct loading. These are recommendations for the downstream design, not decisions already implemented.

There is **no separate persistent autosave index** in the inspected implementation: the recovery list is reconstructed from names and modification times. There is also **no dedicated writer for Aegisub Extradata or Aegisub Project Garbage**: HikariSub persists its project metadata as Script Info and certain event-field conventions. A generic ASS loader does not consequently preserve arbitrary unknown sections. An importer must distinguish these facts from assumptions implicit in the issue's wording.

## Locations and common text handling

Let **E** be the directory containing the executable. `config::LoadOptions` sets `pathfull` to that directory and `configPath` to `E/Config`. The same expressions apply on Windows and the current Linux code; there is no AppData/XDG relocation for the files below. Linux desktop integration uses XDG for its launcher, not these settings. A future macOS package is not evidence for an existing macOS data directory. User-chosen files can reside anywhere. Paths in files may still contain Windows separators; the path helpers normalize them on non-Windows builds. [Path setup](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L541), [path helpers](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.h#L40), [Linux integration](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/packaging/linux/install-desktop-integration.sh).

`OpenWrite::FileWrite` defaults to BOM-prefixed UTF-8 (`utf=false` instead uses local conversion). `PartFileWrite` emits a BOM on its first write, but delegates encoding to `wxFile::Write`'s default converter; verify non-ASCII fixtures with the actual wx build rather than inferring a converter from the extension. Readers using `FileOpen(...,test=true)` detect a UTF-8 BOM, validate non-ASCII UTF-8, then try uchardet/`wxCSConv` and the locale fallback. ASCII-only text falls through the UTF-8 validator; empty files fail. `test=false` skips that detection and uses `ReadAll`. Most record writers use CRLF; brace tables internally contain LF. No universal escaping scheme exists. [Reader/writer implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OpennWrite.cpp), [defaults](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OpennWrite.h).

| File or family | Location | Contents/version marker | Carry-over |
|---|---|---|---|
| Main options | E/Config/Config.txt | `[program name/version]`, key/value and brace tables | One-shot importer |
| Audio options | E/Config/AudioConfig.txt | Same syntax; ordinal partition described below | One-shot importer |
| Bindings | E/Config/Hotkeys.txt, AudioHotkeys.txt | Program header, action/context/binding lines | One-shot importer |
| Themes | E/Themes/name.txt | Color key/hex value; no current header | Import into new tokens without making old widget names the new design |
| Last session | E/Config/LastSession.txt | Program header, optional close marker, tab blocks | Direct read, do not rewrite input |
| Exported session | User-selected `.kls` | Same tab-block grammar | Direct read |
| Autosaved subtitles | E/Subs/name_tab_sequence.ass/srt/txt | Actual subtitle payload, no wrapper | Direct read |
| Crash recovery subtitles | E/Recovery/name.ext | Actual subtitle payload referenced by session | Direct read |
| Style catalogs | E/Catalog/name.sty | ASS `Style:` records; no catalog version | Direct read |
| Font catalogs | E/Config/FontCatalogs.txt; user-imported paths | Named brace blocks; no version | Direct read |
| Font catalog safety copies | E/Config/FontCatalogsAutosaveN.txt | Same grammar | Direct read |
| Histories, recent paths, shift profiles | Entries inside Config.txt | Table and packed-string fields | Import as part of settings |
| Misspell rules | E/Config/Rules.txt | Comment header, positional enabled flags, form-feed fields | Preserve user-authored rules |
| Personal spelling words | E/Dictionary/UserDic.udic | Newline-delimited words | Preserve/import |
| FFMS indices | E/Indices/mediaBase_audioTrack.ffindex | FFMS library binary | Regenerable cache |
| Decoded audio | E/AudioCache/mediaBase_trackN_Cch_D.w64, temporary .part | Raw interleaved PCM despite the `.w64` name | Regenerable cache |

For encoding by family: main/audio config, hotkeys, sessions, style catalogs, rules and personal words use the explicit UTF-8+BOM `FileWrite` path. Custom themes, font catalogs and ordinary subtitle/autosave output use `PartFileWrite` and its converter caveat above. Config, themes, styles, sessions, font catalogs and personal words call `FileOpen` with `test=false`; hotkeys/rules use detection. Subtitle input passes through the subtitle-opening path and encoding detection. The binary FFMS index and raw PCM cache have no text encoding. These differences belong in import fixtures; an extension alone does not determine encoding.

## Main/audio configuration: grammar, defaults, versions

`GetRawOptions` writes nonempty entries only. `IsAudioOption` means enum ordinals through `AUDIO_WHEEL_DEFAULT_TO_ZOOM`, not every key whose name begins AUDIO: `AUDIO_BOX_HEIGHT`, `ACCEPTED_AUDIO_STREAM`, and `AUDIO_RECENT_FILES` belong to the main file. Every registry key, storage family, explicit default, consumer type and example is listed in Appendix A. An unassigned key starts as the empty string, so bool reads false, integer/failed floating reads zero, and table reads empty; UI code may supply another fallback, shown by the consumer evidence where present. Do not invent universal defaults from labels. [Partition and API](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.h#L451), [serialization](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L246), [main defaults](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L345), [audio defaults](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L804).

A scalar is `NAME=value`, split at the first equals sign; the label is trimmed and mapped through the registry. Bool is literal `true`; numbers use wx conversions. Coordinates are `x,y`. A table is `NAME={`, followed by tab-prefixed entries and a closing `}`. The parser recognizes a line ending in `{`, collects until the closing line, and table readers strip the wrapping syntax and first tab. Newline, tab, brace, equals and delimiter-containing values need fixtures: there is no general quoted-string/escape grammar. Unknown keys map to enum slot zero, described in the header as trash. [Parser](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L122), [table operations](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L728).

The header uses `Options.progname`, containing application version/build information, rather than a dedicated data schema version. A changed header loads defaults before overlaying file values. `___Program Crashed___` is an additional crash suffix. `ConfigNeedToConvert` accepts modern SemVer without conversion; older four-part versions with build below 1142, and malformed old versions, enter the converter. More than ten parsed options are required for the normal success path. Linux can retain an invalid file while using defaults; Windows follows its warning/failure path. The legacy converter mutates files in place, renames keys, splits selected old values using the delimiters in Appendix D, and specially maps toolbar actions. This destructive reuse is inappropriate for the new one-shot importer. [Load/version handling](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L541), [converter implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ConfigConverter.cpp#L503).

### Packed settings and values requiring interpretation

* `LAST_SESSION_CONFIG`: 0 do nothing, 1 ask, 2 load on start. It is a three-valued choice, not independent flags.
* `SHIFT_TIMES_OPTIONS`: 1 forward, 2 use start boundary when anchoring, 4 anchor to video, 8 anchor to audio, 16 display/use frames, 32 shift override-tag times. Older separately named SHIFT_TIMES bools remain registry entries; do not equate their defaults with this packed field.
* `POSTPROCESSOR_ON`: 1 lead-in, 2 lead-out, 4 continuous timing, 8 keyframe snapping, 16 show postprocessor mode. Lead/threshold/keyframe tolerance fields are integer milliseconds.
* `SUBS_COMPARISON_TYPE`: 1 times, 2 styles, 4 chosen styles, 8 visible, 16 selections. `GRID_FILTER_BY`: 1 styles, 2 selection, 4 comments, 8 doubtful, 16 untranslated.
* Column masks use Layer=1, Start=2, End=4, Style=8, Actor=16, MarginL=32, MarginR=64, MarginV=128, Effect=256, CPS=512, Text=1024, TextTl=2048, Comment=4096, Wraps=8192. Copy/paste/hide consumers must be checked separately: the copy-columns path also uses 2048 for strip-tags behavior.
* `TEXT_EDITOR_TAG_LIST_OPTIONS`: 1 visual-tool tags, 2 VSFilterMod tags, 4 descriptions. `FIND_REPLACE_OPTIONS` and `SELECT_LINES_OPTIONS` exact bit definitions are copied into Appendix E to preserve all alternatives.
* `TOOLBAR_IDS` is an ordered action-ID table. Editbox tag button slots 1–20 each store a brace table ordered as tag text, insertion type (0 at cursor, 1 at beginning, 2 plain text), button name; `EDITBOX_TAG_BUTTONS` is their enabled count (0–20). `COLORPICKER_RECENT_COLORS` is space-separated ASS color text, padded with black for empty UI cells; preserve its exact raw value before conversion. [Tag button fields](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1793), [color-history grammar](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L209).

[Session menu](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L964), [shift options/profiles](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp), [comparison](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.h#L140), [filter bits](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridFiltering.h#L52), [column bits](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.h), [tag-list bits](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.h#L23).

Themes serialize `NAME=#RRGGBB` or `NAME=#AARRGGBB`, unlike ASS's BGR color notation. DarkSentro/LightSentro are built-in defaults; Appendix B enumerates every color and both defaults. Custom files have no current header. Missing colors fall back to dark defaults and can trigger a repair save; an old first line without an underscore triggers old-name conversion. [Theme loader/writer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L894).

## Hotkeys and automation

After the application header, the normal record is `ACTION_SYMBOL C=Binding`; C is G global, S grid, E editbox, V video, A audio. Legacy numeric action IDs are accepted. Appendix C lists every named action, numeric ID, and explicit default; actions absent from the default table start unbound. The key-name table recognizes named navigation keys, function keys and Num-pad keys; bindings use modifier prefixes such as `Ctrl-Shift-`. Preserve spelling and scope before translating to Qt key sequences. The same chord can intentionally exist in multiple contexts. [Load/save/defaults/key parsing](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp), [registry](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.h).

Automation uses `Script <scriptfilename>-<macroindex>=Binding`. Runtime IDs begin at 30100 and depend on loading order; persist script identity plus macro ordinal, not that transient integer. Macro renaming/reordering and same-basename scripts require unresolved-import reporting. Blank bindings are skipped; the writer also skips IDs below 100. [Automation binding identity](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationHotkeysDialog.cpp#L114), [application](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1373).

Hotkey version handling still parses the old four-part version differently from the main config: it requires an old build number above 487 and converts below 1141. Missing/outdated headers in an existing file cause defaults to be saved; a missing file loads defaults in memory. Ten or fewer recognized binding lines also trigger default loading, so a deliberately small custom file needs an import fixture. Modern header handling therefore needs a regression fixture; do not reuse this loader as a non-destructive importer. Legacy scope N becomes S, W becomes V; every old action-name mapping is in Appendix D. This audit identifies source-level risk, not a reproduced user data-loss incident.

## Sessions and per-tab state

`Notebook::SaveLastSession`/`LoadLastSession` serve both the automatic file and exported `.kls`. A `[HikariSub ...]` header is required (the reader tests that prefix, not a schema version). `[Close session]` immediately after the header marks orderly shutdown; `CheckLastSession` returns 0 missing, 1 clean, 2 unclean. Every following `Tab:` begins a record; next Tab or EOF terminates it. [Writer/reader](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L1323), [external sessions](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L2499).

| Field | Meaning and initial fallback |
|---|---|
| Tab | Zero-based sequential tab block number/order |
| Video | Media path; empty |
| Position | Video `Tell()` position in milliseconds; 0 |
| FFMS2 | Boolean provider choice, inverse of DirectShow; true |
| Subtitles | Subtitle path, or crash-recovery output path; empty |
| Active | Zero-based active subtitle line; 0 |
| Scroll | Grid scroll position; 0 |
| Editor | Editor enabled; true |
| Audio | Separate audio path, omitted if empty or the video path; initially empty |
| Keyframes | External keyframe path, optional; empty |

These fields do not contain the undo stack, an unsaved edit buffer, full line selection, selected-tab identity, or a general dock layout. The loop resets most defaults between tabs but does not reset `audio`; a missing Audio field can inherit a prior tab's value. This should be characterized as a legacy bug, not specified as intended behavior. Missing files and platform-incompatible absolute paths need recoverable import diagnostics.

## Autosaves, recovery, and catalogs

`SubsGridBase::OnBackupTimer` writes `E/Subs/<basename>_<tabindex>_<numsave>` with `.ass` for ASS/SSA-like formats, `.srt` for SRT and `.txt` otherwise. It calls the ordinary subtitle save path with `normalSave=false`. `AUTOSAVE_MAX_FILES` defaults to 3; the counter resets only when the configured limit is greater than 1 and the threshold is reached. The 0/1 cases need fixtures rather than assuming a one-file ring. `AutoSaveOpen::GenerateList` scans nonempty files, skips DummySubs when no grouped basename can be extracted, splits names from the right-hand underscores, and groups versions by formatted modification time. It does not read or write an index manifest. `FindAutoSaveSubstitute` searches matching basename/tab files and considers modification time against the original; it can substitute an autosave and restore the original path while marking the tab dirty. Recovery files under E/Recovery are ordinary subtitles referenced by crash-session saving. [Timer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1636), [autosave list](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L137), [recovery lookup](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L1496).

Style `.sty` files are a series of ASS `Style:` lines without section headers or catalog version. Their fixed ordered fields are Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding. Colors are ASS `&HAABBGGRR`; booleans serialize as -1/0, BorderStyle as 3 (opaque box) or 1 (outline), alignment is the ASS numeric alignment. Font size/scales/spacing/angle/outline/shadow are numeric text, margins/encoding numeric fields. There is no escaping for commas in style names/fields. [Catalog loading](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L658), [style parser/defaults/writer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/styles.cpp#L249).

The default Styles object is Default/Garamond/40, white primary, black secondary, blue outline, black back; four booleans false; scales 100/100, spacing/angle 0, outline/shadow 2, alignment 2, margins 20/20/20, encoding 1. Catalog bootstrap differs: Garamond 30, red secondary, margins 10/10/10 and literal border field 0. Keep both fixtures rather than collapsing them into one invented default. Exact bootstrap record: `Style: Default,Garamond,30,&H00FFFFFF,&H000000FF,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,2,2,10,10,10,1`. [Bootstrap](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L592).

Font catalogs use `CatalogName={` followed by tab-prefixed font-family names and `}`. There is no numeric version or program header; initial state is empty. Loading trims whitespace and avoids duplicate fonts within a group. Names refer to installed families, not bundled font binaries. The management dialog rotates safety copies numbered 0, 1 and 2 with the same format and supports explicit import paths. Preserve unavailable family names rather than deleting them. [Load/save](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L567), [safety copies](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L203).

## Histories, recent files and shift profiles

`FIND_RECENT_FINDS`, `REPLACE_RECENT_REPLACEMENTS`, `FIND_IN_SUBS_FILTERS_RECENT`, `FIND_IN_SUBS_PATHS_RECENT` are brace tables read with empty-token retention and truncated to 20 entries; an empty list is seeded for UI use. They respectively hold search strings, replacement strings, filename filters and directory paths. `SUBS_RECENT_FILES`, `VIDEO_RECENT_FILES`, `AUDIO_RECENT_FILES`, `KEYFRAMES_RECENT` are most-recent-first path tables with duplicates moved and a 20-entry bound. `AUTOMATION_RECENT_FILES` despite its plural name is used as the last selected script filename string. `SELECT_LINES_RECENT_SELECTIONS` holds selection-query history. Defaults are unset/empty tables, not sample filenames. [Find history](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L44), [history saving](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1201), [recent files](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1510).

Each `SHIFT_TIMES_PROFILES` table entry is a positional string:

```text
name: Time: ms Forward: bool Frames: bool MoveTagTimes: bool MoveToStartTimes: bool MoveToVideoTime: bool MoveToVideoTime: bool WhichLines: index StylesText: value WhichTimes: index EndTimeCorrection: index
```

`WhichLines` indices are 0 all, 1 selected, 2 from selection, 3 times greater than or equal to the comparison point, 4 less than or equal, 5 chosen styles. `WhichTimes` is 0 both, 1 start, 2 end. `EndTimeCorrection` is 0 unchanged, 1 adjust overlaps, 2 new times. [Choice definitions](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L268).

The **second** `MoveToVideoTime` field actually represents the audio anchor. The reader consumes tokens positionally, so correcting that duplicate label on input is not necessary and deriving meanings by label alone is wrong. Values encode shift duration, direction, time/frame choice, tag-time shifting, anchor boundary, video/audio anchoring, line-scope selection, style selector, endpoint selection and end-time correction. There are no built-in profiles; the duration setting defaults to 2000 ms, line/endpoint indices to 0, and omitted profile fields can retain current control state. Whitespace/colon-containing names or style selectors need round-trip fixtures. [Profile serialization/parsing](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L799).

## Misspell rules and spelling dictionary

Rules.txt begins `#HikariSub rules file`, then a pipe-delimited positional list of enabled flags (1 checked, 0 unchecked), then one rule per line. A rule has four form-feed-delimited fields: description, regex, replacement, decimal option mask. Missing enabled flags are false. Options are match-case=1, replacement-lower=2, replacement-upper=4, unchanged-case=8, only-tags=16, only-text=32. Regexes are interpreted by wx's advanced regex engine; escaping/backreferences must survive import. Form feeds/newlines have no escaping layer. The exact built-in templates are in Appendix E; all start disabled, and the default enabled list has fewer entries than the rule list, which falls back to disabled for the remainder. Invalid regexes should be reported, not silently rewritten. [Load and save](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L258), [rule parser/defaults](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L701), [option mask](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.h).

`Dictionary/UserDic.udic` is the personal newline word list loaded into Hunspell; it is not the counted `.dic` distribution-file format. Bundled language `.aff`/`.dic` are dictionaries, not user preferences. Preserve Unicode words and original order/bytes during migration. [SpellChecker](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp#L49).

## ASS data, project metadata and nonstandard conventions

The standard event record is Layer, Start, End, Style, Name/Actor, MarginL, MarginR, MarginV, Effect, Text, with Dialogue/Comment prefix. New Dialogue defaults are layer 0, start 0, end 5000 ms, Default style, zero margins, empty actor/effect/text, non-comment. Text can contain commas, ASS override blocks, drawing commands and explicit line breaks; preserve it as authored. Subtitles use BOM/encoding detection above and the format-specific record grammars audited in [core inventory issue #5](https://github.com/altqx/hikari/issues/5); ASS timing is centiseconds, SRT milliseconds, MicroDVD frames, MPL2 tenths of a second and TMPlayer seconds. The following Script Info keys are the app's metadata vocabulary; arbitrary key/value Script Info is also stored by `SInfo`.

| Key(s) | Meaning/default or omission |
|---|---|
| Title | `HikariSub Ass File` |
| Original Script, Original Translation, Original Editing, Original Timing, Script Updated By | Author/credit text; empty by default |
| PlayResX, PlayResY | Script resolution; 1280, 720 |
| ScaledBorderAndShadow | yes |
| WrapStyle | 0 |
| Collisions | Normal or Reverse collision ordering, optional; UI defaults to Normal when absent |
| ScriptType | v4.00+ |
| YCbCr Matrix | TV.601 default; loader normalizes empty/None handling |
| LayoutResX, LayoutResY | Optional layout resolution; absence read as zero |
| Last Style Storage | Catalog name; Default |
| Active Line | Current zero-based line index |
| Audio File, Video File, Keyframes File | Linked resource paths; omit/empty when unavailable; same-directory paths may be shortened on ordinary save |
| Automation Scripts | Script paths; loader tokenizes using `|`, `~`, `$` delimiters, not a general escaped path format |
| TLMode | Yes or Translated, identifies translation mode/export |
| TLMode Style | Style used for the original line in a translation pair |
| TLMode Showtl | Yes when translation visibility setting is stored |

[Defaults](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1428), [ASS loading](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsLoader.cpp#L83), [Script Info writer](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L1052), [resource metadata](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1467), [automation paths](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1275).

Bookmarks use `[bookmark]` in Actor/Name; tree state uses `[tree_description]`, `[tree_opened]`, `[tree_closed]`, and visibility uses `[hidden]`/`[visible]` conventions. Translation mode stores original/translated event pairs, a generated original style, and doubtful state through form-feed plus D in the original Effect. Translated export omits TLMode metadata. These conventions collide with otherwise ordinary user text, so a new document model needs explicit recognition rules and ambiguous-input tests. [Dialogue serialization](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L826), [ASS loader](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsLoader.cpp), [save path](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp).

The parser special-cases embedded Fonts and Graphics as raw payload. Otherwise it treats many noncomment, nonsection colon records as Script Info and does not preserve the original section structure. `GetSInfos` emits these under Script Info. Therefore **unknown ASS sections, Aegisub Extradata, and Project Garbage are not guaranteed byte- or section-preserving**, even though some key/value contents may survive. `FFMS_GetSubtitleExtradata` in demux code refers to codec private data, not support for the ASS `[Aegisub Extradata]` section. A section-preserving reader and fixtures from other editors are recommended for the rewrite; compatibility must improve this lossy behavior rather than require it.

## Cache boundaries and validation work

FFMS's binary `.ffindex` uses its library reader/writer and media modification-time checks. The decoded-audio `.w64` cache is actually headerless raw PCM, written in blocks with silence/skip for delay and renamed from `.part` only on completion. Neither is an authored file format to migrate as a document. Basename-derived cache identities and truncated/foreign cache files deserve invalidation tests. [Index handling](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L271), [audio cache](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L812).

Recommended characterization corpus: current/old headers; unknown and duplicate keys; all legacy delimiters; multilingual paths and BOM/no-BOM/legacy encodings; blank table entries; every binding scope and automation ordinal; two sessions where only the first has Audio; missing resources; autosave basenames with underscores and equal mtimes; 0/1/3 retention; styles with comma-like corruption; font catalogs with missing families; rules with backreferences; translation pairs/bookmarks/trees; unknown ASS sections/embedded fonts. Import tests should prove the source tree is unchanged and importing twice does not duplicate user data. No runtime round-trip or platform installation test was performed in this research; these are the explicit follow-on gates, not missing evidence disguised as successful tests.

## Appendix A — complete persisted option registry

207 names from the CFG registry. “Unset” means no explicit assignment in either default loader; the empty-string/getter semantics above apply. Descriptions normalize the symbolic names; linked consumer expressions supply the actual interpretation, including control indices and conversions. “No direct typed getter” may mean the option is passed through a generic UI binding, not that the key is unused. All explicit default expressions are copied verbatim.

| Key / purpose | File | Explicit initial value | Typed use and consumer evidence |
|---|---|---|---|
| `AUDIO_AUTO_COMMIT` — audio auto commit | AudioConfig | `L"true"` | Bool; [AudioBox.cpp:207](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L207) `AutoCommit->SetValue(Options.GetBool(AUDIO_AUTO_COMMIT));`<br>[AudioDisplay.cpp:1753](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1753) `if (Options.GetBool(AUDIO_AUTO_COMMIT)) CommitChanges();` |
| `AUDIO_AUTO_FOCUS` — audio auto focus | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:2024](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2024) `if (Options.GetBool(AUDIO_AUTO_FOCUS) && wxWindow::FindFocus() != this) SetFocus();`<br>[OptionsDialog.cpp:705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L705) `AUDIO_DRAW_KEYFRAMES, AUDIO_LOCK_SCROLL_ON_CURSOR, AUDIO_AUTO_FOCUS, AUDIO_SNAP_TO_KEYFRAMES, AUDIO_SNAP_TO_OTHER_LINES,` |
| `AUDIO_AUTO_SCROLL` — audio auto scroll | AudioConfig | `L"true"` | Bool; [AudioBox.cpp:215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L215) `AutoScroll->SetValue(Options.GetBool(AUDIO_AUTO_SCROLL));`<br>[AudioDisplay.cpp:1193](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1193) `if (Options.GetBool(AUDIO_AUTO_SCROLL))` |
| `AUDIO_CACHE_FILES_LIMIT` — audio cache files limit | AudioConfig | `L"10"` | Int; [ProviderFFMS2.cpp:877](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L877) `size_t maxAudio = Options.GetInt(AUDIO_CACHE_FILES_LIMIT);`<br>[OptionsDialog.cpp:722](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L722) `AUDIO_LINE_BOUNDARIES_THICKNESS, AUDIO_CACHE_FILES_LIMIT, AUDIO_LEAD_IN_VALUE, AUDIO_LEAD_OUT_VALUE };` |
| `AUDIO_DELAY` — Audio offset in milliseconds before sample-frame conversion | AudioConfig | `L"0"` | Int; [ProviderFFMS2.cpp:482](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L482) `m_delayFrames = llround(m_sampleRate * (Options.GetInt(AUDIO_DELAY) / 1000.0));`<br>[OptionsDialog.cpp:721](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L721) `CONFIG opts1[7] = { AUDIO_DELAY, AUDIO_MARK_PLAY_TIME, AUDIO_INACTIVE_LINES_DISPLAY_MODE,` |
| `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES` — audio dont play when line changes | AudioConfig | `unset (empty string)` | Bool; [AudioBox.cpp:368](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L368) `bool playAudio = !Options.GetBool(AUDIO_DONT_PLAY_WHEN_LINE_CHANGES);`<br>[AudioBox.cpp:378](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L378) `bool playAudio = !Options.GetBool(AUDIO_DONT_PLAY_WHEN_LINE_CHANGES);` |
| `AUDIO_DRAW_KEYFRAMES` — audio draw keyframes | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:1513](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1513) `drawKeyframes = Options.GetBool(AUDIO_DRAW_KEYFRAMES);`<br>[OptionsDialog.cpp:705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L705) `AUDIO_DRAW_KEYFRAMES, AUDIO_LOCK_SCROLL_ON_CURSOR, AUDIO_AUTO_FOCUS, AUDIO_SNAP_TO_KEYFRAMES, AUDIO_SNAP_TO_OTHER_LINES,` |
| `AUDIO_DRAW_SECONDARY_LINES` — audio draw secondary lines | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:1512](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1512) `drawBoundaryLines = Options.GetBool(AUDIO_DRAW_SECONDARY_LINES);`<br>[OptionsDialog.cpp:704](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L704) `CONFIG opts[numOfElements] = { AUDIO_DRAW_TIME_CURSOR, AUDIO_DRAW_SECONDARY_LINES, AUDIO_DRAW_SELECTION_BACKGROUND, AUDIO_DRAW_VIDEO_POSITION,` |
| `AUDIO_DRAW_SELECTION_BACKGROUND` — audio draw selection background | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:1510](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1510) `drawSelectionBackground = Options.GetBool(AUDIO_DRAW_SELECTION_BACKGROUND);`<br>[OptionsDialog.cpp:704](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L704) `CONFIG opts[numOfElements] = { AUDIO_DRAW_TIME_CURSOR, AUDIO_DRAW_SECONDARY_LINES, AUDIO_DRAW_SELECTION_BACKGROUND, AUDIO_DRAW_VIDEO_POSITION,` |
| `AUDIO_DRAW_TIME_CURSOR` — audio draw time cursor | AudioConfig | `L"true"` | No direct typed getter; [OptionsDialog.cpp:704](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L704) `CONFIG opts[numOfElements] = { AUDIO_DRAW_TIME_CURSOR, AUDIO_DRAW_SECONDARY_LINES, AUDIO_DRAW_SELECTION_BACKGROUND, AUDIO_DRAW_VIDEO_POSITION,` |
| `AUDIO_DRAW_VIDEO_POSITION` — audio draw video position | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:1509](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1509) `drawVideoPos = Options.GetBool(AUDIO_DRAW_VIDEO_POSITION);`<br>[OptionsDialog.cpp:704](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L704) `CONFIG opts[numOfElements] = { AUDIO_DRAW_TIME_CURSOR, AUDIO_DRAW_SECONDARY_LINES, AUDIO_DRAW_SELECTION_BACKGROUND, AUDIO_DRAW_VIDEO_POSITION,` |
| `AUDIO_GRAB_TIMES_ON_SELECT` — audio grab times on select | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:1674](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1674) `if (Options.GetBool(AUDIO_GRAB_TIMES_ON_SELECT)) {` |
| `AUDIO_HORIZONTAL_ZOOM` — Horizontal zoom control value (not directly pixels or seconds) | AudioConfig | `L"50"` | Int; [AudioBox.cpp:103](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L103) `int zoom = Options.GetInt(AUDIO_HORIZONTAL_ZOOM);`<br>[AudioBox.cpp:271](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L271) `Options.SetInt(AUDIO_HORIZONTAL_ZOOM, event.GetPosition());` |
| `AUDIO_INACTIVE_LINES_DISPLAY_MODE` — audio inactive lines display mode | AudioConfig | `L"1"` | Int; [AudioDisplay.cpp:1508](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1508) `shadeType = Options.GetInt(AUDIO_INACTIVE_LINES_DISPLAY_MODE);`<br>[HikariSubFrame.cpp:2551](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L2551) `int inactiveType = Options.GetInt(AUDIO_INACTIVE_LINES_DISPLAY_MODE);` |
| `AUDIO_KARAOKE` — audio karaoke | AudioConfig | `L"false"` | Bool; [AudioDisplay.cpp:81](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L81) `hasKara = Options.GetBool(AUDIO_KARAOKE);`<br>[AudioBox.cpp:486](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L486) `Options.SetBool(AUDIO_KARAOKE, audioDisplay->hasKara);` |
| `AUDIO_KARAOKE_MOVE_ON_CLICK` — audio karaoke move on click | AudioConfig | `unset (empty string)` | Bool; [AudioDisplay.cpp:2180](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2180) `if (Options.GetBool(AUDIO_KARAOKE_MOVE_ON_CLICK) && hasSyl &&`<br>[OptionsDialog.cpp:706](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L706) `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES, AUDIO_MERGE_EVERY_N_WITH_SYLLABLE, AUDIO_KARAOKE_MOVE_ON_CLICK, AUDIO_RAM_CACHE };` |
| `AUDIO_KARAOKE_SPLIT_MODE` — audio karaoke split mode | AudioConfig | `L"true"` | Bool; [AudioDisplay.cpp:80](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L80) `karaAuto = Options.GetBool(AUDIO_KARAOKE_SPLIT_MODE);`<br>[AudioBox.cpp:500](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L500) `Options.SetBool(AUDIO_KARAOKE_SPLIT_MODE, audioDisplay->karaAuto);` |
| `AUDIO_LEAD_IN_VALUE` — audio lead in value | AudioConfig | `L"200"` | Int; [AudioDisplay.cpp:1741](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1741) `curStartMS -= Options.GetInt(AUDIO_LEAD_IN_VALUE);`<br>[OptionsDialog.cpp:722](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L722) `AUDIO_LINE_BOUNDARIES_THICKNESS, AUDIO_CACHE_FILES_LIMIT, AUDIO_LEAD_IN_VALUE, AUDIO_LEAD_OUT_VALUE };` |
| `AUDIO_LEAD_OUT_VALUE` — audio lead out value | AudioConfig | `L"300"` | Int; [AudioDisplay.cpp:1747](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1747) `curEndMS += Options.GetInt(AUDIO_LEAD_OUT_VALUE);`<br>[OptionsDialog.cpp:722](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L722) `AUDIO_LINE_BOUNDARIES_THICKNESS, AUDIO_CACHE_FILES_LIMIT, AUDIO_LEAD_IN_VALUE, AUDIO_LEAD_OUT_VALUE };` |
| `AUDIO_LINE_BOUNDARIES_THICKNESS` — audio line boundaries thickness | AudioConfig | `L"2"` | Int; [AudioDisplay.cpp:1507](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1507) `selWidth = Options.GetInt(AUDIO_LINE_BOUNDARIES_THICKNESS);`<br>[OptionsDialog.cpp:722](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L722) `AUDIO_LINE_BOUNDARIES_THICKNESS, AUDIO_CACHE_FILES_LIMIT, AUDIO_LEAD_IN_VALUE, AUDIO_LEAD_OUT_VALUE };` |
| `AUDIO_LINK` — audio link | AudioConfig | `L"false"` | Bool; [AudioBox.cpp:116](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L116) `bool link = Options.GetBool(AUDIO_LINK);`<br>[AudioBox.cpp:330](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L330) `Options.SetBool(AUDIO_LINK, VerticalLink->GetValue());` |
| `AUDIO_LOCK_SCROLL_ON_CURSOR` — audio lock scroll on cursor | AudioConfig | `L"false"` | No direct typed getter; [OptionsDialog.cpp:705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L705) `AUDIO_DRAW_KEYFRAMES, AUDIO_LOCK_SCROLL_ON_CURSOR, AUDIO_AUTO_FOCUS, AUDIO_SNAP_TO_KEYFRAMES, AUDIO_SNAP_TO_OTHER_LINES,` |
| `AUDIO_MARK_PLAY_TIME` — audio mark play time | AudioConfig | `L"1000"` | Int; [AudioBox.cpp:388](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L388) `audioDisplay->Play(start - Options.GetInt(AUDIO_MARK_PLAY_TIME), start);`<br>[AudioBox.cpp:395](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L395) `audioDisplay->Play(start, start + Options.GetInt(AUDIO_MARK_PLAY_TIME));` |
| `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE` — audio merge every n with syllable | AudioConfig | `unset (empty string)` | Bool; [KaraokeSplitting.cpp:45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/KaraokeSplitting.cpp#L45) `bool Everyn = Options.GetBool(AUDIO_MERGE_EVERY_N_WITH_SYLLABLE);`<br>[OptionsDialog.cpp:706](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L706) `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES, AUDIO_MERGE_EVERY_N_WITH_SYLLABLE, AUDIO_KARAOKE_MOVE_ON_CLICK, AUDIO_RAM_CACHE };` |
| `AUDIO_NEXT_LINE_ON_COMMIT` — audio next line on commit | AudioConfig | `L"true"` | Bool; [AudioBox.cpp:211](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L211) `NextCommit->SetValue(Options.GetBool(AUDIO_NEXT_LINE_ON_COMMIT));`<br>[AudioBox.cpp:535](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L535) `Options.SetBool(AUDIO_NEXT_LINE_ON_COMMIT, NextCommit->GetValue());` |
| `AUDIO_RAM_CACHE` — audio ram cache | AudioConfig | `L"false"` | Bool; [ProviderFFMS2.cpp:87](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L87) `m_discCache = !Options.GetBool(AUDIO_RAM_CACHE);`<br>[OptionsDialog.cpp:706](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L706) `AUDIO_DONT_PLAY_WHEN_LINE_CHANGES, AUDIO_MERGE_EVERY_N_WITH_SYLLABLE, AUDIO_KARAOKE_MOVE_ON_CLICK, AUDIO_RAM_CACHE };` |
| `AUDIO_SNAP_TO_KEYFRAMES` — audio snap to keyframes | AudioConfig | `L"false"` | Bool; [AudioDisplay.cpp:2498](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2498) `bool snapKey = Options.GetBool(AUDIO_SNAP_TO_KEYFRAMES);`<br>[OptionsDialog.cpp:705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L705) `AUDIO_DRAW_KEYFRAMES, AUDIO_LOCK_SCROLL_ON_CURSOR, AUDIO_AUTO_FOCUS, AUDIO_SNAP_TO_KEYFRAMES, AUDIO_SNAP_TO_OTHER_LINES,` |
| `AUDIO_SNAP_TO_OTHER_LINES` — audio snap to other lines | AudioConfig | `L"false"` | Bool; [AudioDisplay.cpp:2516](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2516) `bool snapLines = Options.GetBool(AUDIO_SNAP_TO_OTHER_LINES);`<br>[OptionsDialog.cpp:705](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L705) `AUDIO_DRAW_KEYFRAMES, AUDIO_LOCK_SCROLL_ON_CURSOR, AUDIO_AUTO_FOCUS, AUDIO_SNAP_TO_KEYFRAMES, AUDIO_SNAP_TO_OTHER_LINES,` |
| `AUDIO_SPECTRUM_ON` — audio spectrum on | AudioConfig | `L"false"` | Bool; [AudioBox.cpp:219](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L219) `SpectrumMode->SetValue(Options.GetBool(AUDIO_SPECTRUM_ON));`<br>[AudioDisplay.cpp:1511](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1511) `spectrumOn = Options.GetBool(AUDIO_SPECTRUM_ON);` |
| `AUDIO_SPECTRUM_NON_LINEAR_ON` — audio spectrum non linear on | AudioConfig | `unset (empty string)` | Bool; [AudioBox.cpp:223](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L223) `SpectrumNonLinear->SetValue(Options.GetBool(AUDIO_SPECTRUM_NON_LINEAR_ON));`<br>[AudioDisplay.cpp:1544](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L1544) `spectrumRenderer->SetNonLinear(Options.GetBool(AUDIO_SPECTRUM_NON_LINEAR_ON));` |
| `AUDIO_START_DRAG_SENSITIVITY` — audio start drag sensitivity | AudioConfig | `L"6"` | Int; [AudioDisplay.cpp:2328](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2328) `if (leftIsDown && abs((long)(x - lastX)) > Options.GetInt(AUDIO_START_DRAG_SENSITIVITY)) {` |
| `AUDIO_VERTICAL_ZOOM` — Vertical zoom control value | AudioConfig | `L"50"` | Int; [AudioBox.cpp:108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L108) `int pos = Options.GetInt(AUDIO_VERTICAL_ZOOM);`<br>[AudioBox.cpp:290](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L290) `Options.SetInt(AUDIO_VERTICAL_ZOOM, pos);` |
| `AUDIO_VOLUME` — Audio volume control value | AudioConfig | `L"50"` | Int; [AudioBox.cpp:114](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L114) `VolumeBar = new HikariSlider(this, Audio_Volume, Options.GetInt(AUDIO_VOLUME), 1, 100, wxDefaultPosition, wxSize(-1, 20), wxSL_VERTICAL &#124; wxSL_BOTH &#124; wxSL_INVERSE);`<br>[AudioBox.cpp:253](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L253) `float value = PlaybackVolumeFromSlider(Options.GetInt(AUDIO_VOLUME));` |
| `AUDIO_WHEEL_DEFAULT_TO_ZOOM` — audio wheel default to zoom | AudioConfig | `L"false"` | Bool; [AudioDisplay.cpp:2078](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2078) `if (Options.GetBool(AUDIO_WHEEL_DEFAULT_TO_ZOOM)) zoom = !zoom;` |
| `ACCEPTED_AUDIO_STREAM` — Saved accepted audio-stream selection policy/value | Config | `unset (empty string)` | TableFromString; [ProviderFFMS2.cpp:199](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L199) `Options.GetTableFromString(ACCEPTED_AUDIO_STREAM, enabled, L";");`<br>[VideoBox.cpp:1478](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1478) `Options.GetTableFromString(ACCEPTED_AUDIO_STREAM, enabled, L";");` |
| `AUDIO_BOX_HEIGHT` — audio box height | Config | `L"170"` | Int; [AudioBox.cpp:86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioBox.cpp#L86) `int height = Options.GetInt(AUDIO_BOX_HEIGHT);`<br>[EditBox.cpp:2148](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2148) `int audioHeight = Options.GetInt(AUDIO_BOX_HEIGHT);` |
| `ASS_PROPERTIES_TITLE` — ass properties title | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L33) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,`<br>[SubsGrid.cpp:1474](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1474) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,` |
| `ASS_PROPERTIES_SCRIPT` — ass properties script | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L33) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,`<br>[SubsGrid.cpp:1474](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1474) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,` |
| `ASS_PROPERTIES_TRANSLATION` — ass properties translation | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:33](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L33) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,`<br>[SubsGrid.cpp:1474](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1474) `CONFIG fieldValues[numFields] = { ASS_PROPERTIES_TITLE, ASS_PROPERTIES_SCRIPT, ASS_PROPERTIES_TRANSLATION,` |
| `ASS_PROPERTIES_EDITING` — ass properties editing | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L34) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };`<br>[SubsGrid.cpp:1475](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1475) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };` |
| `ASS_PROPERTIES_TIMING` — ass properties timing | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L34) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };`<br>[SubsGrid.cpp:1475](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1475) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };` |
| `ASS_PROPERTIES_UPDATE` — ass properties update | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:34](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L34) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };`<br>[SubsGrid.cpp:1475](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1475) `ASS_PROPERTIES_EDITING, ASS_PROPERTIES_TIMING, ASS_PROPERTIES_UPDATE };` |
| `ASS_PROPERTIES_TITLE_ON` — ass properties title on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L35) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,`<br>[SubsGrid.cpp:1472](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1472) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,` |
| `ASS_PROPERTIES_SCRIPT_ON` — ass properties script on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L35) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,`<br>[SubsGrid.cpp:1472](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1472) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,` |
| `ASS_PROPERTIES_TRANSLATION_ON` — ass properties translation on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L35) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,`<br>[SubsGrid.cpp:1472](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1472) `CONFIG fieldOnValues[numFields] = { ASS_PROPERTIES_TITLE_ON, ASS_PROPERTIES_SCRIPT_ON, ASS_PROPERTIES_TRANSLATION_ON,` |
| `ASS_PROPERTIES_EDITING_ON` — ass properties editing on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L36) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };`<br>[SubsGrid.cpp:1473](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1473) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };` |
| `ASS_PROPERTIES_TIMING_ON` — ass properties timing on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L36) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };`<br>[SubsGrid.cpp:1473](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1473) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };` |
| `ASS_PROPERTIES_UPDATE_ON` — ass properties update on | Config | `unset (empty string)` | No direct typed getter; [OptionsPanels.cpp:36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L36) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };`<br>[SubsGrid.cpp:1473](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1473) `ASS_PROPERTIES_EDITING_ON, ASS_PROPERTIES_TIMING_ON, ASS_PROPERTIES_UPDATE_ON };` |
| `ASS_PROPERTIES_ASK_FOR_CHANGE` — ass properties ask for change | Config | `unset (empty string)` | Bool; [OptionsPanels.cpp:56](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsPanels.cpp#L56) `option->SetValue(Options.GetBool(ASS_PROPERTIES_ASK_FOR_CHANGE));`<br>[SubsGrid.cpp:1478](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1478) `if (Options.GetBool(ASS_PROPERTIES_ASK_FOR_CHANGE)){` |
| `AUDIO_RECENT_FILES` — audio recent files | Config | `unset (empty string)` | Table; [HikariSubFrame.cpp:120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L120) `Options.GetTable(AUDIO_RECENT_FILES, audsrec);`<br>[HikariSubFrame.cpp:533](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L533) `Options.SetTable(AUDIO_RECENT_FILES, audsrec);` |
| `AUTOMATION_LOADING_METHOD` — automation loading method | Config | `unset (empty string)` | Int; [Automation.cpp:1122](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1122) `int loadMethod = Options.GetInt(AUTOMATION_LOADING_METHOD);`<br>[Automation.cpp:1350](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1350) `int loadMethod = Options.GetInt(AUTOMATION_LOADING_METHOD);` |
| `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY` — automation old scripts compatibility | Config | `unset (empty string)` | Bool; [AutomationScriptReader.cpp:41](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationScriptReader.cpp#L41) `bool compatybility = Options.GetBool(AUTOMATION_OLD_SCRIPTS_COMPATIBILITY);`<br>[OptionsDialog.cpp:317](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L317) `DONT_ASK_FOR_BAD_RESOLUTION, AUTOMATION_OLD_SCRIPTS_COMPATIBILITY };` |
| `AUTOMATION_RECENT_FILES` — Last selected automation filename (not a path table) | Config | `unset (empty string)` | String; [HikariSubFrame.cpp:902](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L902) `Options.GetString(AUTOMATION_RECENT_FILES),`<br>[HikariSubFrame.cpp:906](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L906) `Options.SetString(AUTOMATION_RECENT_FILES, HikariPathName(file));` |
| `AUTOMATION_SCRIPT_EDITOR` — automation script editor | Config | `unset (empty string)` | String; [Automation.cpp:1311](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1311) `wxString editor = Options.GetString(AUTOMATION_SCRIPT_EDITOR);`<br>[Automation.cpp:1316](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L1316) `Options.SetString(AUTOMATION_SCRIPT_EDITOR, editor);` |
| `AUTOMATION_TRACE_LEVEL` — automation trace level | Config | `L"3"` | Int, String; [AutomationProgress.cpp:60](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp#L60) `trace_level = Options.GetInt(AUTOMATION_TRACE_LEVEL);`<br>[OptionsDialog.cpp:378](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L378) `NumCtrl* ltl = new NumCtrl(EditorAdvanced, ID_NUMBER_CONTROL, Options.GetString(AUTOMATION_TRACE_LEVEL), 0, 5, true, wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);` |
| `AUTO_MOVE_TAGS_FROM_ORIGINAL` — auto move tags from original | Config | `unset (empty string)` | Bool; [EditBox.cpp:1144](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1144) `AutoMoveTags->SetValue(Options.GetBool(AUTO_MOVE_TAGS_FROM_ORIGINAL));`<br>[EditBox.cpp:1805](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1805) `Options.SetBool(AUTO_MOVE_TAGS_FROM_ORIGINAL, AutoMoveTags->GetValue());` |
| `AUTOSAVE_MAX_FILES` — autosave max files | Config | `L"3"` | Int, String; [OptionsDialog.cpp:371](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L371) `NumCtrl* autoSaveMax = new NumCtrl(EditorAdvanced, ID_NUMBER_CONTROL, Options.GetString(AUTOSAVE_MAX_FILES), 2, 1000000, true, wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);`<br>[SubsGridBase.cpp:1646](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1646) `int maxFiles = Options.GetInt(AUTOSAVE_MAX_FILES);` |
| `AUTO_SELECT_LINES_FROM_LAST_TAB` — auto select lines from last tab | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1998](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1998) `if (Options.GetBool(AUTO_SELECT_LINES_FROM_LAST_TAB)){`<br>[OptionsDialog.cpp:311](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L311) `CONFIG opts[optsSize] = { GRID_LOAD_SORTED_SUBS, SPELLCHECKER_ON, AUTO_SELECT_LINES_FROM_LAST_TAB,` |
| `CALC_SPACES_AND_PUNCTATION_FOR_WRAPS` — calc spaces and punctation for wraps | Config | `unset (empty string)` | Bool; [OptionsDialog.cpp:429](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L429) `allCharWraps->SetValue(Options.GetBool(CALC_SPACES_AND_PUNCTATION_FOR_WRAPS));`<br>[SpellChecker.cpp:352](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp#L352) `allWrapsChars = Options.GetBool(CALC_SPACES_AND_PUNCTATION_FOR_WRAPS);` |
| `CALC_SPACES_AND_PUNCTATION_FOR_CPS` — calc spaces and punctation for cps | Config | `unset (empty string)` | Bool; [OptionsDialog.cpp:432](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L432) `allCharCPS->SetValue(Options.GetBool(CALC_SPACES_AND_PUNCTATION_FOR_CPS));`<br>[SpellChecker.cpp:351](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SpellChecker.cpp#L351) `allCPSChars = Options.GetBool(CALC_SPACES_AND_PUNCTATION_FOR_CPS);` |
| `COLORPICKER_RECENT_COLORS` — Recent color string; ColorPicker owns its grammar | Config | `unset (empty string)` | String; [ColorPicker.cpp:672](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L672) `recent_box->LoadFromString(Options.GetString(COLORPICKER_RECENT_COLORS));`<br>[ColorPicker.cpp:755](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L755) `wxString recentString = Options.GetString(COLORPICKER_RECENT_COLORS);` |
| `COLORPICKER_SWITCH_CLICKS` — colorpicker switch clicks | Config | `unset (empty string)` | Bool; [ColorPicker.cpp:564](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ColorPicker.cpp#L564) `SwitchClicks->SetValue(Options.GetBool(COLORPICKER_SWITCH_CLICKS));`<br>[EditBox.cpp:865](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L865) `if (Options.GetBool(COLORPICKER_SWITCH_CLICKS))` |
| `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE` — convert ass tags to insert in line | Config | `unset (empty string)` | String; [OptionsDialog.cpp:569](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L569) `HikariTextCtrl* tc = new HikariTextCtrl(ConvOpt, -1, Options.GetString(CONVERT_ASS_TAGS_TO_INSERT_IN_LINE), wxDefaultPosition, wxSize(250, -1), wxTE_PROCESS_ENTER);`<br>[SubsGridBase.cpp:216](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L216) `const wxString & prefix = Options.GetString(CONVERT_ASS_TAGS_TO_INSERT_IN_LINE);` |
| `CONVERT_FPS` — Fallback conversion frame rate | Config | `L"23.976"` | Float, String; [OptionsDialog.cpp:529](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L529) `const wxString& convFPS = Options.GetString(CONVERT_FPS);`<br>[SubsGridBase.cpp:212](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L212) `if (Options.GetFloat(CONVERT_FPS) < 1){ HikariMessageBox(_("Invalid FPS. Correct it in options and try again.")); return; }` |
| `CONVERT_FPS_FROM_VIDEO` — convert fps from video | Config | `unset (empty string)` | Bool; [SubsGridBase.cpp:209](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L209) `if (Options.GetBool(CONVERT_FPS_FROM_VIDEO) && tab->VideoPath != emptyString){`<br>[OptionsDialog.cpp:544](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L544) `CONFIG optname = (i == 0) ? CONVERT_FPS_FROM_VIDEO : (i == 1) ? CONVERT_NEW_END_TIMES : CONVERT_SHOW_SETTINGS;` |
| `CONVERT_NEW_END_TIMES` — convert new end times | Config | `L"false"` | Bool; [SubsGridBase.cpp:214](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L214) `bool newendtimes = Options.GetBool(CONVERT_NEW_END_TIMES);`<br>[OptionsDialog.cpp:544](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L544) `CONFIG optname = (i == 0) ? CONVERT_FPS_FROM_VIDEO : (i == 1) ? CONVERT_NEW_END_TIMES : CONVERT_SHOW_SETTINGS;` |
| `CONVERT_RESOLUTION_WIDTH` — convert resolution width | Config | `L"1280"` | String; [OptionsDialog.cpp:556](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L556) `sc = new NumCtrl(ConvOpt, ID_NUMBER_CONTROL, Options.GetString(CONVERT_RESOLUTION_WIDTH), 1, 3000, true,`<br>[SubsGridBase.cpp:251](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L251) `wxString resx = Options.GetString(CONVERT_RESOLUTION_WIDTH);` |
| `CONVERT_RESOLUTION_HEIGHT` — convert resolution height | Config | `L"720"` | String; [OptionsDialog.cpp:564](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L564) `sc = new NumCtrl(ConvOpt, ID_NUMBER_CONTROL, Options.GetString(CONVERT_RESOLUTION_HEIGHT), 1, 3000, true, wxDefaultPosition, wxSize(115, -1), wxTE_PROCESS_ENTER);`<br>[SubsGridBase.cpp:252](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L252) `wxString resy = Options.GetString(CONVERT_RESOLUTION_HEIGHT);` |
| `CONVERT_SHOW_SETTINGS` — convert show settings | Config | `L"false"` | Bool; [SubsGridBase.cpp:201](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L201) `if (Options.GetBool(CONVERT_SHOW_SETTINGS)){`<br>[OptionsDialog.cpp:544](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L544) `CONFIG optname = (i == 0) ? CONVERT_FPS_FROM_VIDEO : (i == 1) ? CONVERT_NEW_END_TIMES : CONVERT_SHOW_SETTINGS;` |
| `CONVERT_STYLE` — convert style | Config | `L"Default"` | String; [OptionsDialog.cpp:491](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L491) `wxString optname = (i == 0) ? Options.GetString(CONVERT_STYLE_CATALOG) : Options.GetString(CONVERT_STYLE);`<br>[SubsDialogue.cpp:943](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L943) `Style = Options.GetString(CONVERT_STYLE);` |
| `CONVERT_STYLE_CATALOG` — convert style catalog | Config | `L"Default"` | String; [OptionsDialog.cpp:491](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L491) `wxString optname = (i == 0) ? Options.GetString(CONVERT_STYLE_CATALOG) : Options.GetString(CONVERT_STYLE);`<br>[OptionsDialog.cpp:516](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L516) `ConOpt(cmb, (i == 0) ? CONVERT_STYLE_CATALOG : CONVERT_STYLE);` |
| `CONVERT_TIME_PER_CHARACTER` — Automatic duration per character in milliseconds | Config | `L"110"` | Int, String; [OptionsDialog.cpp:550](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L550) `NumCtrl* sc = new NumCtrl(ConvOpt, ID_NUMBER_CONTROL, Options.GetString(CONVERT_TIME_PER_CHARACTER), 30, 1000, true,`<br>[SubsGridBase.cpp:215](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L215) `int endt = Options.GetInt(CONVERT_TIME_PER_CHARACTER);` |
| `COPY_COLLUMS_SELECTIONS` — copy collums selections | Config | `unset (empty string)` | Int; [SubsGrid.cpp:710](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L710) `int PasteCollumnsSelections = Options.GetInt(COPY_COLLUMS_SELECTIONS);`<br>[SubsGrid.cpp:727](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L727) `Options.SetInt(COPY_COLLUMS_SELECTIONS, cols);` |
| `DICTIONARY_LANGUAGE` — dictionary language | Config | `L"en_US"` | String; [DialogueTextEditor.cpp:2323](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2323) `const wxString &language = Options.FindLanguage(Options.GetString(DICTIONARY_LANGUAGE));`<br>[OptionsDialog.cpp:351](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L351) `dic->SetSelection(dic->FindString(Options.FindLanguage(Options.GetString(DICTIONARY_LANGUAGE))));` |
| `DISABLE_LIVE_VIDEO_EDITING` — disable live video editing | Config | `unset (empty string)` | Bool; [AudioDisplay.cpp:2844](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L2844) `if (!Options.GetBool(DISABLE_LIVE_VIDEO_EDITING)){`<br>[EditBox.cpp:343](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L343) `if (!Options.GetBool(DISABLE_LIVE_VIDEO_EDITING)){` |
| `DONT_ASK_FOR_BAD_RESOLUTION` — dont ask for bad resolution | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1400](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1400) `SetSubsResolution(!Options.GetBool(DONT_ASK_FOR_BAD_RESOLUTION));`<br>[HikariSubFrame.cpp:1862](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1862) `bool askForRes = !Options.GetBool(DONT_ASK_FOR_BAD_RESOLUTION);` |
| `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK` — editbox suggestions on double click | Config | `unset (empty string)` | Bool; [DialogueTextEditor.cpp:856](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L856) `if (Options.GetBool(EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK) && errn >= 0){`<br>[OptionsDialog.cpp:312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L312) `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK, OPEN_SUBS_IN_NEW_TAB, EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,` |
| `EDITBOX_TIMES_TO_FRAMES_SWITCH` — editbox times to frames switch | Config | `unset (empty string)` | Bool; [EditBox.cpp:221](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L221) `bool asFrames = Options.GetBool(EDITBOX_TIMES_TO_FRAMES_SWITCH);`<br>[VideoBox.cpp:411](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L411) `if (Options.GetBool(EDITBOX_TIMES_TO_FRAMES_SWITCH)){` |
| `EDITOR_ON` — editor on | Config | `L"true"` | Bool; [HikariSubFrame.cpp:427](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L427) `if (!Options.GetBool(EDITOR_ON)){ HideEditor(false); }`<br>[Notebook.cpp:176](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L176) `if (!Options.GetBool(EDITOR_ON)){` |
| `FIND_IN_SUBS_FILTERS_RECENT` — find in subs filters recent | Config | `unset (empty string)` | Table; [findreplace.cpp:54](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L54) `Options.GetTable(FIND_IN_SUBS_FILTERS_RECENT, subsFindingFilters, wxTOKEN_RET_EMPTY_ALL);`<br>[findreplace.cpp:1263](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1263) `Options.SetTable(FIND_IN_SUBS_FILTERS_RECENT, subsFindingFilters);` |
| `FIND_IN_SUBS_PATHS_RECENT` — find in subs paths recent | Config | `unset (empty string)` | Table; [findreplace.cpp:59](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L59) `Options.GetTable(FIND_IN_SUBS_PATHS_RECENT, subsFindingPaths, wxTOKEN_RET_EMPTY_ALL);`<br>[findreplace.cpp:1264](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1264) `Options.SetTable(FIND_IN_SUBS_PATHS_RECENT, subsFindingPaths);` |
| `FIND_REPLACE_STYLES` — find replace styles | Config | `unset (empty string)` | String; [FindReplaceDialog.cpp:263](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L263) `ChoosenStyleText = new HikariTextCtrl(this, ID_CHOOSEN_STYLE_TEXT, Options.GetString(FIND_REPLACE_STYLES));`<br>[FindReplaceDialog.cpp:379](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L379) `ChoosenStyleText->SetValue(Options.GetString(FIND_REPLACE_STYLES));` |
| `FIND_RECENT_FINDS` — find recent finds | Config | `unset (empty string)` | Table; [findreplace.cpp:44](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L44) `Options.GetTable(FIND_RECENT_FINDS, findRecent, wxTOKEN_RET_EMPTY_ALL);`<br>[findreplace.cpp:1201](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1201) `Options.SetTable(FIND_RECENT_FINDS, findRecent);` |
| `FIND_REPLACE_OPTIONS` — find replace options | Config | `unset (empty string)` | Int; [FindReplaceDialog.cpp:35](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L35) `int options = Options.GetInt(FIND_REPLACE_OPTIONS);`<br>[FindReplaceDialog.cpp:290](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplaceDialog.cpp#L290) `options = Options.GetInt(FIND_REPLACE_OPTIONS);` |
| `FONT_COLLECTOR_ACTION` — font collector action | Config | `unset (empty string)` | Int; [FontCollector.cpp:158](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L158) `path->Enable(Options.GetInt(FONT_COLLECTOR_ACTION) != 0);`<br>[FontCollector.cpp:160](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L160) `choosepath->Enable(Options.GetInt(FONT_COLLECTOR_ACTION) != 0);` |
| `FONT_COLLECTOR_DIRECTORY` — font collector directory | Config | `unset (empty string)` | String; [FontCollector.cpp:157](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L157) `path = new HikariTextCtrl(this, -1, Options.GetString(FONT_COLLECTOR_DIRECTORY), wxDefaultPosition, wxSize(150, -1), 0, valid);`<br>[FontCollector.cpp:430](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L430) `Options.SetString(FONT_COLLECTOR_DIRECTORY, destdir);` |
| `FONT_COLLECTOR_FROM_MKV` — font collector from mkv | Config | `unset (empty string)` | Bool; [FontCollector.cpp:183](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L183) `fromMKV->SetValue(Options.GetBool(FONT_COLLECTOR_FROM_MKV));` |
| `FONT_COLLECTOR_USE_SUBS_DIRECTORY` — font collector use subs directory | Config | `unset (empty string)` | Bool; [FontCollector.cpp:178](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L178) `subsdir->SetValue(Options.GetBool(FONT_COLLECTOR_USE_SUBS_DIRECTORY));`<br>[FontCollector.cpp:551](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L551) `Options.SetBool(FONT_COLLECTOR_USE_SUBS_DIRECTORY, subsdir->GetValue());` |
| `EXTERNAL_FONTS_DIRECTORY` — external fonts directory | Config | `unset (empty string)` | String; [FontCollector.cpp:770](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCollector.cpp#L770) `fontFolderExternal = Options.GetString(EXTERNAL_FONTS_DIRECTORY);`<br>[FontEnumerator.cpp:376](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontEnumerator.cpp#L376) `fontrealpath = Options.GetString(EXTERNAL_FONTS_DIRECTORY);` |
| `FFMS2_VIDEO_SEEKING` — FFMS seek-mode integer | Config | `L"2"` | Int; [ProviderFFMS2.cpp:340](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L340) `Options.GetInt(FFMS2_VIDEO_SEEKING),`<br>[OptionsDialog.cpp:584](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L584) `VIDEO_GPU_CONVERSION, ACCEPTED_AUDIO_STREAM, FFMS2_VIDEO_SEEKING, VSFILTER_INSTANCE, VIDEO_ZOOM_PERCENT };` |
| `GRID_CHANGE_ACTIVE_ON_SELECTION` — grid change active on selection | Config | `L"true"` | Bool; [SubsGridPreview.cpp:719](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridPreview.cpp#L719) `bool changeActive = Options.GetBool(GRID_CHANGE_ACTIVE_ON_SELECTION);`<br>[SubsGridWindow.cpp:1596](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L1596) `bool changeActive = Options.GetBool(GRID_CHANGE_ACTIVE_ON_SELECTION);` |
| `GRID_FONT` — grid font | Config | `L"Tahoma"` | String; [OptionsDialog.cpp:414](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L414) `FontPickerButton* optf = new FontPickerButton(EditorAdvanced, -1, wxFont(Options.GetInt(GRID_FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, Options.GetString(GRID_FONT)));`<br>[SubsGridWindow.cpp:91](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L91) `const wxString & fontname = Options.GetString(GRID_FONT);` |
| `GRID_FONT_SIZE` — grid font size | Config | `L"10"` | Int; [OptionsDialog.cpp:414](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L414) `FontPickerButton* optf = new FontPickerButton(EditorAdvanced, -1, wxFont(Options.GetInt(GRID_FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, Options.GetString(GRID_FONT)));`<br>[OptionsDialog.cpp:1347](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1347) `wxFont font(Options.GetInt(OB.option == PROGRAM_FONT ? PROGRAM_FONT_SIZE : GRID_FONT_SIZE),` |
| `GRID_ADD_TO_FILTER` — grid add to filter | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:230](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L230) `filterMenu->SetAccMenu(GRID_FILTER_DO_NOT_RESET, _("Do not reset previous filtering"), _("Do not reset previous filtering"), true, ITEM_CHECK)->Check(Options.GetBool(GRID_ADD_TO_FILTER));`<br>[SubsGrid.cpp:925](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L925) `Options.SetBool(GRID_ADD_TO_FILTER, !Options.GetBool(GRID_ADD_TO_FILTER));` |
| `GRID_FILTER_AFTER_LOAD` — grid filter after load | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:228](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L228) `filterMenu->SetAccMenu(GRID_FILTER_AFTER_SUBS_LOAD, _("Filter after loading subtitles"), _("Do not include selected lines"), isASS, ITEM_CHECK)->Check(Options.GetBool(GRID_FILTER_AFTER_LOAD));`<br>[SubsGrid.cpp:928](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L928) `Options.SetBool(GRID_FILTER_AFTER_LOAD, !Options.GetBool(GRID_FILTER_AFTER_LOAD));` |
| `GRID_FILTER_BY` — grid filter by | Config | `unset (empty string)` | Int; [SubsGrid.cpp:105](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L105) `int filterBy = Options.GetInt(GRID_FILTER_BY);`<br>[SubsGrid.cpp:116](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L116) `int filterBy = Options.GetInt(GRID_FILTER_BY);` |
| `GRID_FILTER_INVERTED` — grid filter inverted | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:102](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L102) `Options.SetBool(GRID_FILTER_INVERTED, !Options.GetBool(GRID_FILTER_INVERTED));`<br>[SubsGrid.cpp:102](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L102) `Options.SetBool(GRID_FILTER_INVERTED, !Options.GetBool(GRID_FILTER_INVERTED));` |
| `GRID_FILTER_STYLES` — grid filter styles | Config | `unset (empty string)` | Table; [SubsGrid.cpp:216](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L216) `Options.GetTable(GRID_FILTER_STYLES, optionsFilterStyles);`<br>[SubsGridFiltering.cpp:48](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridFiltering.cpp#L48) `Options.GetTable(GRID_FILTER_STYLES, styles);` |
| `GRID_HIDE_COLUMNS` — grid hide columns | Config | `unset (empty string)` | Int; [SubsGrid.cpp:59](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L59) `visibleColumns = Options.GetInt(GRID_HIDE_COLUMNS);`<br>[SubsGrid.cpp:98](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L98) `Options.SetInt(GRID_HIDE_COLUMNS, visibleColumns);` |
| `GRID_HIDE_TAGS` — grid hide tags | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:60](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L60) `hideOverrideTags = Options.GetBool(GRID_HIDE_TAGS);`<br>[SubsGridWindow.cpp:1962](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L1962) `Options.SetBool(GRID_HIDE_TAGS, hideOverrideTags);` |
| `GRID_IGNORE_FILTERING` — grid ignore filtering | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:86](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L86) `ignoreFiltered = Options.GetBool(GRID_IGNORE_FILTERING);`<br>[SubsGrid.cpp:931](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L931) `ignoreFiltered = !Options.GetBool(GRID_IGNORE_FILTERING);` |
| `GRID_LOAD_SORTED_SUBS` — grid load sorted subs | Config | `unset (empty string)` | Bool; [SubsGridBase.cpp:1240](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1240) `if (Options.GetBool(GRID_LOAD_SORTED_SUBS)){`<br>[OptionsDialog.cpp:311](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L311) `CONFIG opts[optsSize] = { GRID_LOAD_SORTED_SUBS, SPELLCHECKER_ON, AUTO_SELECT_LINES_FROM_LAST_TAB,` |
| `GRID_SAVE_AFTER_CHARACTER_COUNT` — Edit commit character-count threshold | Config | `L"1"` | Int, String; [EditBox.cpp:383](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L383) `if (Options.GetInt(GRID_SAVE_AFTER_CHARACTER_COUNT) > 1 && rowChanged && save) {`<br>[EditBox.cpp:1562](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1562) `int saveAfter = Options.GetInt(GRID_SAVE_AFTER_CHARACTER_COUNT);` |
| `GRID_TAGS_SWAP_CHARACTER` — grid tags swap character | Config | `L"☀"` | String; [OptionsDialog.cpp:381](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L381) `HikariTextCtrl* sc2 = new HikariTextCtrl(EditorAdvanced, ID_TAGS_SWAP_CHARACTER, Options.GetString(GRID_TAGS_SWAP_CHARACTER), wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);`<br>[SubsGridPreview.cpp:184](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridPreview.cpp#L184) `const wxString &chtag = Options.GetString(GRID_TAGS_SWAP_CHARACTER);` |
| `GRID_INSERT_END_OFFSET` — grid insert end offset | Config | `L"0"` | Int, String; [HikariSubFrame.cpp:757](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L757) `int time = tab->video->GetFrameTime(false) + Options.GetInt(GRID_INSERT_END_OFFSET);`<br>[OptionsDialog.cpp:380](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L380) `NumCtrl* sc1 = new NumCtrl(EditorAdvanced, ID_NUMBER_CONTROL, Options.GetString(GRID_INSERT_END_OFFSET), -100000, 100000, true, wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);` |
| `GRID_INSERT_START_OFFSET` — grid insert start offset | Config | `L"0"` | Int, String; [HikariSubFrame.cpp:753](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L753) `int time = tab->video->GetFrameTime() + Options.GetInt(GRID_INSERT_START_OFFSET);`<br>[OptionsDialog.cpp:379](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L379) `NumCtrl* sc = new NumCtrl(EditorAdvanced, ID_NUMBER_CONTROL, Options.GetString(GRID_INSERT_START_OFFSET), -100000, 100000, true, wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);` |
| `GRID_DUPLICATION_DONT_CHANGE_SELECTION` — grid duplication dont change selection | Config | `unset (empty string)` | Bool; [SubsGrid.cpp:404](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L404) `if (!Options.GetBool(GRID_DUPLICATION_DONT_CHANGE_SELECTION))`<br>[OptionsDialog.cpp:315](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L315) `GRID_DUPLICATION_DONT_CHANGE_SELECTION, GRID_DONT_CENTER_ACTIVE_LINE,` |
| `GRID_DONT_CENTER_ACTIVE_LINE` — grid dont center active line | Config | `unset (empty string)` | Bool; [SubsGridBase.cpp:1113](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1113) `if (Options.GetBool(GRID_DONT_CENTER_ACTIVE_LINE))`<br>[SubsGridBase.cpp:1403](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1403) `if (Options.GetBool(GRID_DONT_CENTER_ACTIVE_LINE))` |
| `KEYFRAMES_RECENT` — keyframes recent | Config | `unset (empty string)` | Table; [HikariSubFrame.cpp:121](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L121) `Options.GetTable(KEYFRAMES_RECENT, keyframesRecent);`<br>[HikariSubFrame.cpp:1530](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1530) `else{ Options.SetTable(KEYFRAMES_RECENT, recs); }` |
| `LAST_SESSION_CONFIG` — 0 none, 1 ask, 2 automatic restore | Config | `unset (empty string)` | Int; [HikariSubFrame.cpp:167](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L167) `int lastSessionConfig = Options.GetInt(LAST_SESSION_CONFIG);`<br>[hikarisubApp.cpp:507](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L507) `int session = Options.GetInt(LAST_SESSION_CONFIG);` |
| `SHIFT_TIMES_BY_TIME` — Legacy time-versus-frame shifting setting | Config | `L"false"` | No direct typed getter; No consumer outside config/converter found at this baseline. |
| `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB` — shift times change values with tab | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1994](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1994) `if (Tabs->iter != Tabs->GetOldSelection() && Options.GetBool(SHIFT_TIMES_CHANGE_VALUES_WITH_TAB)){`<br>[OptionsDialog.cpp:313](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L313) `DISABLE_LIVE_VIDEO_EDITING, GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN, SHIFT_TIMES_CHANGE_VALUES_WITH_TAB,` |
| `SHIFT_TIMES_CORRECT_END_TIMES` — shift times correct end times | Config | `unset (empty string)` | Int; [ShiftTimes.cpp:645](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L645) `EndTimeCorrection->SetSelection((secondWindow) ? secondWindow->EndTimeCorrection->GetSelection() : Options.GetInt(SHIFT_TIMES_CORRECT_END_TIMES));`<br>[SubsGridBase.cpp:438](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L438) `settings.correctEndTimes = Options.GetInt(SHIFT_TIMES_CORRECT_END_TIMES);` |
| `SHIFT_TIMES_MOVE_FORWARD` — Legacy direction setting; packed SHIFT_TIMES_OPTIONS controls current panel | Config | `L"true"` | No direct typed getter; No consumer outside config/converter found at this baseline. |
| `SHIFT_TIMES_DISPLAY_FRAMES` — Legacy frame-display setting, also consumed during profile restoration | Config | `unset (empty string)` | Int; [ShiftTimes.cpp:608](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L608) `SubsTime ct = (secondWindow) ? secondWindow->TimeText->GetTime() : SubsTime(Options.GetInt(SHIFT_TIMES_TIME), Options.GetInt(SHIFT_TIMES_DISPLAY_FRAMES));`<br>[ShiftTimes.cpp:764](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L764) `ct.orgframe = Options.GetInt(SHIFT_TIMES_DISPLAY_FRAMES);` |
| `SHIFT_TIMES_ON` — shift times on | Config | `L"true"` | Bool; [HikariSubFrame.cpp:880](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L880) `tab->shiftTimes->Show(Options.GetBool(SHIFT_TIMES_ON));`<br>[HikariSubFrame.cpp:2035](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L2035) `if (Options.GetBool(SHIFT_TIMES_ON)){` |
| `SHIFT_TIMES_OPTIONS` — shift times options | Config | `unset (empty string)` | Int; [ShiftTimes.cpp:604](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L604) `int mto = Options.GetInt(SHIFT_TIMES_OPTIONS);`<br>[SubsGridBase.cpp:433](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L433) `settings.options = Options.GetInt(SHIFT_TIMES_OPTIONS);` |
| `SHIFT_TIMES_WHICH_LINES` — shift times which lines | Config | `L"0"` | Int; [ShiftTimes.cpp:636](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L636) `int cm = (secondWindow) ? secondWindow->WhichLines->GetSelection() : Options.GetInt(SHIFT_TIMES_WHICH_LINES);`<br>[SubsGridBase.cpp:436](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L436) `settings.whichLines = Options.GetInt(SHIFT_TIMES_WHICH_LINES);` |
| `SHIFT_TIMES_WHICH_TIMES` — shift times which times | Config | `L"0"` | Int; [ShiftTimes.cpp:640](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L640) `WhichTimes->SetSelection((secondWindow) ? secondWindow->WhichTimes->GetSelection() : Options.GetInt(SHIFT_TIMES_WHICH_TIMES));`<br>[SubsGridBase.cpp:437](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L437) `settings.whichTimes = Options.GetInt(SHIFT_TIMES_WHICH_TIMES);` |
| `SHIFT_TIMES_STYLES` — shift times styles | Config | `emptyString` | String; [ShiftTimes.cpp:634](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L634) `Stylestext->SetValue((secondWindow) ? secondWindow->Stylestext->GetValue() : Options.GetString(SHIFT_TIMES_STYLES));`<br>[SubsGridBase.cpp:440](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L440) `settings.styles = Options.GetString(SHIFT_TIMES_STYLES);` |
| `SHIFT_TIMES_TIME` — Shift duration in milliseconds | Config | `L"2000"` | Int; [ShiftTimes.cpp:608](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L608) `SubsTime ct = (secondWindow) ? secondWindow->TimeText->GetTime() : SubsTime(Options.GetInt(SHIFT_TIMES_TIME), Options.GetInt(SHIFT_TIMES_DISPLAY_FRAMES));`<br>[ShiftTimes.cpp:759](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L759) `ct.mstime = Options.GetInt(SHIFT_TIMES_TIME);` |
| `MOVE_VIDEO_TO_ACTIVE_LINE` — move video to active line | Config | `unset (empty string)` | Int; [VideoToolbar.cpp:136](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L136) `videoSeekAfter->SetSelection(Options.GetInt(MOVE_VIDEO_TO_ACTIVE_LINE));`<br>[VideoToolbar.cpp:146](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L146) `Options.SetInt(MOVE_VIDEO_TO_ACTIVE_LINE, videoSeekAfter->GetSelection());` |
| `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT` — editbox dont go to next line on times edit | Config | `unset (empty string)` | Bool; [EditBox.cpp:994](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L994) `DurEdit->HasFocus()) &#124;&#124; !Options.GetBool(EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT);`<br>[OptionsDialog.cpp:312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L312) `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK, OPEN_SUBS_IN_NEW_TAB, EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,` |
| `OPEN_SUBS_IN_NEW_TAB` — open subs in new tab | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1363](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1363) `if (Options.GetBool(OPEN_SUBS_IN_NEW_TAB) && tab->SubsPath != emptyString &&`<br>[OptionsDialog.cpp:312](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L312) `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK, OPEN_SUBS_IN_NEW_TAB, EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT,` |
| `OPEN_VIDEO_AT_ACTIVE_LINE` — open video at active line | Config | `unset (empty string)` | Bool; [VideoBox.cpp:408](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L408) `tab->SubsPath != emptyString && Options.GetBool(OPEN_VIDEO_AT_ACTIVE_LINE)){`<br>[OptionsDialog.cpp:583](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L583) `CONFIG vopts[] = { VIDEO_FULL_SCREEN_ON_START, VIDEO_PAUSE_ON_CLICK, OPEN_VIDEO_AT_ACTIVE_LINE,` |
| `PASTE_COLUMNS_SELECTION` — paste columns selection | Config | `unset (empty string)` | Int; [SubsGrid.cpp:547](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L547) `int PasteCollumnsSelections = Options.GetInt(PASTE_COLUMNS_SELECTION);`<br>[SubsGrid.cpp:563](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L563) `Options.SetInt(PASTE_COLUMNS_SELECTION, collumns);` |
| `VIDEO_PLAY_AFTER_SELECTION` — video play after selection | Config | `L"0"` | Int; [VideoToolbar.cpp:140](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L140) `videoPlayAfter->SetSelection(Options.GetInt(VIDEO_PLAY_AFTER_SELECTION));`<br>[VideoToolbar.cpp:150](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoToolbar.cpp#L150) `Options.SetInt(VIDEO_PLAY_AFTER_SELECTION, videoPlayAfter->GetSelection());` |
| `POSTPROCESSOR_ON` — postprocessor on | Config | `unset (empty string)` | Int; [ShiftTimes.cpp:123](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L123) `CreateControls(Options.GetInt(POSTPROCESSOR_ON) < 16);`<br>[ShiftTimes.cpp:390](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L390) `int pe = Options.GetInt(POSTPROCESSOR_ON);` |
| `POSTPROCESSOR_KEYFRAME_BEFORE_START` — postprocessor keyframe before start | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:440](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L440) `BeforeStart = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_KEYFRAME_BEFORE_START), 0, 1000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:659](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L659) `BeforeStart->SetInt((secondWindow) ? secondWindow->BeforeStart->GetInt() : Options.GetInt(POSTPROCESSOR_KEYFRAME_BEFORE_START));` |
| `POSTPROCESSOR_KEYFRAME_AFTER_START` — postprocessor keyframe after start | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:441](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L441) `AfterStart = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_KEYFRAME_AFTER_START), 0, 1000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:660](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L660) `AfterStart->SetInt((secondWindow) ? secondWindow->AfterStart->GetInt() : Options.GetInt(POSTPROCESSOR_KEYFRAME_AFTER_START));` |
| `POSTPROCESSOR_KEYFRAME_BEFORE_END` — postprocessor keyframe before end | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:442](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L442) `BeforeEnd = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_KEYFRAME_BEFORE_END), 0, 1000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:661](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L661) `BeforeEnd->SetInt((secondWindow) ? secondWindow->BeforeEnd->GetInt() : Options.GetInt(POSTPROCESSOR_KEYFRAME_BEFORE_END));` |
| `POSTPROCESSOR_KEYFRAME_AFTER_END` — postprocessor keyframe after end | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:443](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L443) `AfterEnd = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_KEYFRAME_AFTER_END), 0, 1000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:662](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L662) `AfterEnd->SetInt((secondWindow) ? secondWindow->AfterEnd->GetInt() : Options.GetInt(POSTPROCESSOR_KEYFRAME_AFTER_END));` |
| `POSTPROCESSOR_LEAD_IN` — postprocessor lead in | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:398](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L398) `LITime = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_LEAD_IN), -10000, 10000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:655](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L655) `LITime->SetInt((secondWindow) ? secondWindow->LITime->GetInt() : Options.GetInt(POSTPROCESSOR_LEAD_IN));` |
| `POSTPROCESSOR_LEAD_OUT` — postprocessor lead out | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:401](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L401) `LOTime = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_LEAD_OUT), -10000, 10000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:656](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L656) `LOTime->SetInt((secondWindow) ? secondWindow->LOTime->GetInt() : Options.GetInt(POSTPROCESSOR_LEAD_OUT));` |
| `POSTPROCESSOR_THRESHOLD_START` — postprocessor threshold start | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:419](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L419) `ThresStart = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_THRESHOLD_START), 0, 10000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:657](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L657) `ThresStart->SetInt((secondWindow) ? secondWindow->ThresStart->GetInt() : Options.GetInt(POSTPROCESSOR_THRESHOLD_START));` |
| `POSTPROCESSOR_THRESHOLD_END` — postprocessor threshold end | Config | `unset (empty string)` | Int, String; [ShiftTimes.cpp:420](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L420) `ThresEnd = new NumCtrl(panel, -1, Options.GetString(POSTPROCESSOR_THRESHOLD_END), 0, 10000, true, wxDefaultPosition, wxSize(40, -1), SCROLL_ON_FOCUS);`<br>[ShiftTimes.cpp:658](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L658) `ThresEnd->SetInt((secondWindow) ? secondWindow->ThresEnd->GetInt() : Options.GetInt(POSTPROCESSOR_THRESHOLD_END));` |
| `STYLE_PREVIEW_TEXT` — style preview text | Config | `L"Podgląd"` | String; [FontCatalogList.cpp:233](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L233) `const wxString& previewText = Options.GetString(STYLE_PREVIEW_TEXT);`<br>[ListControls.cpp:952](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ListControls.cpp#L952) `previewText = Options.GetString(STYLE_PREVIEW_TEXT);` |
| `PROGRAM_FONT` — program font | Config | `L"Tahoma"` | String; [OptionsDialog.cpp:421](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L421) `FontPickerButton* programFont = new FontPickerButton(EditorAdvanced, -1, wxFont(Options.GetInt(PROGRAM_FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, Options.GetString(PROGRAM_FONT)));`<br>[OptionsDialog.cpp:1347](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L1347) `wxFont font(Options.GetInt(OB.option == PROGRAM_FONT ? PROGRAM_FONT_SIZE : GRID_FONT_SIZE),` |
| `PROGRAM_FONT_SIZE` — program font size | Config | `L"10"` | Int; [AudioDisplay.cpp:108](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AudioDisplay.cpp#L108) `int fontSize = Options.GetInt(PROGRAM_FONT_SIZE);`<br>[OptionsDialog.cpp:421](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L421) `FontPickerButton* programFont = new FontPickerButton(EditorAdvanced, -1, wxFont(Options.GetInt(PROGRAM_FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, Options.GetString(PROGRAM_FONT)));` |
| `PROGRAM_LANGUAGE` — UI language selection | Config | `unset (empty string)` | String; [OptionsDialog.cpp:336](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L336) `int sel = programLanguage->FindString(Options.FindLanguage(Options.GetString(PROGRAM_LANGUAGE)));`<br>[hikarisubApp.cpp:328](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L328) `wxString lang = Options.GetString(PROGRAM_LANGUAGE);` |
| `PROGRAM_THEME` — program theme | Config | `L"DarkSentro"` | String; [OptionsDialog.cpp:840](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L840) `const wxString & programTheme = Options.GetString(PROGRAM_THEME);`<br>[OptionsDialog.cpp:906](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L906) `Options.SetString(PROGRAM_THEME, themeName);` |
| `REPLACE_RECENT_REPLACEMENTS` — replace recent replacements | Config | `unset (empty string)` | Table; [findreplace.cpp:49](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L49) `Options.GetTable(REPLACE_RECENT_REPLACEMENTS, replaceRecent, wxTOKEN_RET_EMPTY_ALL);`<br>[findreplace.cpp:1224](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/findreplace.cpp#L1224) `Options.SetTable(REPLACE_RECENT_REPLACEMENTS, replaceRecent);` |
| `SELECT_LINES_RECENT_SELECTIONS` — select lines recent selections | Config | `unset (empty string)` | Table; [SelectLines.cpp:36](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L36) `Options.GetTable(SELECT_LINES_RECENT_SELECTIONS, selsRecent, wxTOKEN_RET_EMPTY_ALL);`<br>[SelectLines.cpp:450](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L450) `Options.SetTable(SELECT_LINES_RECENT_SELECTIONS, selsRecent);` |
| `SELECT_LINES_OPTIONS` — select lines options | Config | `unset (empty string)` | Int; [SelectLines.cpp:37](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L37) `int options = Options.GetInt(SELECT_LINES_OPTIONS);`<br>[SelectLines.cpp:206](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.cpp#L206) `Options.SetInt(SELECT_LINES_OPTIONS, options);` |
| `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN` — grid set visible line after full screen | Config | `unset (empty string)` | Bool; [VideoBox.cpp:556](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L556) `if (!m_IsFullscreen && tab->SubsPath != emptyString && Options.GetBool(GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN)){`<br>[VideoBox.cpp:641](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L641) `if (tab->SubsPath != emptyString && Options.GetBool(GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN)){` |
| `SHIFT_TIMES_PROFILES` — shift times profiles | Config | `unset (empty string)` | Table; [ShiftTimes.cpp:772](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L772) `Options.GetTable(SHIFT_TIMES_PROFILES, fullProfiles, wxTOKEN_STRTOK);`<br>[ShiftTimes.cpp:785](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ShiftTimes.cpp#L785) `Options.GetTable(SHIFT_TIMES_PROFILES, profiles);` |
| `SPELLCHECKER_ON` — spellchecker on | Config | `L"true"` | Bool; [DialogueTextEditor.cpp:42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L42) `SpellCheckerOnOff = (_spell)? Options.GetBool(SPELLCHECKER_ON) : false;`<br>[DialogueTextEditor.cpp:2326](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2326) `nullptr, nullptr, ITEM_CHECK_AND_HIDE)->Check(Options.GetBool(SPELLCHECKER_ON));` |
| `STYLE_EDIT_FILTER_TEXT` — style edit filter text | Config | `L"ĄĆĘŁŃÓŚŹŻąćęłńóśźż"` | String; [FontCatalogList.cpp:116](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontCatalogList.cpp#L116) `wxString fontFilterText = Options.GetString(STYLE_EDIT_FILTER_TEXT);`<br>[FontDialog.cpp:717](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L717) `wxString fontFilterText = Options.GetString(STYLE_EDIT_FILTER_TEXT);` |
| `STYLE_EDIT_FILTER_TEXT_ON` — style edit filter text on | Config | `unset (empty string)` | Bool; [FontDialog.cpp:431](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FontDialog.cpp#L431) `bool fontFilterOn = Options.GetBool(STYLE_EDIT_FILTER_TEXT_ON);`<br>[StyleChange.cpp:75](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/StyleChange.cpp#L75) `bool fontFilterOn = Options.GetBool(STYLE_EDIT_FILTER_TEXT_ON);` |
| `STYLE_MANAGER_POSITION` — style manager position | Config | `unset (empty string)` | Coords; [hikarisubApp.cpp:392](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L392) `Options.GetCoords(STYLE_MANAGER_POSITION, &sposx, &sposy);`<br>[stylestore.cpp:979](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L979) `Options.GetCoords(STYLE_MANAGER_POSITION, &ww, &hh);` |
| `STYLE_MANAGER_DETACH_EDIT_WINDOW` — style manager detach edit window | Config | `unset (empty string)` | Bool; [stylestore.cpp:45](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L45) `bool isDetached = detachedEtit = Options.GetBool(STYLE_MANAGER_DETACH_EDIT_WINDOW);`<br>[stylestore.cpp:925](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/stylestore.cpp#L925) `bool detach = Options.GetBool(STYLE_MANAGER_DETACH_EDIT_WINDOW);` |
| `SUBS_AUTONAMING` — subs autonaming | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:194](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L194) `PTR_BITMAP_PNG(L"SAVEWITHVIDEONAME"), nullptr, ITEM_CHECK)->Check(Options.GetBool(SUBS_AUTONAMING));`<br>[HikariSubFrame.cpp:1265](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1265) `&#124;&#124; (Options.GetBool(SUBS_AUTONAMING)` |
| `SUBS_COMPARISON_TYPE` — subs comparison type | Config | `unset (empty string)` | Int; [Notebook.cpp:82](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L82) `int compareBy = Options.GetInt(SUBS_COMPARISON_TYPE);`<br>[Notebook.cpp:930](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L930) `int compareBy = Options.GetInt(SUBS_COMPARISON_TYPE);` |
| `SUBS_COMPARISON_STYLES` — subs comparison styles | Config | `unset (empty string)` | Table; [Notebook.cpp:921](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L921) `Options.GetTable(SUBS_COMPARISON_STYLES, optionsCompareStyles);`<br>[Notebook.cpp:112](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L112) `Options.SetTable(SUBS_COMPARISON_STYLES, SubsGrid::compareStyles);` |
| `SUBS_RECENT_FILES` — subs recent files | Config | `unset (empty string)` | Table; [HikariSubFrame.cpp:118](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L118) `Options.GetTable(SUBS_RECENT_FILES, subsrec);`<br>[HikariSubFrame.cpp:531](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L531) `Options.SetTable(SUBS_RECENT_FILES, subsrec);` |
| `TAB_TEXT_MAX_CHARS` — tab text max chars | Config | `unset (empty string)` | Int; [Notebook.cpp:53](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L53) `int maxTextChars = Options.GetInt(TAB_TEXT_MAX_CHARS);`<br>[OptionsDialog.cpp:373](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L373) `int numMaxChars = Options.GetInt(TAB_TEXT_MAX_CHARS);` |
| `TEXT_EDITOR_FONT_SIZE` — text editor font size | Config | `unset (empty string)` | Int; [DialogueTextEditor.cpp:106](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L106) `fontSize = Options.GetInt(TEXT_EDITOR_FONT_SIZE);`<br>[DialogueTextEditor.cpp:2931](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2931) `int fontSize = Options.GetInt(TEXT_EDITOR_FONT_SIZE);` |
| `TEXT_EDITOR_HIDE_STATUS_BAR` — text editor hide status bar | Config | `unset (empty string)` | Bool; [DialogueTextEditor.cpp:119](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L119) `statusBarHeight = (Options.GetBool(TEXT_EDITOR_HIDE_STATUS_BAR)) ? 0 : fontHeight + 8;`<br>[DialogueTextEditor.cpp:1022](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L1022) `statusBarHeight = (Options.GetBool(TEXT_EDITOR_HIDE_STATUS_BAR)) ? 0 : fontHeight + 8;` |
| `TEXT_EDITOR_CHANGE_QUOTES` — text editor change quotes | Config | `unset (empty string)` | Bool; [DialogueTextEditor.cpp:120](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L120) `changeQuotes = Options.GetBool(TEXT_EDITOR_CHANGE_QUOTES);`<br>[DialogueTextEditor.cpp:2342](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L2342) `nullptr, emptyString, ITEM_CHECK)->Check(Options.GetBool(TEXT_EDITOR_CHANGE_QUOTES));` |
| `TEXT_EDITOR_TAG_LIST_OPTIONS` — text editor tag list options | Config | `unset (empty string)` | Int; [TextEditorTagList.cpp:131](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.cpp#L131) `int options = Options.GetInt(TEXT_EDITOR_TAG_LIST_OPTIONS);`<br>[TextEditorTagList.cpp:250](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TextEditorTagList.cpp#L250) `int options = Options.GetInt(TEXT_EDITOR_TAG_LIST_OPTIONS);` |
| `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS` — text field allow numpad hotkeys | Config | `unset (empty string)` | Bool; [DialogueTextEditor.cpp:76](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/DialogueTextEditor.cpp#L76) `bool setNumpadAccels = !Options.GetBool(TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS);`<br>[HikariTextCtrl.cpp:92](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariTextCtrl.cpp#L92) `bool setNumpadAccels = !Options.GetBool(TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS);` |
| `TL_MODE_SHOW_ORIGINAL` — tl mode show original | Config | `unset (empty string)` | Bool; [SubsGridBase.cpp:997](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L997) `showOriginal = (file->GetSInfo(L"TLMode Showtl") == L"Yes" &#124;&#124; (hasTLMode && Options.GetBool(TL_MODE_SHOW_ORIGINAL) != 0));`<br>[SubsGridBase.cpp:1214](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1214) `if (hasTLMode && (file->GetSInfo(L"TLMode Showtl") == L"Yes" &#124;&#124; Options.GetBool(TL_MODE_SHOW_ORIGINAL))){` |
| `TL_MODE_HIDE_ORIGINAL_ON_VIDEO` — tl mode hide original on video | Config | `unset (empty string)` | Bool; [SubsGridBase.cpp:313](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L313) `bool showOriginalOnVideo = !Options.GetBool(TL_MODE_HIDE_ORIGINAL_ON_VIDEO);`<br>[SubsGridBase.cpp:1520](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1520) `bool showOriginalOnVideo = !Options.GetBool(TL_MODE_HIDE_ORIGINAL_ON_VIDEO);` |
| `TOOLBAR_IDS` — Ordered toolbar action-ID table | Config | `unset (empty string)` | Table; [Toolbar.cpp:66](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L66) `Options.GetTable(TOOLBAR_IDS, stringIDs);`<br>[Toolbar.cpp:58](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L58) `Options.SetTable(TOOLBAR_IDS, names);` |
| `TOOLBAR_ALIGNMENT` — toolbar alignment | Config | `unset (empty string)` | Int; [HikariSubFrame.cpp:1664](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1664) `int toolbarAlignment = Options.GetInt(TOOLBAR_ALIGNMENT);`<br>[Toolbar.cpp:42](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Toolbar.cpp#L42) `alignment = Options.GetInt(TOOLBAR_ALIGNMENT);` |
| `UPDATER_AUTO_CHECK` — updater auto check | Config | `unset (empty string)` | Bool; [UpdateChecker.cpp:110](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L110) `autoCheck->SetValue(Options.GetBool(UPDATER_AUTO_CHECK));`<br>[UpdateChecker.cpp:224](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L224) `if (!Options.GetBool(UPDATER_AUTO_CHECK))` |
| `UPDATER_CHECK_FOR_STABLE` — updater check for stable | Config | `L"true"` | Bool; [UpdateChecker.cpp:114](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L114) `stableOnly->SetValue(Options.GetBool(UPDATER_CHECK_FOR_STABLE));`<br>[UpdateChecker.cpp:190](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L190) `Options.GetBool(UPDATER_CHECK_FOR_STABLE), &release))` |
| `UPDATER_NEXT_CHECK` — updater next check | Config | `unset (empty string)` | Int; [UpdateChecker.cpp:226](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L226) `if (time(nullptr) < (time_t)Options.GetInt(UPDATER_NEXT_CHECK))`<br>[UpdateChecker.cpp:134](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/UpdateChecker.cpp#L134) `Options.SetInt(UPDATER_NEXT_CHECK, (int)(time(nullptr) + kWeek));` |
| `VIDEO_FULL_SCREEN_ON_START` — video full screen on start | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1857](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1857) `OpenFile(files[0], (videos.size() == 1 && Options.GetBool(VIDEO_FULL_SCREEN_ON_START)));`<br>[hikarisubApp.cpp:485](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L485) `if (Options.GetBool(VIDEO_FULL_SCREEN_ON_START)){` |
| `VIDEO_GPU_CONVERSION` — video gpu conversion | Config | `L"true"` | Bool; [ProviderFFMS2.cpp:419](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L419) `if (m_renderer && Options.GetBool(VIDEO_GPU_CONVERSION) && m_CS != FFMS_CS_RGB &&`<br>[OptionsDialog.cpp:584](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L584) `VIDEO_GPU_CONVERSION, ACCEPTED_AUDIO_STREAM, FFMS2_VIDEO_SEEKING, VSFILTER_INSTANCE, VIDEO_ZOOM_PERCENT };` |
| `VIDEO_INDEX` — video index | Config | `L"true"` | Bool; [HikariSubFrame.cpp:285](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L285) `bool videoIndex = Options.GetBool(VIDEO_INDEX);`<br>[HikariSubFrame.cpp:775](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L775) `CONFIG conf = (id == GLOBAL_VIDEO_INDEXING) ? VIDEO_INDEX : SUBS_AUTONAMING;` |
| `VIDEO_PAUSE_ON_CLICK` — video pause on click | Config | `unset (empty string)` | Bool; [VideoBox.cpp:617](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L617) `if (Options.GetBool(VIDEO_PAUSE_ON_CLICK) && event.LeftUp() && !event.ControlDown()){`<br>[OptionsDialog.cpp:583](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L583) `CONFIG vopts[] = { VIDEO_FULL_SCREEN_ON_START, VIDEO_PAUSE_ON_CLICK, OPEN_VIDEO_AT_ACTIVE_LINE,` |
| `VIDEO_PROGRESS_BAR` — video progress bar | Config | `L"true"` | Bool; [RendererVideo.cpp:221](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/RendererVideo.cpp#L221) `videoControl->m_FullScreenProgressBar = Options.GetBool(VIDEO_PROGRESS_BAR);`<br>[VideoBox.cpp:1067](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L1067) `bool pb = !Options.GetBool(VIDEO_PROGRESS_BAR);` |
| `VIDEO_RECENT_FILES` — video recent files | Config | `unset (empty string)` | Table; [HikariSubFrame.cpp:119](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L119) `Options.GetTable(VIDEO_RECENT_FILES, videorec);`<br>[HikariSubFrame.cpp:532](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L532) `Options.SetTable(VIDEO_RECENT_FILES, videorec);` |
| `VIDEO_VOLUME` — video volume | Config | `unset (empty string)` | Int; [VideoBox.cpp:167](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L167) `m_VolumeSlider = new VolSlider(m_VideoPanel, ID_VOL, Options.GetInt(VIDEO_VOLUME), wxPoint(size.x - 110, m_ToolBarHeight - 5), wxSize(110, 25));`<br>[VideoBox.cpp:389](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VideoBox.cpp#L389) `int pos = Options.GetInt(VIDEO_VOLUME);` |
| `VIDEO_WINDOW_SIZE` — video window size | Config | `L"500,350"` | Coords; [HikariFrame.cpp:497](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L497) `Options.GetCoords(VIDEO_WINDOW_SIZE, &vsizex, &vsizey);`<br>[HikariSubFrame.cpp:883](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L883) `Options.GetCoords(VIDEO_WINDOW_SIZE, &x, &y);` |
| `VIDEO_VISUAL_WARNINGS_OFF` — video visual warnings off | Config | `unset (empty string)` | Bool; [Visuals.cpp:481](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Visuals.cpp#L481) `if (Options.GetBool(VIDEO_VISUAL_WARNINGS_OFF))`<br>[Visuals.cpp:533](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Visuals.cpp#L533) `if (Options.GetBool(VIDEO_VISUAL_WARNINGS_OFF))` |
| `VIDEO_ZOOM_PERCENT` — video zoom percent | Config | `unset (empty string)` | Int; [RendererVideo.cpp:745](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/RendererVideo.cpp#L745) `float npercent = Options.GetInt(VIDEO_ZOOM_PERCENT) / 100.f;`<br>[OptionsDialog.cpp:584](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OptionsDialog.cpp#L584) `VIDEO_GPU_CONVERSION, ACCEPTED_AUDIO_STREAM, FFMS2_VIDEO_SEEKING, VSFILTER_INSTANCE, VIDEO_ZOOM_PERCENT };` |
| `VSFILTER_INSTANCE` — Selected CSRI/VSFilter renderer instance | Config | `unset (empty string)` | String; [SubtitlesProviderManager.cpp:31](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesProviderManager.cpp#L31) `wxString provider = Options.GetString(VSFILTER_INSTANCE);`<br>[SubtitlesProviderManager.cpp:136](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubtitlesProviderManager.cpp#L136) `wxString provider = Options.GetString(VSFILTER_INSTANCE);` |
| `WINDOW_MAXIMIZED` — window maximized | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:424](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L424) `bool im = Options.GetBool(WINDOW_MAXIMIZED);`<br>[HikariSubFrame.cpp:425](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L425) `if (im){ Maximize(Options.GetBool(WINDOW_MAXIMIZED)); }` |
| `WINDOW_POSITION` — window position | Config | `unset (empty string)` | Coords; [HikariFrame.cpp:317](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L317) `Options.GetCoords(WINDOW_POSITION, &lastPosition.x, &lastPosition.y);`<br>[hikarisubApp.cpp:389](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L389) `Options.GetCoords(WINDOW_POSITION, &posx, &posy);` |
| `WINDOW_SIZE` — window size | Config | `L"1000,700"` | Coords; [HikariFrame.cpp:341](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L341) `Options.GetCoords(WINDOW_SIZE, &lastSize.x, &lastSize.y);`<br>[hikarisubApp.cpp:390](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L390) `Options.GetCoords(WINDOW_SIZE, &sizex, &sizey);` |
| `MONITOR_POSITION` — monitor position | Config | `unset (empty string)` | Coords; [HikariFrame.cpp:437](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L437) `Options.GetCoords(MONITOR_POSITION, &oldRt.x, &oldRt.y);`<br>[hikarisubApp.cpp:413](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L413) `Options.GetCoords(MONITOR_POSITION, &mposx, &mposy);` |
| `MONITOR_SIZE` — monitor size | Config | `unset (empty string)` | Coords; [HikariFrame.cpp:436](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariFrame.cpp#L436) `Options.GetCoords(MONITOR_SIZE, &oldRt.width, &oldRt.height);`<br>[hikarisubApp.cpp:391](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/hikarisubApp.cpp#L391) `Options.GetCoords(MONITOR_SIZE, &msizex, &msizey);` |
| `DONT_SHOW_CRASH_INFO` — dont show crash info | Config | `unset (empty string)` | No direct typed getter; No consumer outside config/converter found at this baseline. |
| `LINK_RESOLUTIONS` — link resolutions | Config | `unset (empty string)` | Bool; [HikariSubFrame.cpp:1236](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L1236) `bool link = Options.GetBool(LINK_RESOLUTIONS);`<br>[ScriptInfo.cpp:92](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ScriptInfo.cpp#L92) `bool linkRes = Options.GetBool(LINK_RESOLUTIONS);` |
| `EDITBOX_TAG_BUTTONS` — Number of custom tag buttons, 0 through 20 | Config | `unset (empty string)` | Int, String; [EditBox.cpp:1749](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1749) `numTagButtons = new NumCtrl(this, -1, Options.GetString(EDITBOX_TAG_BUTTONS), 0, 20, true);`<br>[EditBox.cpp:2014](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2014) `int numTagButtons = Options.GetInt(EDITBOX_TAG_BUTTONS);` |
| `EDITBOX_TAG_BUTTON_VALUE1` — Custom tag button 1 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE2` — Custom tag button 2 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE3` — Custom tag button 3 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE4` — Custom tag button 4 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE5` — Custom tag button 5 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE6` — Custom tag button 6 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE7` — Custom tag button 7 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE8` — Custom tag button 8 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE9` — Custom tag button 9 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE10` — Custom tag button 10 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE11` — Custom tag button 11 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE12` — Custom tag button 12 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE13` — Custom tag button 13 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE14` — Custom tag button 14 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE15` — Custom tag button 15 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE16` — Custom tag button 16 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE17` — Custom tag button 17 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE18` — Custom tag button 18 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE19` — Custom tag button 19 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |
| `EDITBOX_TAG_BUTTON_VALUE20` — Custom tag button 20 table (tag, insertion choice, name); indexed relative to slot 1 | Config | `unset (empty string)` | No direct typed getter; [EditBox.cpp:1650](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L1650) `Options.GetTable((CONFIG)(event.GetId() - EDITBOX_TAG_BUTTON1 + EDITBOX_TAG_BUTTON_VALUE1), tagOptions);`<br>[EditBox.cpp:2020](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/EditBox.cpp#L2020) `Options.GetTable((CONFIG)(i + EDITBOX_TAG_BUTTON_VALUE1), tagOption, wxTOKEN_RET_EMPTY_ALL);` |

## Appendix B — complete theme color registry

139 color fields. Names identify the widget/visual role; expressions retain the exact dark/light defaults and any alpha handling. These are source defaults, not a proposed Qt token palette. [Default source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/config.cpp#L393).

| Role/key | Default expression (`dark ? dark-value : light-value`) |
|---|---|
| `WINDOW_BACKGROUND` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `WINDOW_BACKGROUND_INACTIVE` | `(dark) ? L"#4E5155" : L"#C3C3C3"` |
| `WINDOW_TEXT` | `(dark) ? L"#AEAFB2" : L"#000000"` |
| `WINDOW_TEXT_INACTIVE` | `(dark) ? L"#7D7F83" : L"#0E0082"` |
| `WINDOW_BORDER` | `(dark) ? L"#2F3136" : L"#BFBFBF"` |
| `WINDOW_BORDER_INACTIVE` | `(dark) ? L"#4E5155" : L"#C3C3C3"` |
| `WINDOW_BORDER_BACKGROUND` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `WINDOW_BORDER_BACKGROUND_INACTIVE` | `(dark) ? L"#4E5155" : L"#AEAEAE"` |
| `WINDOW_HEADER_TEXT` | `(dark) ? L"#AEAFB2" : L"#000000"` |
| `WINDOW_HEADER_TEXT_INACTIVE` | `(dark) ? L"#7D7F83" : L"#242424"` |
| `WINDOW_HOVER_HEADER_ELEMENT` | `(dark) ? L"#5D636D" : L"#BFBFBF"` |
| `WINDOW_PUSHED_HEADER_ELEMENT` | `(dark) ? L"#788291" : L"#EEEEEE"` |
| `WINDOW_HOVER_CLOSE_BUTTON` | `(dark) ? L"#A22525" : L"#925B1F"` |
| `WINDOW_PUSHED_CLOSE_BUTTON` | `(dark) ? L"#E24443" : L"#FF6968"` |
| `WINDOW_WARNING_ELEMENTS` | `(dark) ? L"#8791FD" : L"#1A5325"` |
| `GRID_TEXT` | `(dark) ? L"#FFFFFF" : L"#000000"` |
| `GRID_BACKGROUND` | `(dark) ? L"#50565F" : L"#EEEEEE"` |
| `GRID_DIALOGUE` | `(dark) ? L"#50565F" : L"#EEEEEE"` |
| `GRID_COMMENT` | `(dark) ? L"#4880D0" : L"#4880D0"` |
| `GRID_SELECTION` | `(dark) ? wxColour(0x87, 0x91, 0xFD, 75) : wxColour(0x0E, 0x00, 0x82, 75)` |
| `GRID_LINE_VISIBLE_ON_VIDEO` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `GRID_COLLISIONS` | `(dark) ? L"#F1FF00" : L"#3600AA"` |
| `GRID_LINES` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `GRID_ACTIVE_LINE` | `(dark) ? L"#8791FD" : L"#000000"` |
| `GRID_HEADER` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `GRID_HEADER_TEXT` | `(dark) ? L"#8791FD" : L"#000000"` |
| `GRID_LABEL_NORMAL` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `GRID_LABEL_MODIFIED` | `(dark) ? L"#322F4E" : L"#B0ADD8"` |
| `GRID_LABEL_SAVED` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `GRID_LABEL_DOUBTFUL` | `(dark) ? L"#925B1F" : L"#925B1F"` |
| `GRID_SPELLCHECKER` | `(dark) ? L"#940000" : L"#FF6968"` |
| `GRID_COMPARISON_OUTLINE` | `(dark) ? L"#2700FF" : L"#FFFFFF"` |
| `GRID_COMPARISON_BACKGROUND_NOT_MATCH` | `(dark) ? L"#272B32" : L"#FF000C"` |
| `GRID_COMPARISON_BACKGROUND_MATCH` | `(dark) ? L"#3A3E45" : L"#B7AC00"` |
| `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH` | `(dark) ? L"#003176" : L"#9C0000"` |
| `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH` | `(dark) ? L"#3662A1" : L"#817900"` |
| `EDITOR_TEXT` | `(dark) ? L"#F4F4F4" : L"#000000"` |
| `EDITOR_TAG_NAMES` | `(dark) ? L"#00C3FF" : L"#787600"` |
| `EDITOR_TAG_VALUES` | `(dark) ? L"#0076FF" : L"#34C200"` |
| `EDITOR_CURLY_BRACES` | `(dark) ? L"#8791FD" : L"#7E3B00"` |
| `EDITOR_TAG_OPERATORS` | `(dark) ? L"#00C3FF" : L"#787600"` |
| `EDITOR_SPLIT_LINES_AND_DRAWINGS` | `(dark) ? L"#ADABAB" : L"#817F7F"` |
| `EDITOR_TEMPLATE_VARIABLES` | `(dark) ? L"#BDB76B" : L"#BDB76B"` |
| `EDITOR_TEMPLATE_CODE_MARKS` | `(dark) ? L"#FB4544" : L"#FB4544"` |
| `EDITOR_TEMPLATE_FUNCTIONS` | `(dark) ? L"#00C3FF" : L"#00C3FF"` |
| `EDITOR_TEMPLATE_KEYWORDS` | `(dark) ? L"#8D80D3" : L"#8D80D3"` |
| `EDITOR_TEMPLATE_STRINGS` | `(dark) ? L"#11A243" : L"#11A243"` |
| `EDITOR_PHRASE_SEARCH` | `(dark) ? L"#0B5808" : L"#0B5808"` |
| `EDITOR_BRACES_BACKGROUND` | `(dark) ? L"#F4F4F4" : L"#F4F4F4"` |
| `EDITOR_BACKGROUND` | `(dark) ? L"#50565F" : L"#EEEEEE"` |
| `EDITOR_SELECTION` | `(dark) ? L"#646D8A" : L"#BBC0FB"` |
| `EDITOR_SELECTION_NO_FOCUS` | `(dark) ? L"#646D8A" : L"#BBC0FB"` |
| `EDITOR_BORDER` | `(dark) ? L"#36393E" : L"#EEEEEE"` |
| `EDITOR_BORDER_ON_FOCUS` | `(dark) ? L"#8791FD" : L"#000000"` |
| `EDITOR_SPELLCHECKER` | `(dark) ? L"#940000" : L"#FF6968"` |
| `AUDIO_BACKGROUND` | `(dark) ? L"#36393E" : L"#36393E"` |
| `AUDIO_LINE_BOUNDARY_START` | `(dark) ? L"#940000" : L"#940000"` |
| `AUDIO_LINE_BOUNDARY_END` | `(dark) ? L"#940000" : L"#940000"` |
| `AUDIO_LINE_BOUNDARY_MARK` | `L"#FFFFFF"` |
| `AUDIO_LINE_BOUNDARY_INACTIVE_LINE` | `L"#00D77D"` |
| `AUDIO_PLAY_CURSOR` | `L"#8791FD"` |
| `AUDIO_SECONDS_BOUNDARIES` | `0xF4, 0xF4, 0xF4, 0x37` |
| `AUDIO_KEYFRAMES` | `L"#F4F4F4"` |
| `AUDIO_SYLLABLE_BOUNDARIES` | `L"#202225"` |
| `AUDIO_SYLLABLE_TEXT` | `L"#8791FD"` |
| `AUDIO_SELECTION_BACKGROUND` | `0xFF, 0xFF, 0xFF, 0x37` |
| `AUDIO_SELECTION_BACKGROUND_MODIFIED` | `0xFF, 0xFF, 0xFF, 0x37` |
| `AUDIO_INACTIVE_LINES_BACKGROUND` | `0x00, 0x00, 0x00, 0x55` |
| `AUDIO_WAVEFORM` | `L"#202225"` |
| `AUDIO_WAVEFORM_INACTIVE` | `L"#2F3136"` |
| `AUDIO_WAVEFORM_MODIFIED` | `L"#FF6968"` |
| `AUDIO_WAVEFORM_SELECTED` | `L"#DCDAFF"` |
| `AUDIO_SPECTRUM_BACKGROUND` | `L"#000000"` |
| `AUDIO_SPECTRUM_ECHO` | `L"#674FD7"` |
| `AUDIO_SPECTRUM_INNER` | `L"#F4F4F4"` |
| `TEXT_FIELD_BACKGROUND` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `TEXT_FIELD_BORDER` | `(dark) ? L"#36393E" : L"#7F7F7F"` |
| `TEXT_FIELD_BORDER_ON_FOCUS` | `(dark) ? L"#8791FD" : L"#000000"` |
| `TEXT_FIELD_SELECTION` | `(dark) ? L"#646D8A" : L"#BBDEFB"` |
| `TEXT_FIELD_SELECTION_NO_FOCUS` | `(dark) ? L"#383E4F" : L"#93B2CC"` |
| `BUTTON_BACKGROUND` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `BUTTON_BACKGROUND_HOVER` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `BUTTON_BACKGROUND_PUSHED` | `(dark) ? L"#4F535B" : L"#EFEFEF"` |
| `BUTTON_BACKGROUND_ON_FOCUS` | `(dark) ? L"#4F535B" : L"#EFEFEF"` |
| `BUTTON_BORDER` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `BUTTON_BORDER_HOVER` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `BUTTON_BORDER_PUSHED` | `(dark) ? L"#4F535B" : L"#EFEFEF"` |
| `BUTTON_BORDER_ON_FOCUS` | `(dark) ? L"#4F535B" : L"#EFEFEF"` |
| `BUTTON_BORDER_INACTIVE` | `(dark) ? L"#4F535B" : L"#A0A0A0"` |
| `TOGGLE_BUTTON_BACKGROUND_TOGGLED` | `(dark) ? L"#5C5889" : L"#656565"` |
| `TOGGLE_BUTTON_BORDER_TOGGLED` | `(dark) ? L"#5C5889" : L"#656565"` |
| `SCROLLBAR_BACKGROUND` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `SCROLLBAR_THUMB` | `(dark) ? L"#484B51" : L"#ACACAC"` |
| `SCROLLBAR_THUMB_HOVER` | `(dark) ? L"#8982D5" : L"#C5C5C5"` |
| `SCROLLBAR_THUMB_PUSHED` | `(dark) ? L"#9E96FF" : L"#DDDDDD"` |
| `STATICBOX_BORDER` | `(dark) ? L"#36393E" : L"#A0A0A0"` |
| `STATICLIST_BORDER` | `(dark) ? L"#36393E" : L"#EEEEEE"` |
| `STATICLIST_BACKGROUND` | `(dark) ? L"#36393E" : L"#EEEEEE"` |
| `STATICLIST_SELECTION` | `(dark) ? L"#4F535B" : L"#BFBFBF"` |
| `STATICLIST_BACKGROUND_HEADLINE` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `STATICLIST_TEXT_HEADLINE` | `(dark) ? L"#AEAFB2" : L"#000000"` |
| `STATUSBAR_BORDER` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `MENUBAR_BACKGROUND1` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `MENUBAR_BACKGROUND2` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `MENUBAR_BORDER_SELECTION` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `MENUBAR_BACKGROUND_HOVER` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `MENUBAR_BACKGROUND_SELECTION` | `(dark) ? L"#575478" : L"#BBDEFB"` |
| `MENUBAR_BACKGROUND` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `MENU_BORDER_SELECTION` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `MENU_BACKGROUND_SELECTION` | `(dark) ? L"#53516B" : L"#DEDEDE"` |
| `TABSBAR_BACKGROUND1` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `TABSBAR_BACKGROUND2` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `TABS_BORDER_ACTIVE` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `TABS_BORDER_INACTIVE` | `(dark) ? L"#202225" : L"#A0A0A0"` |
| `TABS_BACKGROUND_ACTIVE` | `(dark) ? L"#53516B" : L"#BFBFBF"` |
| `TABS_BACKGROUND_INACTIVE` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `TABS_BACKGROUND_INACTIVE_HOVER` | `(dark) ? L"#53516B" : L"#BFBFBF"` |
| `TABS_BACKGROUND_SECOND_WINDOW` | `(dark) ? L"#383654" : L"#BFBFBF"` |
| `TABS_TEXT_ACTIVE` | `(dark) ? L"#FFFFFF" : L"#000000"` |
| `TABS_TEXT_INACTIVE` | `(dark) ? L"#7D7F83" : L"#EEEEEE"` |
| `TABS_CLOSE_HOVER` | `(dark) ? L"#A22525" : L"#925B1F"` |
| `TABSBAR_ARROW` | `(dark) ? L"#202225" : L"#BFBFBF"` |
| `TABSBAR_ARROW_BACKGROUND` | `(dark) ? L"#2F3136" : L"#A0A0A0"` |
| `TABSBAR_ARROW_BACKGROUND_HOVER` | `(dark) ? L"#76777B" : L"#797979"` |
| `SLIDER_PATH_BACKGROUND` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `SLIDER_PATH_BORDER` | `(dark) ? L"#2F3136" : L"#7F7F7F"` |
| `SLIDER_BORDER` | `(dark) ? L"#5C5889" : L"#000000"` |
| `SLIDER_BORDER_HOVER` | `(dark) ? L"#8982D5" : L"#000000"` |
| `SLIDER_BORDER_PUSHED` | `(dark) ? L"#9E96FF" : L"#000000"` |
| `SLIDER_BACKGROUND` | `(dark) ? L"#5C5889" : L"#ACACAC"` |
| `SLIDER_BACKGROUND_HOVER` | `(dark) ? L"#8982D5" : L"#C5C5C5"` |
| `SLIDER_BACKGROUND_PUSHED` | `(dark) ? L"#9E96FF" : L"#DDDDDD"` |
| `WINDOW_RESIZER_DOTS` | `(dark) ? L"#2F3136" : L"#FFFFFF"` |
| `FIND_RESULT_FILENAME_FOREGROUND` | `(dark) ? L"#BB0099" : L"#5D67CF"` |
| `FIND_RESULT_FILENAME_BACKGROUND` | `(dark) ? L"#440033" : L"#000000"` |
| `FIND_RESULT_FOUND_PHRASE_FOREGROUND` | `(dark) ? L"#FFFFFF" : L"#FFFFFF"` |
| `FIND_RESULT_FOUND_PHRASE_BACKGROUND` | `(dark) ? L"#8791FD" : L"#5D67CF"` |
| `STYLE_PREVIEW_COLOR1` | `L"#434343"` |
| `STYLE_PREVIEW_COLOR2` | `L"#626262"` |

## Appendix C — action IDs and explicit default bindings

243 symbolic actions. IDs are legacy identifiers, not a recommended future schema. Scope/default is absent when LoadDefault does not bind an action. [Registry](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.h#L30), [human names](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HotkeysNaming.cpp#L45), [bindings](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp#L134).

| Action | Numeric ID | Meaning | Explicit default scope/chord |
|---|---|---|---|
| `AUDIO_COMMIT_ALT` | 1000 | Commit alt | AUDIO `G` |
| `AUDIO_PLAY_ALT` | 1001 | Play alt | AUDIO `S` |
| `AUDIO_PLAY_LINE_ALT` | 1002 | Play line alt | AUDIO `R` |
| `AUDIO_PREVIOUS_ALT` | 1003 | Previous line alt | AUDIO `Z` |
| `AUDIO_NEXT_ALT` | 1004 | Next line alt | AUDIO `X` |
| `AUDIO_COMMIT` | 1010 | Commit | AUDIO `Enter` |
| `AUDIO_PLAY` | 1011 | Play | AUDIO `Down` |
| `AUDIO_PLAY_LINE` | 1012 | Play line | AUDIO `Up` |
| `AUDIO_PREVIOUS` | 1013 | Previous line | AUDIO `Left` |
| `AUDIO_NEXT` | 1014 | Next line | AUDIO `Right` |
| `AUDIO_STOP` | 1015 | Stop | AUDIO `H` |
| `AUDIO_PLAY_BEFORE_MARK` | 1016 | Play before the marker | AUDIO `Num 0` |
| `AUDIO_PLAY_AFTER_MARK` | 1017 | Play after the marker | AUDIO `Num .` |
| `AUDIO_PLAY_500MS_BEFORE` | 1018 | Play 500ms before | AUDIO `Q` |
| `AUDIO_PLAY_500MS_AFTER` | 1019 | Play 500ms after | AUDIO `W` |
| `AUDIO_PLAY_500MS_FIRST` | 1020 | Play first 500ms | AUDIO `E` |
| `AUDIO_PLAY_500MS_LAST` | 1021 | Play last 500ms | AUDIO `D` |
| `AUDIO_PLAY_TO_END` | 1022 | Play to the end | AUDIO `T` |
| `AUDIO_SCROLL_LEFT` | 1023 | Scroll right | AUDIO `F` |
| `AUDIO_SCROLL_RIGHT` | 1024 | Scroll left | AUDIO `A` |
| `AUDIO_GOTO` | 1025 | Go to selection | AUDIO `B` |
| `AUDIO_LEAD_IN` | 1026 | Add lead-in | AUDIO `C` |
| `AUDIO_LEAD_OUT` | 1027 | Add lead-out | AUDIO `V` |
| `VIDEO_PLAY_PAUSE` | 2000 | Play / Pause | VIDEO `Space` |
| `VIDEO_STOP` | 2001 | Stop | Unbound |
| `VIDEO_5_SECONDS_FORWARD` | 2002 | 5 seconds forward | VIDEO `L` |
| `VIDEO_5_SECONDS_BACKWARD` | 2003 | 5 seconds backward | VIDEO `;` |
| `VIDEO_MINUTE_BACKWARD` | 2004 | 1 minute backward | VIDEO `Down` |
| `VIDEO_MINUTE_FORWARD` | 2005 | 1 minute forward | VIDEO `Up` |
| `VIDEO_VOLUME_PLUS` | 2006 | Volume up | VIDEO `Num .` |
| `VIDEO_VOLUME_MINUS` | 2007 | Volume down | VIDEO `Num 0` |
| `VIDEO_PREVIOUS_FILE` | 2008 | Previous file | VIDEO `,` |
| `VIDEO_NEXT_FILE` | 2009 | Next file | VIDEO `.` |
| `VIDEO_PREVIOUS_CHAPTER` | 2010 | Previous chapter | VIDEO `N` |
| `VIDEO_NEXT_CHAPTER` | 2011 | Next chapter | VIDEO `M` |
| `VIDEO_FULL_SCREEN` | 2012 | Full screen | Unbound |
| `VIDEO_HIDE_PROGRESS_BAR` | 2013 | Show / hide progress bar | Unbound |
| `VIDEO_DELETE_FILE` | 2014 | Remove video | Unbound |
| `VIDEO_ASPECT_RATIO` | 2015 | Change aspect ratio | Unbound |
| `VIDEO_COPY_COORDS` | 2016 | video copy coords | Unbound |
| `VIDEO_SAVE_FRAME_TO_PNG` | 2017 | Save frame as PNG | Unbound |
| `VIDEO_COPY_FRAME_TO_CLIPBOARD` | 2018 | Copy frame to clipboard | Unbound |
| `VIDEO_SAVE_SUBBED_FRAME_TO_PNG` | 2019 | Save frame with subtitles as PNG | Unbound |
| `VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD` | 2020 | Copy frame with subtitles to clipboard | Unbound |
| `EDITBOX_CHANGE_FONT` | 3000 | Font selection | Unbound |
| `EDITBOX_CHANGE_UNDERLINE` | 3001 | Underline | Unbound |
| `EDITBOX_CHANGE_STRIKEOUT` | 3002 | Strikethrough | Unbound |
| `EDITBOX_PASTE_ALL_TO_TRANSLATION` | 3003 | Paste all | Unbound |
| `EDITBOX_PASTE_SELECTION_TO_TRANSLATION` | 3004 | Paste the selected | Unbound |
| `EDITBOX_HIDE_ORIGINAL` | 3005 | Hide original | Unbound |
| `EDITBOX_CHANGE_COLOR_PRIMARY` | 3006 | Primary color | Unbound |
| `EDITBOX_CHANGE_COLOR_SECONDARY` | 3007 | Secondary color for karaoke | Unbound |
| `EDITBOX_CHANGE_COLOR_OUTLINE` | 3008 | Border color | Unbound |
| `EDITBOX_CHANGE_COLOR_SHADOW` | 3009 | Shadow color | Unbound |
| `EDITBOX_COMMIT` | 3010 | Apply changes | EDITBOX `Ctrl-Enter` |
| `EDITBOX_COMMIT_GO_NEXT_LINE` | 3011 | Apply the changes and go to the next line | EDITBOX `Enter` |
| `EDITBOX_INSERT_BOLD` | 3012 | Add bold | EDITBOX `Ctrl-B` |
| `EDITBOX_INSERT_ITALIC` | 3013 | Add italic | EDITBOX `Ctrl-I` |
| `EDITBOX_SPLIT_LINE` | 3014 | Add line wrap | EDITBOX `Shift-Enter` |
| `EDITBOX_START_DIFFERENCE` | 3015 | Insert difference from the start | EDITBOX `Ctrl-,` |
| `EDITBOX_END_DIFFERENCE` | 3016 | Insert difference to the end | EDITBOX `Ctrl-.` |
| `EDITBOX_FIND_NEXT_DOUBTFUL` | 3017 | Next unconfirmed line | EDITBOX `Ctrl-D` |
| `EDITBOX_FIND_NEXT_UNTRANSLATED` | 3018 | Next untranslated line | EDITBOX `Ctrl-R` |
| `EDITBOX_SET_DOUBTFUL` | 3019 | Mark as unconfirmed and go to the next line | EDITBOX `Alt-Down` |
| `EDITBOX_TAG_BUTTON1` | 3100 | First tag button | Unbound |
| `EDITBOX_TAG_BUTTON2` | 3101 | Second tag button | Unbound |
| `EDITBOX_TAG_BUTTON3` | 3102 | Third tag button | Unbound |
| `EDITBOX_TAG_BUTTON4` | 3103 | 4th tag button | Unbound |
| `EDITBOX_TAG_BUTTON5` | 3104 | 5th tag button | Unbound |
| `EDITBOX_TAG_BUTTON6` | 3105 | 6th tag button | Unbound |
| `EDITBOX_TAG_BUTTON7` | 3106 | 7th tag button | Unbound |
| `EDITBOX_TAG_BUTTON8` | 3107 | 8th tag button | Unbound |
| `EDITBOX_TAG_BUTTON9` | 3108 | 9th tag button | Unbound |
| `EDITBOX_TAG_BUTTON10` | 3109 | 10th tag button | Unbound |
| `EDITBOX_TAG_BUTTON11` | 3110 | 11th tag button | Unbound |
| `EDITBOX_TAG_BUTTON12` | 3111 | 12th tag button | Unbound |
| `EDITBOX_TAG_BUTTON13` | 3112 | 13th tag button | Unbound |
| `EDITBOX_TAG_BUTTON14` | 3113 | 14th tag button | Unbound |
| `EDITBOX_TAG_BUTTON15` | 3114 | 15th tag button | Unbound |
| `EDITBOX_TAG_BUTTON16` | 3115 | 16th tag button | Unbound |
| `EDITBOX_TAG_BUTTON17` | 3116 | 17th tag button | Unbound |
| `EDITBOX_TAG_BUTTON18` | 3117 | 18th tag button | Unbound |
| `EDITBOX_TAG_BUTTON19` | 3118 | 19th tag button | Unbound |
| `EDITBOX_TAG_BUTTON20` | 3119 | 20th tag button | Unbound |
| `GRID_HIDE_LAYER` | 4001 | Hide layer | Unbound |
| `GRID_HIDE_START` | 4002 | Hide start time | Unbound |
| `GRID_HIDE_END` | 4004 | Hide end time | Unbound |
| `GRID_HIDE_ACTOR` | 4016 | Hide actor | Unbound |
| `GRID_HIDE_STYLE` | 4008 | Hide style | Unbound |
| `GRID_HIDE_MARGINL` | 4032 | Hide left margin | Unbound |
| `GRID_HIDE_MARGINR` | 4064 | Hide right margin | Unbound |
| `GRID_HIDE_MARGINV` | 4128 | Hide vertical margin | Unbound |
| `GRID_HIDE_EFFECT` | 4256 | Hide effect | Unbound |
| `GRID_HIDE_CPS` | 4512 | Hide characters per second | Unbound |
| `GRID_HIDE_WRAPS` | 4513 | grid hide wraps | Unbound |
| `GRID_INSERT_BEFORE` | 4514 | Insert before | Unbound |
| `GRID_INSERT_AFTER` | 4515 | Insert after | Unbound |
| `GRID_INSERT_BEFORE_VIDEO` | 4516 | Insert before with video time | Unbound |
| `GRID_INSERT_AFTER_VIDEO` | 4517 | Insert after with video time | Unbound |
| `GRID_INSERT_BEFORE_WITH_VIDEO_FRAME` | 4518 | Insert before with video frame time | Unbound |
| `GRID_INSERT_AFTER_WITH_VIDEO_FRAME` | 4519 | Insert after with video frame time | Unbound |
| `GRID_SELECT_VISIBLE_LINES` | 4520 | Select all lines visible on video | Unbound |
| `GRID_SPLIT_BY_VIDEO_TIME` | 4521 | Split line at video time | Unbound |
| `GRID_SPLIT_BY_FRAME` | 4522 | Split lines into frames | Unbound |
| `GRID_SPLIT_BY_CHARS` | 4523 | Split lines into characters | Unbound |
| `GRID_SPLIT_BY_WORDS` | 4524 | Split lines into words | Unbound |
| `GRID_SPLIT_BY_WRAPS` | 4525 | Split lines by wraps | Unbound |
| `GRID_SWAP_LINES` | 4526 | Swap lines | Unbound |
| `GRID_DUPLICATE_LINES` | 4527 | Duplicate lines | GRID `Ctrl-D` |
| `GRID_JOIN_LINES` | 4528 | Join lines | Unbound |
| `GRID_JOIN_TO_FIRST_LINE` | 4529 | Join lines and keep first | Unbound |
| `GRID_JOIN_TO_LAST_LINE` | 4530 | Join lines and keep last | Unbound |
| `GRID_COPY` | 4531 | grid copy | Unbound |
| `GRID_PASTE` | 4532 | grid paste | Unbound |
| `GRID_CUT` | 4533 | grid cut | Unbound |
| `GRID_SHOW_PREVIEW` | 4534 | Show subtitles preview | GRID `Ctrl-Q` |
| `GRID_HIDE_SELECTED` | 4535 | Hide selected lines | Unbound |
| `GRID_FILTER_BY_NOTHING` | 4536 | Turn off filtering | Unbound |
| `GRID_FILTER_BY_STYLES` | 4537 | Hide lines with styles | Unbound |
| `GRID_FILTER_BY_SELECTIONS` | 4538 | Hide selected lines | Unbound |
| `GRID_FILTER_BY_DIALOGUES` | 4539 | Hide comments | Unbound |
| `GRID_FILTER_BY_DOUBTFUL` | 4540 | Show unconfirmed | Unbound |
| `GRID_FILTER_BY_UNTRANSLATED` | 4541 | Show untranslated | Unbound |
| `GRID_FILTER` | 4542 | Filter | Unbound |
| `GRID_FILTER_AFTER_SUBS_LOAD` | 4543 | Filter after loading subtitles | Unbound |
| `GRID_FILTER_INVERT` | 4544 | Reverse filtering | Unbound |
| `GRID_FILTER_DO_NOT_RESET` | 4545 | Do not reset previous filtering | Unbound |
| `GRID_FILTER_IGNORE_IN_ACTIONS` | 4546 | Ignore filtering in some actions | Unbound |
| `GRID_TREE_MAKE` | 4547 | Make tree | Unbound |
| `GRID_PASTE_TRANSLATION` | 4548 | Paste translation text | Unbound |
| `GRID_TRANSLATION_DIALOG` | 4549 | Dialogue shifting window | Unbound |
| `GRID_SUBS_FROM_MKV` | 4550 | Load subtitles from an MKV file | Unbound |
| `GRID_MAKE_CONTINOUS_PREVIOUS_LINE` | 4551 | Set times as a continuous (previous line) | Unbound |
| `GRID_MAKE_CONTINOUS_NEXT_LINE` | 4552 | Set times as a continuous (next line) | Unbound |
| `GRID_PASTE_COLUMNS` | 4553 | Paste columns | GRID `Ctrl-Shift-V` |
| `GRID_COPY_COLUMNS` | 4554 | Copy columns | GRID `Ctrl-Shift-C` |
| `GRID_SET_FPS_FROM_VIDEO` | 4555 | Set FPS from video | Unbound |
| `GRID_SET_NEW_FPS` | 4556 | Set new FPS | Unbound |
| `GLOBAL_SAVE_SUBS` | 5000 | Save | GLOBAL `Ctrl-S` |
| `GLOBAL_SAVE_ALL_SUBS` | 5001 | Save all subtitles | Unbound |
| `GLOBAL_SAVE_SUBS_AS` | 5002 | Save as... | GLOBAL `Ctrl-Shift-S` |
| `GLOBAL_SAVE_TRANSLATION` | 5003 | Save translation | Unbound |
| `GLOBAL_REMOVE_SUBS` | 5004 | Remove subtitles from the editor | Unbound |
| `GLOBAL_REDO` | 5005 | Redo | GLOBAL `Ctrl-Y` |
| `GLOBAL_UNDO` | 5006 | Undo | GLOBAL `Ctrl-Z` |
| `GLOBAL_UNDO_TO_LAST_SAVE` | 5007 | Undo to last save | Unbound |
| `GLOBAL_HISTORY` | 5008 | History | GLOBAL `Ctrl-Shift-H` |
| `GLOBAL_SEARCH` | 5009 | Find | GLOBAL `Ctrl-F` |
| `GLOBAL_FIND_REPLACE` | 5010 | Find and replace | GLOBAL `Ctrl-H` |
| `GLOBAL_FIND_NEXT` | 5011 | Find next | GLOBAL `F3` |
| `GLOBAL_MISSPELLS_REPLACER` | 5012 | Fix minor errors (experimental) | Unbound |
| `GLOBAL_OPEN_SELECT_LINES` | 5013 | Select lines | Unbound |
| `GLOBAL_OPEN_SPELLCHECKER` | 5014 | Check spelling | Unbound |
| `GLOBAL_VIDEO_INDEXING` | 5015 | Open video with FFMS2 | Unbound |
| `GLOBAL_SAVE_WITH_VIDEO_NAME` | 5016 | Save subtitles using the video name | Unbound |
| `GLOBAL_OPEN_AUDIO` | 5017 | Open audio | Unbound |
| `GLOBAL_AUDIO_FROM_VIDEO` | 5018 | Open audio from video | Unbound |
| `GLOBAL_CLOSE_AUDIO` | 5019 | Close audio | Unbound |
| `GLOBAL_CONVERT_TO_ASS` | 5020 | Convert to ASS | GLOBAL `F9` |
| `GLOBAL_CONVERT_TO_SRT` | 5021 | Convert to SRT | GLOBAL `F8` |
| `GLOBAL_CONVERT_TO_TMP` | 5022 | Convert to TMP | GLOBAL `Ctrl-F12` |
| `GLOBAL_CONVERT_TO_MDVD` | 5023 | Convert to MDVD | GLOBAL `F10` |
| `GLOBAL_CONVERT_TO_MPL2` | 5024 | Convert to MPL2 | GLOBAL `F11` |
| `GLOBAL_OPEN_ASS_PROPERTIES` | 5025 | ASS file properties | Unbound |
| `GLOBAL_OPEN_STYLE_MANAGER` | 5026 | Style manager | GLOBAL `Ctrl-M` |
| `GLOBAL_OPEN_SUBS_RESAMPLE` | 5027 | Resample subtitles | Unbound |
| `GLOBAL_OPEN_FONT_COLLECTOR` | 5028 | Font collector | Unbound |
| `GLOBAL_HIDE_TAGS` | 5029 | Hide tags | Unbound |
| `GLOBAL_SHOW_SHIFT_TIMES` | 5030 | Time shift window | GLOBAL `Ctrl-I` |
| `GLOBAL_VIEW_ALL` | 5031 | View all | Unbound |
| `GLOBAL_VIEW_AUDIO` | 5032 | View audio and subtitles | Unbound |
| `GLOBAL_VIEW_VIDEO` | 5033 | View video and subtitles | Unbound |
| `GLOBAL_VIEW_ONLY_VIDEO` | 5034 | View only video | Unbound |
| `GLOBAL_VIEW_SUBS` | 5035 | View only subtitles | Unbound |
| `GLOBAL_AUTOMATION_LOAD_SCRIPT` | 5036 | Load script | Unbound |
| `GLOBAL_AUTOMATION_RELOAD_AUTOLOAD` | 5037 | Refresh autoload scripts | Unbound |
| `GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT` | 5038 | Run the last loaded script | Unbound |
| `GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW` | 5039 | Open shortcut mapping window | Unbound |
| `GLOBAL_PLAY_PAUSE` | 5040 | Play / Pause | GLOBAL `Alt-Space` |
| `GLOBAL_PREVIOUS_FRAME` | 5041 | Previous frame | GLOBAL `Left` |
| `GLOBAL_NEXT_FRAME` | 5042 | Next frame | GLOBAL `Right` |
| `GLOBAL_VIDEO_ZOOM` | 5043 | Zoom video | Unbound |
| `GLOBAL_RESET_VIDEO_ZOOM` | 5044 | Turn off video zoom | Unbound |
| `GLOBAL_SET_START_TIME` | 5045 | Insert start time from video | GLOBAL `Ctrl-Left` |
| `GLOBAL_SET_END_TIME` | 5046 | Insert end time from video | GLOBAL `Ctrl-Right` |
| `GLOBAL_SET_VIDEO_AT_START_TIME` | 5047 | Go to start time | Unbound |
| `GLOBAL_SET_VIDEO_AT_END_TIME` | 5048 | Go to end time of line | Unbound |
| `GLOBAL_GO_TO_NEXT_KEYFRAME` | 5049 | Go to next keyframe | Unbound |
| `GLOBAL_GO_TO_PREVIOUS_KEYFRAME` | 5050 | Go to previous keyframe | Unbound |
| `GLOBAL_SET_AUDIO_FROM_VIDEO` | 5051 | Set audio position to video time | Unbound |
| `GLOBAL_SET_AUDIO_MARK_FROM_VIDEO` | 5052 | Set audio marker to video time | Unbound |
| `GLOBAL_LOAD_EXTERNAL_SESSION` | 5053 | global load external session | Unbound |
| `GLOBAL_SAVE_EXTERNAL_SESSION` | 5054 | global save external session | Unbound |
| `GLOBAL_LOAD_LAST_SESSION` | 5055 | Load last session | Unbound |
| `GLOBAL_OPEN_SUBS` | 5100 | Open subtitles | GLOBAL `Ctrl-O` |
| `GLOBAL_OPEN_VIDEO` | 5101 | Open video | GLOBAL `Ctrl-Shift-O` |
| `GLOBAL_OPEN_KEYFRAMES` | 5102 | Open keyframes | Unbound |
| `GLOBAL_OPEN_DUMMY_VIDEO` | 5103 | global open dummy video | Unbound |
| `GLOBAL_OPEN_DUMMY_AUDIO` | 5104 | global open dummy audio | Unbound |
| `GLOBAL_OPEN_AUTO_SAVE` | 5105 | Open auto save | Unbound |
| `GLOBAL_DELETE_TEMPORARY_FILES` | 5106 | global delete temporary files | Unbound |
| `GLOBAL_SETTINGS` | 5107 | Settings | Unbound |
| `GLOBAL_QUIT` | 5108 | global quit | GLOBAL `Alt-F4` |
| `GLOBAL_EDITOR` | 5109 | Enable / Disable editor | GLOBAL `Ctrl-E` |
| `GLOBAL_ABOUT` | 5110 | About | Unbound |
| `GLOBAL_HELPERS` | 5111 | Credits | Unbound |
| `GLOBAL_HELP` | 5112 | HikariSub website | GLOBAL `F1` |
| `GLOBAL_ANSI` | 5113 | Report an issue | Unbound |
| `GLOBAL_CHECK_FOR_UPDATES` | 5114 | Check for updates | Unbound |
| `GLOBAL_PREVIOUS_LINE` | 5150 | Previous line | GLOBAL `Ctrl-Up` |
| `GLOBAL_NEXT_LINE` | 5151 | Next line | GLOBAL `Ctrl-Down` |
| `GLOBAL_JOIN_WITH_PREVIOUS` | 5152 | Merge with previous line | GLOBAL `F4` |
| `GLOBAL_JOIN_WITH_NEXT` | 5153 | Merge with next line | GLOBAL `F5` |
| `GLOBAL_NEXT_TAB` | 5154 | Next tab | GLOBAL `Ctrl-PgDn` |
| `GLOBAL_PREVIOUS_TAB` | 5155 | Previous tab | GLOBAL `Ctrl-PgUp` |
| `GLOBAL_REMOVE_LINES` | 5156 | Delete line | GLOBAL `Shift-Delete` |
| `GLOBAL_REMOVE_TEXT` | 5157 | Delete text | GLOBAL `Alt-Delete` |
| `GLOBAL_SNAP_WITH_START` | 5158 | Change start time to nearest keyframe | GLOBAL `Shift-Left` |
| `GLOBAL_SNAP_WITH_END` | 5159 | Change end time to nearest keyframe | GLOBAL `Shift-Right` |
| `GLOBAL_SORT_LINES` | 5160 | global sort lines | Unbound |
| `GLOBAL_SORT_SELECTED_LINES` | 5161 | global sort selected lines | Unbound |
| `GLOBAL_RECENT_AUDIO` | 5162 | global recent audio | Unbound |
| `GLOBAL_RECENT_VIDEO` | 5163 | global recent video | Unbound |
| `GLOBAL_RECENT_SUBS` | 5164 | global recent subs | Unbound |
| `GLOBAL_RECENT_KEYFRAMES` | 5165 | global recent keyframes | Unbound |
| `GLOBAL_SELECT_FROM_VIDEO` | 5166 | Select line at current video position | GLOBAL `F2` |
| `GLOBAL_PLAY_ACTUAL_LINE` | 5167 | Play active line | Unbound |
| `GLOBAL_STYLE_MANAGER_CLEAN_STYLE` | 5168 | Clean styles of ASS file | Unbound |
| `GLOBAL_SORT_ALL_BY_START_TIMES` | 5200 | Sort all lines by start time | Unbound |
| `GLOBAL_SORT_ALL_BY_END_TIMES` | 5201 | Sort all lines by end time | Unbound |
| `GLOBAL_SORT_ALL_BY_STYLE` | 5202 | Sort all lines by styles | Unbound |
| `GLOBAL_SORT_ALL_BY_ACTOR` | 5203 | Sort all lines by actor | Unbound |
| `GLOBAL_SORT_ALL_BY_EFFECT` | 5204 | Sort all lines by effect | Unbound |
| `GLOBAL_SORT_ALL_BY_LAYER` | 5205 | Sort all lines by layer | Unbound |
| `GLOBAL_SORT_SELECTED_BY_START_TIMES` | 5206 | Sort selected lines by start time | Unbound |
| `GLOBAL_SORT_SELECTED_BY_END_TIMES` | 5207 | Sort selected lines by end time | Unbound |
| `GLOBAL_SORT_SELECTED_BY_STYLE` | 5208 | Sort selected lines by styles | Unbound |
| `GLOBAL_SORT_SELECTED_BY_ACTOR` | 5209 | Sort selected lines by actor | Unbound |
| `GLOBAL_SORT_SELECTED_BY_EFFECT` | 5210 | Sort selected lines by effect | Unbound |
| `GLOBAL_SORT_SELECTED_BY_LAYER` | 5211 | Sort selected lines by layer | Unbound |
| `GLOBAL_SHIFT_TIMES` | 5300 | Shift times / run time post processor | Unbound |
| `GLOBAL_ADD_PAGE` | 5301 | Open new tab | GLOBAL `Ctrl-T` |
| `GLOBAL_CLOSE_PAGE` | 5302 | Close current tab | GLOBAL `Ctrl-W` |

Named key spellings (the key-code map is the authority, not locale display text):

```cpp
keys[WXK_BACK] = L"Backspace";
keys[WXK_SPACE] = L"Space";
keys[WXK_RETURN] = L"Enter";
keys[WXK_TAB] = L"Tab";
keys[WXK_PAUSE] = L"Pause";
keys[WXK_LEFT] = L"Left";
keys[WXK_RIGHT] = L"Right";
keys[WXK_UP] = L"Up";
keys[WXK_DOWN] = L"Down";
keys[WXK_INSERT] = L"Insert";
keys[WXK_DELETE] = L"Delete";
keys[WXK_HOME] = L"Home";
keys[WXK_END] = L"End";
keys[WXK_PAGEUP] = L"PgUp";
keys[WXK_PAGEDOWN] = L"PgDn";
keys[WXK_NUMPAD0] = L"Num 0";
keys[WXK_NUMPAD1] = L"Num 1";
keys[WXK_NUMPAD2] = L"Num 2";
keys[WXK_NUMPAD3] = L"Num 3";
keys[WXK_NUMPAD4] = L"Num 4";
keys[WXK_NUMPAD5] = L"Num 5";
keys[WXK_NUMPAD6] = L"Num 6";
keys[WXK_NUMPAD7] = L"Num 7";
keys[WXK_NUMPAD8] = L"Num 8";
keys[WXK_NUMPAD9] = L"Num 9";
keys[WXK_NUMPAD_ADD] = L"Num +";
keys[WXK_NUMPAD_SUBTRACT] = L"Num -";
keys[WXK_NUMPAD_SEPARATOR] = L"Num |";
keys[WXK_NUMPAD_MULTIPLY] = L"Num *";
keys[WXK_NUMPAD_DIVIDE] = L"Num /";
keys[WXK_NUMPAD_DECIMAL] = L"Num .";
keys[WXK_NUMPAD_ENTER] = L"Num Enter";
keys[WXK_F1] = L"F1";
keys[WXK_F2] = L"F2";
keys[WXK_F3] = L"F3";
keys[WXK_F4] = L"F4";
keys[WXK_F5] = L"F5";
keys[WXK_F6] = L"F6";
keys[WXK_F7] = L"F7";
keys[WXK_F8] = L"F8";
keys[WXK_F9] = L"F9";
keys[WXK_F10] = L"F10";
keys[WXK_F11] = L"F11";
keys[WXK_F12] = L"F12";
```

## Appendix D — complete legacy key conversion table

Each row is copied from `ConfigConverter::CreateTable`. A delimiter means split the old scalar into the new brace-table format. `emptyString` means direct rename, except the special toolbar action conversion described above. Old spellings/typos are part of the input contract. [Source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ConfigConverter.cpp#L23).

| Family | Legacy key | New key | Old list delimiter |
|---|---|---|---|
| Config | `AudioAutoCommit` | `AUDIO_AUTO_COMMIT` | `emptyString` |
| Config | `AudioAutoFocus` | `AUDIO_AUTO_FOCUS` | `emptyString` |
| Config | `AudioAutoScroll` | `AUDIO_AUTO_SCROLL` | `emptyString` |
| Config | `AudioBoxHeight` | `AUDIO_BOX_HEIGHT` | `emptyString` |
| Config | `AudioDelay` | `AUDIO_DELAY` | `emptyString` |
| Config | `AudioDrawKeyframes` | `AUDIO_DRAW_KEYFRAMES` | `emptyString` |
| Config | `AudioDrawSecondaryLines` | `AUDIO_DRAW_SECONDARY_LINES` | `emptyString` |
| Config | `AudioDrawSelectionBackground` | `AUDIO_DRAW_SELECTION_BACKGROUND` | `emptyString` |
| Config | `AudioDrawTimeCursor` | `AUDIO_DRAW_TIME_CURSOR` | `emptyString` |
| Config | `AudioDrawVideoPosition` | `AUDIO_DRAW_VIDEO_POSITION` | `emptyString` |
| Config | `AudioGrabTimesOnSelect` | `AUDIO_GRAB_TIMES_ON_SELECT` | `emptyString` |
| Config | `AudioHorizontalZoom` | `AUDIO_HORIZONTAL_ZOOM` | `emptyString` |
| Config | `AudioInactiveLinesDisplayMode` | `AUDIO_INACTIVE_LINES_DISPLAY_MODE` | `emptyString` |
| Config | `AudioKaraoke` | `AUDIO_KARAOKE` | `emptyString` |
| Config | `AudioKaraokeMoveOnClick` | `AUDIO_KARAOKE_MOVE_ON_CLICK` | `emptyString` |
| Config | `AudioKaraokeSplitMode` | `AUDIO_KARAOKE_SPLIT_MODE` | `emptyString` |
| Config | `AudioLeadIn` | `AUDIO_LEAD_IN_VALUE` | `emptyString` |
| Config | `AudioLeadOut` | `AUDIO_LEAD_OUT_VALUE` | `emptyString` |
| Config | `AudioLineBoundariesThickness` | `AUDIO_LINE_BOUNDARIES_THICKNESS` | `emptyString` |
| Config | `AudioLink` | `AUDIO_LINK` | `emptyString` |
| Config | `AudioLockScrollOnCursor` | `AUDIO_LOCK_SCROLL_ON_CURSOR` | `emptyString` |
| Config | `AudioMarkPlayTime` | `AUDIO_MARK_PLAY_TIME` | `emptyString` |
| Config | `AudioMergeEveryNWithSyllable` | `AUDIO_MERGE_EVERY_N_WITH_SYLLABLE` | `emptyString` |
| Config | `AudioNextLineOnCommit` | `AUDIO_NEXT_LINE_ON_COMMIT` | `emptyString` |
| Config | `AudioRAMCache` | `AUDIO_RAM_CACHE` | `emptyString` |
| Config | `AudioSnapToKeyframes` | `AUDIO_SNAP_TO_KEYFRAMES` | `emptyString` |
| Config | `AudioSnapToOtherLines` | `AUDIO_SNAP_TO_OTHER_LINES` | `emptyString` |
| Config | `AudioSpectrumOn` | `AUDIO_SPECTRUM_ON` | `emptyString` |
| Config | `AudioSpectrumNonLinearOn` | `AUDIO_SPECTRUM_NON_LINEAR_ON` | `emptyString` |
| Config | `AudioStartDragSensitivity` | `AUDIO_START_DRAG_SENSITIVITY` | `emptyString` |
| Config | `AudioVerticalZoom` | `AUDIO_VERTICAL_ZOOM` | `emptyString` |
| Config | `AudioVolume` | `AUDIO_VOLUME` | `emptyString` |
| Config | `AudioWheelDefaultToZoom` | `AUDIO_WHEEL_DEFAULT_TO_ZOOM` | `emptyString` |
| Config | `AcceptedAudioStream` | `ACCEPTED_AUDIO_STREAM` | `emptyString` |
| Config | `ASSPropertiesTitle` | `ASS_PROPERTIES_TITLE` | `emptyString` |
| Config | `ASSPropertiesScript` | `ASS_PROPERTIES_SCRIPT` | `emptyString` |
| Config | `ASSPropertiesTranslation` | `ASS_PROPERTIES_TRANSLATION` | `emptyString` |
| Config | `ASSPropertiesEditing` | `ASS_PROPERTIES_EDITING` | `emptyString` |
| Config | `ASSPropertiesTiming` | `ASS_PROPERTIES_TIMING` | `emptyString` |
| Config | `ASSPropertiesUpdate` | `ASS_PROPERTIES_UPDATE` | `emptyString` |
| Config | `ASSPropertiesTitleOn` | `ASS_PROPERTIES_TITLE_ON` | `emptyString` |
| Config | `ASSPropertiesScriptOn` | `ASS_PROPERTIES_SCRIPT_ON` | `emptyString` |
| Config | `ASSPropertiesTranslationOn` | `ASS_PROPERTIES_TRANSLATION_ON` | `emptyString` |
| Config | `ASSPropertiesEditingOn` | `ASS_PROPERTIES_EDITING_ON` | `emptyString` |
| Config | `ASSPropertiesTimingOn` | `ASS_PROPERTIES_TIMING_ON` | `emptyString` |
| Config | `ASSPropertiesUpdateOn` | `ASS_PROPERTIES_UPDATE_ON` | `emptyString` |
| Config | `ASSPropertiesAskForChange` | `ASS_PROPERTIES_ASK_FOR_CHANGE` | `emptyString` |
| Config | `AudioRecent` | `AUDIO_RECENT_FILES` | `L"&#124;"` |
| Config | `AutomationLoadingMethod` | `AUTOMATION_LOADING_METHOD` | `emptyString` |
| Config | `AutomationOldScriptsCompatybility` | `AUTOMATION_OLD_SCRIPTS_COMPATIBILITY` | `emptyString` |
| Config | `AutomationRecent` | `AUTOMATION_RECENT_FILES` | `emptyString` |
| Config | `AutomationScriptEditor` | `AUTOMATION_SCRIPT_EDITOR` | `emptyString` |
| Config | `AutomationTraceLevel` | `AUTOMATION_TRACE_LEVEL` | `emptyString` |
| Config | `AutoMoveTagsFromOriginal` | `AUTO_MOVE_TAGS_FROM_ORIGINAL` | `emptyString` |
| Config | `AutoSaveMaxFiles` | `AUTOSAVE_MAX_FILES` | `emptyString` |
| Config | `AutoSelectLinesFromLastTab` | `AUTO_SELECT_LINES_FROM_LAST_TAB` | `emptyString` |
| Config | `ColorpickerRecent` | `COLORPICKER_RECENT_COLORS` | `emptyString` |
| Config | `ConvertASSTagsOnLineStart` | `CONVERT_ASS_TAGS_TO_INSERT_IN_LINE` | `emptyString` |
| Config | `ConvertFPS` | `CONVERT_FPS` | `emptyString` |
| Config | `ConvertFPSFromVideo` | `CONVERT_FPS_FROM_VIDEO` | `emptyString` |
| Config | `ConvertNewEndTimes` | `CONVERT_NEW_END_TIMES` | `emptyString` |
| Config | `ConvertResolutionWidth` | `CONVERT_RESOLUTION_WIDTH` | `emptyString` |
| Config | `ConvertResolutionHeight` | `CONVERT_RESOLUTION_HEIGHT` | `emptyString` |
| Config | `ConvertShowSettings` | `CONVERT_SHOW_SETTINGS` | `emptyString` |
| Config | `ConvertStyle` | `CONVERT_STYLE` | `emptyString` |
| Config | `ConvertStyleCatalog` | `CONVERT_STYLE_CATALOG` | `emptyString` |
| Config | `ConvertTimePerLetter` | `CONVERT_TIME_PER_CHARACTER` | `emptyString` |
| Config | `CopyCollumnsSelection` | `COPY_COLLUMS_SELECTIONS` | `emptyString` |
| Config | `DictionaryLanguage` | `DICTIONARY_LANGUAGE` | `emptyString` |
| Config | `DisableLiveVideoEditing` | `DISABLE_LIVE_VIDEO_EDITING` | `emptyString` |
| Config | `DontAskForBadResolution` | `DONT_ASK_FOR_BAD_RESOLUTION` | `emptyString` |
| Config | `EditboxSugestionsOnDoubleClick` | `EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK` | `emptyString` |
| Config | `EditorOn` | `EDITOR_ON` | `emptyString` |
| Config | `FIND_IN_SUBS_FILTERS_RECENT` | `FIND_IN_SUBS_FILTERS_RECENT` | `L"\f"` |
| Config | `FIND_IN_SUBS_PATHS_RECENT` | `FIND_IN_SUBS_PATHS_RECENT` | `L"\f"` |
| Config | `FindRecent` | `FIND_RECENT_FINDS` | `L"\f"` |
| Config | `FindReplaceOptions` | `FIND_REPLACE_OPTIONS` | `emptyString` |
| Config | `FontCollectorAction` | `FONT_COLLECTOR_ACTION` | `emptyString` |
| Config | `FontCollectorDirectory` | `FONT_COLLECTOR_DIRECTORY` | `emptyString` |
| Config | `FontCollectorFromMKV` | `FONT_COLLECTOR_FROM_MKV` | `emptyString` |
| Config | `FontCollectorUseSubsDirectory` | `FONT_COLLECTOR_USE_SUBS_DIRECTORY` | `emptyString` |
| Config | `FFMS2VideoSeeking` | `FFMS2_VIDEO_SEEKING` | `emptyString` |
| Config | `GridChangeActiveOnSelection` | `GRID_CHANGE_ACTIVE_ON_SELECTION` | `emptyString` |
| Config | `GridFontName` | `GRID_FONT` | `emptyString` |
| Config | `GridFontSize` | `GRID_FONT_SIZE` | `emptyString` |
| Config | `GridAddToFilter` | `GRID_ADD_TO_FILTER` | `emptyString` |
| Config | `GridFilterAfterLoad` | `GRID_FILTER_AFTER_LOAD` | `emptyString` |
| Config | `GridFilterBy` | `GRID_FILTER_BY` | `emptyString` |
| Config | `GridFilterInverted` | `GRID_FILTER_INVERTED` | `emptyString` |
| Config | `GridFilterStyles` | `GRID_FILTER_STYLES` | `L","` |
| Config | `GridHideCollums` | `GRID_HIDE_COLUMNS` | `emptyString` |
| Config | `GridHideTags` | `GRID_HIDE_TAGS` | `emptyString` |
| Config | `GridIgnoreFiltering` | `GRID_IGNORE_FILTERING` | `emptyString` |
| Config | `GridLoadSortedSubs` | `GRID_LOAD_SORTED_SUBS` | `emptyString` |
| Config | `GridSaveAfterCharacterCount` | `GRID_SAVE_AFTER_CHARACTER_COUNT` | `emptyString` |
| Config | `GridTagsSwapChar` | `GRID_TAGS_SWAP_CHARACTER` | `emptyString` |
| Config | `InsertEndOffset` | `GRID_INSERT_END_OFFSET` | `emptyString` |
| Config | `InsertStartOffset` | `GRID_INSERT_START_OFFSET` | `emptyString` |
| Config | `KEYFRAMES_RECENT` | `KEYFRAMES_RECENT` | `L"&#124;"` |
| Config | `MoveTimesByTime` | `SHIFT_TIMES_BY_TIME` | `emptyString` |
| Config | `MoveTimesLoadSetTabOptions` | `SHIFT_TIMES_CHANGE_VALUES_WITH_TAB` | `emptyString` |
| Config | `MoveTimesCorrectEndTimes` | `SHIFT_TIMES_CORRECT_END_TIMES` | `emptyString` |
| Config | `MoveTimesForward` | `SHIFT_TIMES_MOVE_FORWARD` | `emptyString` |
| Config | `MoveTimesFrames` | `SHIFT_TIMES_DISPLAY_FRAMES` | `emptyString` |
| Config | `MoveTimesOn` | `SHIFT_TIMES_ON` | `emptyString` |
| Config | `MoveTimesOptions` | `SHIFT_TIMES_OPTIONS` | `emptyString` |
| Config | `MoveTimesWhichLines` | `SHIFT_TIMES_WHICH_LINES` | `emptyString` |
| Config | `MoveTimesWhichTimes` | `SHIFT_TIMES_WHICH_TIMES` | `emptyString` |
| Config | `MoveTimesStyles` | `SHIFT_TIMES_STYLES` | `emptyString` |
| Config | `MoveTimesTime` | `SHIFT_TIMES_TIME` | `emptyString` |
| Config | `MoveVideoToActiveLine` | `MOVE_VIDEO_TO_ACTIVE_LINE` | `emptyString` |
| Config | `NoNewLineAfterTimesEdition` | `EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT` | `emptyString` |
| Config | `OpenSubsInNewCard` | `OPEN_SUBS_IN_NEW_TAB` | `emptyString` |
| Config | `OpenVideoAtActiveLine` | `OPEN_VIDEO_AT_ACTIVE_LINE` | `emptyString` |
| Config | `PasteCollumnsSelection` | `PASTE_COLUMNS_SELECTION` | `emptyString` |
| Config | `PlayAfterSelection` | `VIDEO_PLAY_AFTER_SELECTION` | `emptyString` |
| Config | `PostprocessorEnabling` | `POSTPROCESSOR_ON` | `emptyString` |
| Config | `PostprocessorKeyframeBeforeStart` | `POSTPROCESSOR_KEYFRAME_BEFORE_START` | `emptyString` |
| Config | `PostprocessorKeyframeAfterStart` | `POSTPROCESSOR_KEYFRAME_AFTER_START` | `emptyString` |
| Config | `PostprocessorKeyframeBeforeEnd` | `POSTPROCESSOR_KEYFRAME_BEFORE_END` | `emptyString` |
| Config | `PostprocessorKeyframeAfterEnd` | `POSTPROCESSOR_KEYFRAME_AFTER_END` | `emptyString` |
| Config | `PostprocessorLeadIn` | `POSTPROCESSOR_LEAD_IN` | `emptyString` |
| Config | `PostprocessorLeadOut` | `POSTPROCESSOR_LEAD_OUT` | `emptyString` |
| Config | `PostprocessorThresholdStart` | `POSTPROCESSOR_THRESHOLD_START` | `emptyString` |
| Config | `PostprocessorThresholdEnd` | `POSTPROCESSOR_THRESHOLD_END` | `emptyString` |
| Config | `PreviewText` | `STYLE_PREVIEW_TEXT` | `emptyString` |
| Config | `ProgramLanguage` | `PROGRAM_LANGUAGE` | `emptyString` |
| Config | `ProgramTheme` | `PROGRAM_THEME` | `emptyString` |
| Config | `ReplaceRecent` | `REPLACE_RECENT_REPLACEMENTS` | `L"\f"` |
| Config | `SelectionsRecent` | `SELECT_LINES_RECENT_SELECTIONS` | `L"\f"` |
| Config | `SelectionsOptions` | `SELECT_LINES_OPTIONS` | `emptyString` |
| Config | `SelectVisibleLineAfterFullscreen` | `GRID_SET_VISIBLE_LINE_AFTER_FULL_SCREEN` | `emptyString` |
| Config | `SHIFT_TIMES_PROFILES` | `SHIFT_TIMES_PROFILES` | `L"\f"` |
| Config | `SpellcheckerOn` | `SPELLCHECKER_ON` | `emptyString` |
| Config | `StyleEditFilterText` | `STYLE_EDIT_FILTER_TEXT` | `emptyString` |
| Config | `StyleFilterTextOn` | `STYLE_EDIT_FILTER_TEXT_ON` | `emptyString` |
| Config | `StyleManagerPosition` | `STYLE_MANAGER_POSITION` | `emptyString` |
| Config | `StyleManagerDetachEditor` | `STYLE_MANAGER_DETACH_EDIT_WINDOW` | `emptyString` |
| Config | `SubsAutonaming` | `SUBS_AUTONAMING` | `emptyString` |
| Config | `SubsComparisonType` | `SUBS_COMPARISON_TYPE` | `emptyString` |
| Config | `SubsComparisonStyles` | `SUBS_COMPARISON_STYLES` | `emptyString` |
| Config | `SubsRecent` | `SUBS_RECENT_FILES` | `L"&#124;"` |
| Config | `TextFieldAllowNumpadHotkeys` | `TEXT_FIELD_ALLOW_NUMPAD_HOTKEYS` | `emptyString` |
| Config | `TlModeShowOriginal` | `TL_MODE_SHOW_ORIGINAL` | `emptyString` |
| Config | `ToolbarIDs` | `TOOLBAR_IDS` | `L"&#124;"` |
| Config | `ToolbarAlignment` | `TOOLBAR_ALIGNMENT` | `emptyString` |
| Config | `UpdaterCheckForStable` | `UPDATER_CHECK_FOR_STABLE` | `emptyString` |
| Config | `VideoFullskreenOnStart` | `VIDEO_FULL_SCREEN_ON_START` | `emptyString` |
| Config | `VideoIndex` | `VIDEO_INDEX` | `emptyString` |
| Config | `VideoPauseOnClick` | `VIDEO_PAUSE_ON_CLICK` | `emptyString` |
| Config | `VideoProgressBar` | `VIDEO_PROGRESS_BAR` | `emptyString` |
| Config | `VideoRecent` | `VIDEO_RECENT_FILES` | `L"&#124;"` |
| Config | `VideoVolume` | `VIDEO_VOLUME` | `emptyString` |
| Config | `VideoWindowSize` | `VIDEO_WINDOW_SIZE` | `emptyString` |
| Config | `VisualWarningsOff` | `VIDEO_VISUAL_WARNINGS_OFF` | `emptyString` |
| Config | `WindowMaximized` | `WINDOW_MAXIMIZED` | `emptyString` |
| Config | `WindowPosition` | `WINDOW_POSITION` | `emptyString` |
| Config | `WindowSize` | `WINDOW_SIZE` | `emptyString` |
| Config | `EditboxTagButtons` | `EDITBOX_TAG_BUTTONS` | `emptyString` |
| Config | `EditboxTagButton1` | `EDITBOX_TAG_BUTTON_VALUE1` | `L"\f"` |
| Config | `EditboxTagButton2` | `EDITBOX_TAG_BUTTON_VALUE2` | `L"\f"` |
| Config | `EditboxTagButton3` | `EDITBOX_TAG_BUTTON_VALUE3` | `L"\f"` |
| Config | `EditboxTagButton4` | `EDITBOX_TAG_BUTTON_VALUE4` | `L"\f"` |
| Config | `EditboxTagButton5` | `EDITBOX_TAG_BUTTON_VALUE5` | `L"\f"` |
| Config | `EditboxTagButton6` | `EDITBOX_TAG_BUTTON_VALUE6` | `L"\f"` |
| Config | `EditboxTagButton7` | `EDITBOX_TAG_BUTTON_VALUE7` | `L"\f"` |
| Config | `EditboxTagButton8` | `EDITBOX_TAG_BUTTON_VALUE8` | `L"\f"` |
| Config | `EditboxTagButton9` | `EDITBOX_TAG_BUTTON_VALUE9` | `L"\f"` |
| Config | `EditboxTagButton10` | `EDITBOX_TAG_BUTTON_VALUE10` | `L"\f"` |
| Config | `EditboxTagButton11` | `EDITBOX_TAG_BUTTON_VALUE11` | `L"\f"` |
| Config | `EditboxTagButton12` | `EDITBOX_TAG_BUTTON_VALUE12` | `L"\f"` |
| Config | `EditboxTagButton13` | `EDITBOX_TAG_BUTTON_VALUE13` | `L"\f"` |
| Config | `EditboxTagButton14` | `EDITBOX_TAG_BUTTON_VALUE14` | `L"\f"` |
| Config | `EditboxTagButton15` | `EDITBOX_TAG_BUTTON_VALUE15` | `L"\f"` |
| Config | `EditboxTagButton16` | `EDITBOX_TAG_BUTTON_VALUE16` | `L"\f"` |
| Config | `EditboxTagButton17` | `EDITBOX_TAG_BUTTON_VALUE17` | `L"\f"` |
| Config | `EditboxTagButton18` | `EDITBOX_TAG_BUTTON_VALUE18` | `L"\f"` |
| Config | `EditboxTagButton19` | `EDITBOX_TAG_BUTTON_VALUE19` | `L"\f"` |
| Config | `EditboxTagButton20` | `EDITBOX_TAG_BUTTON_VALUE20` | `L"\f"` |
| Colors | `WindowBackground` | `WINDOW_BACKGROUND` | n/a |
| Colors | `WindowBackgroundInactive` | `WINDOW_BACKGROUND_INACTIVE` | n/a |
| Colors | `WindowText` | `WINDOW_TEXT` | n/a |
| Colors | `WindowTextInactive` | `WINDOW_TEXT_INACTIVE` | n/a |
| Colors | `WindowBorder` | `WINDOW_BORDER` | n/a |
| Colors | `WindowBorderInactive` | `WINDOW_BORDER_INACTIVE` | n/a |
| Colors | `WindowBorderBackground` | `WINDOW_BORDER_BACKGROUND` | n/a |
| Colors | `WindowBorderBackgroundInactive` | `WINDOW_BORDER_BACKGROUND_INACTIVE` | n/a |
| Colors | `WindowHeaderText` | `WINDOW_HEADER_TEXT` | n/a |
| Colors | `WindowHeaderTextInactive` | `WINDOW_HEADER_TEXT_INACTIVE` | n/a |
| Colors | `WindowHoverHeaderElement` | `WINDOW_HOVER_HEADER_ELEMENT` | n/a |
| Colors | `WindowPushedHeaderElement` | `WINDOW_PUSHED_HEADER_ELEMENT` | n/a |
| Colors | `WindowHoverCloseButton` | `WINDOW_HOVER_CLOSE_BUTTON` | n/a |
| Colors | `WindowPushedCloseButton` | `WINDOW_PUSHED_CLOSE_BUTTON` | n/a |
| Colors | `WindowWarningElements` | `WINDOW_WARNING_ELEMENTS` | n/a |
| Colors | `GridText` | `GRID_TEXT` | n/a |
| Colors | `GridBackground` | `GRID_BACKGROUND` | n/a |
| Colors | `GridDialogue` | `GRID_DIALOGUE` | n/a |
| Colors | `GridComment` | `GRID_COMMENT` | n/a |
| Colors | `GridSelection` | `GRID_SELECTION` | n/a |
| Colors | `GridVisibleOnVideo` | `GRID_LINE_VISIBLE_ON_VIDEO` | n/a |
| Colors | `GridCollisions` | `GRID_COLLISIONS` | n/a |
| Colors | `GridLines` | `GRID_LINES` | n/a |
| Colors | `GridActiveLine` | `GRID_ACTIVE_LINE` | n/a |
| Colors | `GridHeader` | `GRID_HEADER` | n/a |
| Colors | `GridHeaderText` | `GRID_HEADER_TEXT` | n/a |
| Colors | `GridLabelNormal` | `GRID_LABEL_NORMAL` | n/a |
| Colors | `GridLabelModified` | `GRID_LABEL_MODIFIED` | n/a |
| Colors | `GridLabelSaved` | `GRID_LABEL_SAVED` | n/a |
| Colors | `GridLabelDoubtful` | `GRID_LABEL_DOUBTFUL` | n/a |
| Colors | `GridSpellchecker` | `GRID_SPELLCHECKER` | n/a |
| Colors | `GridComparisonOutline` | `GRID_COMPARISON_OUTLINE` | n/a |
| Colors | `GridComparisonBackgroundNotMatch` | `GRID_COMPARISON_BACKGROUND_NOT_MATCH` | n/a |
| Colors | `GridComparisonBackgroundMatch` | `GRID_COMPARISON_BACKGROUND_MATCH` | n/a |
| Colors | `GridComparisonCommentBackgroundNotMatch` | `GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH` | n/a |
| Colors | `GridComparisonCommentBackgroundMatch` | `GRID_COMPARISON_COMMENT_BACKGROUND_MATCH` | n/a |
| Colors | `EditorText` | `EDITOR_TEXT` | n/a |
| Colors | `EditorTagNames` | `EDITOR_TAG_NAMES` | n/a |
| Colors | `EditorTagValues` | `EDITOR_TAG_VALUES` | n/a |
| Colors | `EditorCurlyBraces` | `EDITOR_CURLY_BRACES` | n/a |
| Colors | `EditorTagOperators` | `EDITOR_TAG_OPERATORS` | n/a |
| Colors | `EditorTemplateVariables` | `EDITOR_TEMPLATE_VARIABLES` | n/a |
| Colors | `EditorTemplateCodeMarks` | `EDITOR_TEMPLATE_CODE_MARKS` | n/a |
| Colors | `EditorTemplateFunctions` | `EDITOR_TEMPLATE_FUNCTIONS` | n/a |
| Colors | `EditorTemplateKeywords` | `EDITOR_TEMPLATE_KEYWORDS` | n/a |
| Colors | `EditorTemplateStrings` | `EDITOR_TEMPLATE_STRINGS` | n/a |
| Colors | `EditorPhraseSearch` | `EDITOR_PHRASE_SEARCH` | n/a |
| Colors | `EditorBracesBackground` | `EDITOR_BRACES_BACKGROUND` | n/a |
| Colors | `EditorBackground` | `EDITOR_BACKGROUND` | n/a |
| Colors | `EditorSelection` | `EDITOR_SELECTION` | n/a |
| Colors | `EditorSelectionNoFocus` | `EDITOR_SELECTION_NO_FOCUS` | n/a |
| Colors | `EditorBorder` | `EDITOR_BORDER` | n/a |
| Colors | `EditorBorderOnFocus` | `EDITOR_BORDER_ON_FOCUS` | n/a |
| Colors | `EditorSpellchecker` | `EDITOR_SPELLCHECKER` | n/a |
| Colors | `AudioBackground` | `AUDIO_BACKGROUND` | n/a |
| Colors | `AudioLineBoundaryStart` | `AUDIO_LINE_BOUNDARY_START` | n/a |
| Colors | `AudioLineBoundaryEnd` | `AUDIO_LINE_BOUNDARY_END` | n/a |
| Colors | `AudioLineBoundaryMark` | `AUDIO_LINE_BOUNDARY_MARK` | n/a |
| Colors | `AudioLineBoundaryInactiveLine` | `AUDIO_LINE_BOUNDARY_INACTIVE_LINE` | n/a |
| Colors | `AudioPlayCursor` | `AUDIO_PLAY_CURSOR` | n/a |
| Colors | `AudioSecondsBoundaries` | `AUDIO_SECONDS_BOUNDARIES` | n/a |
| Colors | `AudioKeyframes` | `AUDIO_KEYFRAMES` | n/a |
| Colors | `AudioSyllableBoundaries` | `AUDIO_SYLLABLE_BOUNDARIES` | n/a |
| Colors | `AudioSyllableText` | `AUDIO_SYLLABLE_TEXT` | n/a |
| Colors | `AudioSelectionBackground` | `AUDIO_SELECTION_BACKGROUND` | n/a |
| Colors | `AudioSelectionBackgroundModified` | `AUDIO_SELECTION_BACKGROUND_MODIFIED` | n/a |
| Colors | `AudioInactiveLinesBackground` | `AUDIO_INACTIVE_LINES_BACKGROUND` | n/a |
| Colors | `AudioWaveform` | `AUDIO_WAVEFORM` | n/a |
| Colors | `AudioWaveformInactive` | `AUDIO_WAVEFORM_INACTIVE` | n/a |
| Colors | `AudioWaveformModified` | `AUDIO_WAVEFORM_MODIFIED` | n/a |
| Colors | `AudioWaveformSelected` | `AUDIO_WAVEFORM_SELECTED` | n/a |
| Colors | `AudioSpectrumBackground` | `AUDIO_SPECTRUM_BACKGROUND` | n/a |
| Colors | `AudioSpectrumEcho` | `AUDIO_SPECTRUM_ECHO` | n/a |
| Colors | `AudioSpectrumInner` | `AUDIO_SPECTRUM_INNER` | n/a |
| Colors | `TextFieldBackground` | `TEXT_FIELD_BACKGROUND` | n/a |
| Colors | `TextFieldBorder` | `TEXT_FIELD_BORDER` | n/a |
| Colors | `TextFieldBorderOnFocus` | `TEXT_FIELD_BORDER_ON_FOCUS` | n/a |
| Colors | `TextFieldSelection` | `TEXT_FIELD_SELECTION` | n/a |
| Colors | `TextFieldSelectionNoFocus` | `TEXT_FIELD_SELECTION_NO_FOCUS` | n/a |
| Colors | `ButtonBackground` | `BUTTON_BACKGROUND` | n/a |
| Colors | `ButtonBackgroundHover` | `BUTTON_BACKGROUND_HOVER` | n/a |
| Colors | `ButtonBackgroundPushed` | `BUTTON_BACKGROUND_PUSHED` | n/a |
| Colors | `ButtonBackgroundOnFocus` | `BUTTON_BACKGROUND_ON_FOCUS` | n/a |
| Colors | `ButtonBorder` | `BUTTON_BORDER` | n/a |
| Colors | `ButtonBorderHover` | `BUTTON_BORDER_HOVER` | n/a |
| Colors | `ButtonBorderPushed` | `BUTTON_BORDER_PUSHED` | n/a |
| Colors | `ButtonBorderOnFocus` | `BUTTON_BORDER_ON_FOCUS` | n/a |
| Colors | `ButtonBorderInactive` | `BUTTON_BORDER_INACTIVE` | n/a |
| Colors | `TogglebuttonBackgroundToggled` | `TOGGLE_BUTTON_BACKGROUND_TOGGLED` | n/a |
| Colors | `TogglebuttonBorderToggled` | `TOGGLE_BUTTON_BORDER_TOGGLED` | n/a |
| Colors | `ScrollbarBackground` | `SCROLLBAR_BACKGROUND` | n/a |
| Colors | `ScrollbarScroll` | `SCROLLBAR_THUMB` | n/a |
| Colors | `ScrollbarScrollHover` | `SCROLLBAR_THUMB_HOVER` | n/a |
| Colors | `ScrollbarScrollPushed` | `SCROLLBAR_THUMB_PUSHED` | n/a |
| Colors | `StaticboxBorder` | `STATICBOX_BORDER` | n/a |
| Colors | `StaticListBorder` | `STATICLIST_BORDER` | n/a |
| Colors | `StaticListBackground` | `STATICLIST_BACKGROUND` | n/a |
| Colors | `StaticListSelection` | `STATICLIST_SELECTION` | n/a |
| Colors | `StaticListBackgroundHeadline` | `STATICLIST_BACKGROUND_HEADLINE` | n/a |
| Colors | `StaticListTextHeadline` | `STATICLIST_TEXT_HEADLINE` | n/a |
| Colors | `StatusBarBorder` | `STATUSBAR_BORDER` | n/a |
| Colors | `MenuBarBackground1` | `MENUBAR_BACKGROUND1` | n/a |
| Colors | `MenuBarBackground2` | `MENUBAR_BACKGROUND2` | n/a |
| Colors | `MenuBarBorderSelection` | `MENUBAR_BORDER_SELECTION` | n/a |
| Colors | `MENU_BAR_BACKGROUND_HOVER` | `MENUBAR_BACKGROUND_HOVER` | n/a |
| Colors | `MenuBarBackgroundSelection` | `MENUBAR_BACKGROUND_SELECTION` | n/a |
| Colors | `MenuBackground` | `MENUBAR_BACKGROUND` | n/a |
| Colors | `MenuBorderSelection` | `MENU_BORDER_SELECTION` | n/a |
| Colors | `MenuBackgroundSelection` | `MENU_BACKGROUND_SELECTION` | n/a |
| Colors | `TabsBarBackground1` | `TABSBAR_BACKGROUND1` | n/a |
| Colors | `TabsBarBackground2` | `TABSBAR_BACKGROUND2` | n/a |
| Colors | `TabsBorderActive` | `TABS_BORDER_ACTIVE` | n/a |
| Colors | `TabsBorderInactive` | `TABS_BORDER_INACTIVE` | n/a |
| Colors | `TabsBackgroundActive` | `TABS_BACKGROUND_ACTIVE` | n/a |
| Colors | `TabsBackgroundInactive` | `TABS_BACKGROUND_INACTIVE` | n/a |
| Colors | `TabsBackgroundInactiveHover` | `TABS_BACKGROUND_INACTIVE_HOVER` | n/a |
| Colors | `TabsBackgroundSecondWindow` | `TABS_BACKGROUND_SECOND_WINDOW` | n/a |
| Colors | `TabsTextActive` | `TABS_TEXT_ACTIVE` | n/a |
| Colors | `TabsTextInactive` | `TABS_TEXT_INACTIVE` | n/a |
| Colors | `TabsCloseHover` | `TABS_CLOSE_HOVER` | n/a |
| Colors | `TabsBarArrow` | `TABSBAR_ARROW` | n/a |
| Colors | `TabsBarArrowBackground` | `TABSBAR_ARROW_BACKGROUND` | n/a |
| Colors | `TabsBarArrowBackgroundHover` | `TABSBAR_ARROW_BACKGROUND_HOVER` | n/a |
| Colors | `SliderPathBackground` | `SLIDER_PATH_BACKGROUND` | n/a |
| Colors | `SliderPathBorder` | `SLIDER_PATH_BORDER` | n/a |
| Colors | `SliderBorder` | `SLIDER_BORDER` | n/a |
| Colors | `SliderBorderHover` | `SLIDER_BORDER_HOVER` | n/a |
| Colors | `SliderBorderPushed` | `SLIDER_BORDER_PUSHED` | n/a |
| Colors | `SliderBackground` | `SLIDER_BACKGROUND` | n/a |
| Colors | `SliderBackgroundHover` | `SLIDER_BACKGROUND_HOVER` | n/a |
| Colors | `SliderBackgroundPushed` | `SLIDER_BACKGROUND_PUSHED` | n/a |
| Colors | `StylePreviewColor1` | `STYLE_PREVIEW_COLOR1` | n/a |
| Colors | `StylePreviewColor2` | `STYLE_PREVIEW_COLOR2` | n/a |
| Hotkeys | `AudioCommitAlt` | `AUDIO_COMMIT_ALT` | n/a |
| Hotkeys | `AudioPlayAlt` | `AUDIO_PLAY_ALT` | n/a |
| Hotkeys | `AudioPlayLineAlt` | `AUDIO_PLAY_LINE_ALT` | n/a |
| Hotkeys | `AudioPreviousAlt` | `AUDIO_PREVIOUS_ALT` | n/a |
| Hotkeys | `AudioNextAlt` | `AUDIO_NEXT_ALT` | n/a |
| Hotkeys | `AudioCommit` | `AUDIO_COMMIT` | n/a |
| Hotkeys | `AudioPlay` | `AUDIO_PLAY` | n/a |
| Hotkeys | `AudioPlayLine` | `AUDIO_PLAY_LINE` | n/a |
| Hotkeys | `AudioPrevious` | `AUDIO_PREVIOUS` | n/a |
| Hotkeys | `AudioNext` | `AUDIO_NEXT` | n/a |
| Hotkeys | `AudioStop` | `AUDIO_STOP` | n/a |
| Hotkeys | `AudioPlayBeforeMark` | `AUDIO_PLAY_BEFORE_MARK` | n/a |
| Hotkeys | `AudioPlayAfterMark` | `AUDIO_PLAY_AFTER_MARK` | n/a |
| Hotkeys | `AudioPlay500MSBefore` | `AUDIO_PLAY_500MS_BEFORE` | n/a |
| Hotkeys | `AudioPlay500MSAfter` | `AUDIO_PLAY_500MS_AFTER` | n/a |
| Hotkeys | `AudioPlay500MSFirst` | `AUDIO_PLAY_500MS_FIRST` | n/a |
| Hotkeys | `AudioPlay500MSLast` | `AUDIO_PLAY_500MS_LAST` | n/a |
| Hotkeys | `AudioPlayToEnd` | `AUDIO_PLAY_TO_END` | n/a |
| Hotkeys | `AudioScrollLeft` | `AUDIO_SCROLL_LEFT` | n/a |
| Hotkeys | `AudioScrollRight` | `AUDIO_SCROLL_RIGHT` | n/a |
| Hotkeys | `AudioGoto` | `AUDIO_GOTO` | n/a |
| Hotkeys | `AudioLeadin` | `AUDIO_LEAD_IN` | n/a |
| Hotkeys | `AudioLeadout` | `AUDIO_LEAD_OUT` | n/a |
| Hotkeys | `PlayPause` | `VIDEO_PLAY_PAUSE` | n/a |
| Hotkeys | `StopPlayback` | `VIDEO_STOP` | n/a |
| Hotkeys | `Plus5Second` | `VIDEO_5_SECONDS_FORWARD` | n/a |
| Hotkeys | `Minus5Second` | `VIDEO_5_SECONDS_BACKWARD` | n/a |
| Hotkeys | `MinusMinute` | `VIDEO_MINUTE_BACKWARD` | n/a |
| Hotkeys | `PlusMinute` | `VIDEO_MINUTE_FORWARD` | n/a |
| Hotkeys | `VolumePlus` | `VIDEO_VOLUME_PLUS` | n/a |
| Hotkeys | `VolumeMinus` | `VIDEO_VOLUME_MINUS` | n/a |
| Hotkeys | `PreviousVideo` | `VIDEO_PREVIOUS_FILE` | n/a |
| Hotkeys | `NextVideo` | `VIDEO_NEXT_FILE` | n/a |
| Hotkeys | `PreviousChapter` | `VIDEO_PREVIOUS_CHAPTER` | n/a |
| Hotkeys | `NextChapter` | `VIDEO_NEXT_CHAPTER` | n/a |
| Hotkeys | `FullScreen` | `VIDEO_FULL_SCREEN` | n/a |
| Hotkeys | `HideProgressBar` | `VIDEO_HIDE_PROGRESS_BAR` | n/a |
| Hotkeys | `DeleteVideo` | `VIDEO_DELETE_FILE` | n/a |
| Hotkeys | `AspectRatio` | `VIDEO_ASPECT_RATIO` | n/a |
| Hotkeys | `CopyCoords` | `VIDEO_COPY_COORDS` | n/a |
| Hotkeys | `FrameToPNG` | `VIDEO_SAVE_FRAME_TO_PNG` | n/a |
| Hotkeys | `FrameToClipboard` | `VIDEO_COPY_FRAME_TO_CLIPBOARD` | n/a |
| Hotkeys | `SubbedFrameToPNG` | `VIDEO_SAVE_SUBBED_FRAME_TO_PNG` | n/a |
| Hotkeys | `SubbedFrameToClipboard` | `VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD` | n/a |
| Hotkeys | `PutBold` | `EDITBOX_INSERT_BOLD` | n/a |
| Hotkeys | `PutItalic` | `EDITBOX_INSERT_ITALIC` | n/a |
| Hotkeys | `SplitLine` | `EDITBOX_SPLIT_LINE` | n/a |
| Hotkeys | `StartDifference` | `EDITBOX_START_DIFFERENCE` | n/a |
| Hotkeys | `EndDifference` | `EDITBOX_END_DIFFERENCE` | n/a |
| Hotkeys | `FindNextDoubtful` | `EDITBOX_FIND_NEXT_DOUBTFUL` | n/a |
| Hotkeys | `FindNextUntranslated` | `EDITBOX_FIND_NEXT_UNTRANSLATED` | n/a |
| Hotkeys | `SetDoubtful` | `EDITBOX_SET_DOUBTFUL` | n/a |
| Hotkeys | `GRID_FILTER_INVERTED` | `GRID_FILTER_INVERT` | n/a |
| Hotkeys | `InsertBefore` | `GRID_INSERT_BEFORE` | n/a |
| Hotkeys | `InsertAfter` | `GRID_INSERT_AFTER` | n/a |
| Hotkeys | `InsertBeforeVideo` | `GRID_INSERT_BEFORE_VIDEO` | n/a |
| Hotkeys | `InsertAfterVideo` | `GRID_INSERT_AFTER_VIDEO` | n/a |
| Hotkeys | `InsertBeforeWithVideoFrame` | `GRID_INSERT_BEFORE_WITH_VIDEO_FRAME` | n/a |
| Hotkeys | `InsertAfterWithVideoFrame` | `GRID_INSERT_AFTER_WITH_VIDEO_FRAME` | n/a |
| Hotkeys | `Swap` | `GRID_SWAP_LINES` | n/a |
| Hotkeys | `Duplicate` | `GRID_DUPLICATE_LINES` | n/a |
| Hotkeys | `Join` | `GRID_JOIN_LINES` | n/a |
| Hotkeys | `JoinToFirst` | `GRID_JOIN_TO_FIRST_LINE` | n/a |
| Hotkeys | `JoinToLast` | `GRID_JOIN_TO_LAST_LINE` | n/a |
| Hotkeys | `Copy` | `GRID_COPY` | n/a |
| Hotkeys | `Paste` | `GRID_PASTE` | n/a |
| Hotkeys | `Cut` | `GRID_CUT` | n/a |
| Hotkeys | `ShowPreview` | `GRID_SHOW_PREVIEW` | n/a |
| Hotkeys | `HideSelected` | `GRID_HIDE_SELECTED` | n/a |
| Hotkeys | `FilterByNothing` | `GRID_FILTER_BY_NOTHING` | n/a |
| Hotkeys | `FilterByStyles` | `GRID_FILTER_BY_STYLES` | n/a |
| Hotkeys | `FilterBySelections` | `GRID_FILTER_BY_SELECTIONS` | n/a |
| Hotkeys | `FilterByDialogues` | `GRID_FILTER_BY_DIALOGUES` | n/a |
| Hotkeys | `FilterByDoubtful` | `GRID_FILTER_BY_DOUBTFUL` | n/a |
| Hotkeys | `FilterByUntranslated` | `GRID_FILTER_BY_UNTRANSLATED` | n/a |
| Hotkeys | `PasteTranslation` | `GRID_PASTE_TRANSLATION` | n/a |
| Hotkeys | `TranslationDialog` | `GRID_TRANSLATION_DIALOG` | n/a |
| Hotkeys | `SubsFromMKV` | `GRID_SUBS_FROM_MKV` | n/a |
| Hotkeys | `ContinousPrevious` | `GRID_MAKE_CONTINOUS_PREVIOUS_LINE` | n/a |
| Hotkeys | `ContinousNext` | `GRID_MAKE_CONTINOUS_NEXT_LINE` | n/a |
| Hotkeys | `PasteCollumns` | `GRID_PASTE_COLUMNS` | n/a |
| Hotkeys | `CopyCollumns` | `GRID_COPY_COLUMNS` | n/a |
| Hotkeys | `FPSFromVideo` | `GRID_SET_FPS_FROM_VIDEO` | n/a |
| Hotkeys | `NewFPS` | `GRID_SET_NEW_FPS` | n/a |
| Hotkeys | `SaveSubs` | `GLOBAL_SAVE_SUBS` | n/a |
| Hotkeys | `SaveAllSubs` | `GLOBAL_SAVE_ALL_SUBS` | n/a |
| Hotkeys | `SaveSubsAs` | `GLOBAL_SAVE_SUBS_AS` | n/a |
| Hotkeys | `SaveTranslation` | `GLOBAL_SAVE_TRANSLATION` | n/a |
| Hotkeys | `RemoveSubs` | `GLOBAL_REMOVE_SUBS` | n/a |
| Hotkeys | `Search` | `GLOBAL_SEARCH` | n/a |
| Hotkeys | `SelectLinesDialog` | `GLOBAL_OPEN_SELECT_LINES` | n/a |
| Hotkeys | `SpellcheckerDialog` | `GLOBAL_OPEN_SPELLCHECKER` | n/a |
| Hotkeys | `VideoIndexing` | `GLOBAL_VIDEO_INDEXING` | n/a |
| Hotkeys | `SaveWithVideoName` | `GLOBAL_SAVE_WITH_VIDEO_NAME` | n/a |
| Hotkeys | `OpenAudio` | `GLOBAL_OPEN_AUDIO` | n/a |
| Hotkeys | `AudioFromVideo` | `GLOBAL_AUDIO_FROM_VIDEO` | n/a |
| Hotkeys | `CloseAudio` | `GLOBAL_CLOSE_AUDIO` | n/a |
| Hotkeys | `ASSProperties` | `GLOBAL_OPEN_ASS_PROPERTIES` | n/a |
| Hotkeys | `StyleManager` | `GLOBAL_OPEN_STYLE_MANAGER` | n/a |
| Hotkeys | `SubsResample` | `GLOBAL_OPEN_SUBS_RESAMPLE` | n/a |
| Hotkeys | `FontCollectorID` | `GLOBAL_OPEN_FONT_COLLECTOR` | n/a |
| Hotkeys | `ConvertToASS` | `GLOBAL_CONVERT_TO_ASS` | n/a |
| Hotkeys | `ConvertToSRT` | `GLOBAL_CONVERT_TO_SRT` | n/a |
| Hotkeys | `ConvertToTMP` | `GLOBAL_CONVERT_TO_TMP` | n/a |
| Hotkeys | `ConvertToMDVD` | `GLOBAL_CONVERT_TO_MDVD` | n/a |
| Hotkeys | `ConvertToMPL2` | `GLOBAL_CONVERT_TO_MPL2` | n/a |
| Hotkeys | `HideTags` | `GLOBAL_HIDE_TAGS` | n/a |
| Hotkeys | `ChangeTime` | `GLOBAL_SHOW_SHIFT_TIMES` | n/a |
| Hotkeys | `ViewAll` | `GLOBAL_VIEW_ALL` | n/a |
| Hotkeys | `ViewAudio` | `GLOBAL_VIEW_AUDIO` | n/a |
| Hotkeys | `ViewVideo` | `GLOBAL_VIEW_VIDEO` | n/a |
| Hotkeys | `ViewSubs` | `GLOBAL_VIEW_SUBS` | n/a |
| Hotkeys | `AutoLoadScript` | `GLOBAL_AUTOMATION_LOAD_SCRIPT` | n/a |
| Hotkeys | `AutoReloadAutoload` | `GLOBAL_AUTOMATION_RELOAD_AUTOLOAD` | n/a |
| Hotkeys | `LoadLastScript` | `GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT` | n/a |
| Hotkeys | `AUTOMATION_OPEN_HOTKEYS_WINDOW` | `GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW` | n/a |
| Hotkeys | `PlayPauseG` | `GLOBAL_PLAY_PAUSE` | n/a |
| Hotkeys | `PreviousFrame` | `GLOBAL_PREVIOUS_FRAME` | n/a |
| Hotkeys | `NextFrame` | `GLOBAL_NEXT_FRAME` | n/a |
| Hotkeys | `VideoZoom` | `GLOBAL_VIDEO_ZOOM` | n/a |
| Hotkeys | `SetStartTime` | `GLOBAL_SET_START_TIME` | n/a |
| Hotkeys | `SetEndTime` | `GLOBAL_SET_END_TIME` | n/a |
| Hotkeys | `SetVideoAtStart` | `GLOBAL_SET_VIDEO_AT_START_TIME` | n/a |
| Hotkeys | `SetVideoAtEnd` | `GLOBAL_SET_VIDEO_AT_END_TIME` | n/a |
| Hotkeys | `GoToNextKeyframe` | `GLOBAL_GO_TO_NEXT_KEYFRAME` | n/a |
| Hotkeys | `GoToPrewKeyframe` | `GLOBAL_GO_TO_PREVIOUS_KEYFRAME` | n/a |
| Hotkeys | `SetAudioFromVideo` | `GLOBAL_SET_AUDIO_FROM_VIDEO` | n/a |
| Hotkeys | `SetAudioMarkFromVideo` | `GLOBAL_SET_AUDIO_MARK_FROM_VIDEO` | n/a |
| Hotkeys | `Redo` | `GLOBAL_REDO` | n/a |
| Hotkeys | `Undo` | `GLOBAL_UNDO` | n/a |
| Hotkeys | `UndoToLastSave` | `GLOBAL_UNDO_TO_LAST_SAVE` | n/a |
| Hotkeys | `History` | `GLOBAL_HISTORY` | n/a |
| Hotkeys | `OpenSubs` | `GLOBAL_OPEN_SUBS` | n/a |
| Hotkeys | `OpenVideo` | `GLOBAL_OPEN_VIDEO` | n/a |
| Hotkeys | `GLOBAL_KEYFRAMES_OPEN` | `GLOBAL_OPEN_KEYFRAMES` | n/a |
| Hotkeys | `Settings` | `GLOBAL_SETTINGS` | n/a |
| Hotkeys | `Quit` | `GLOBAL_QUIT` | n/a |
| Hotkeys | `Editor` | `GLOBAL_EDITOR` | n/a |
| Hotkeys | `About` | `GLOBAL_ABOUT` | n/a |
| Hotkeys | `Helpers` | `GLOBAL_HELPERS` | n/a |
| Hotkeys | `Help` | `GLOBAL_HELP` | n/a |
| Hotkeys | `ANSI` | `GLOBAL_ANSI` | n/a |
| Hotkeys | `PreviousLine` | `GLOBAL_PREVIOUS_LINE` | n/a |
| Hotkeys | `NextLine` | `GLOBAL_NEXT_LINE` | n/a |
| Hotkeys | `JoinWithPrevious` | `GLOBAL_JOIN_WITH_PREVIOUS` | n/a |
| Hotkeys | `JoinWithNext` | `GLOBAL_JOIN_WITH_NEXT` | n/a |
| Hotkeys | `NextTab` | `GLOBAL_NEXT_TAB` | n/a |
| Hotkeys | `PreviousTab` | `GLOBAL_PREVIOUS_TAB` | n/a |
| Hotkeys | `Remove` | `GLOBAL_REMOVE_LINES` | n/a |
| Hotkeys | `RemoveText` | `GLOBAL_REMOVE_TEXT` | n/a |
| Hotkeys | `SnapWithStart` | `GLOBAL_SNAP_WITH_START` | n/a |
| Hotkeys | `SnapWithEnd` | `GLOBAL_SNAP_WITH_END` | n/a |
| Hotkeys | `Plus5SecondG` | `GLOBAL_5_SECONDS_FORWARD` | n/a |
| Hotkeys | `Minus5SecondG` | `GLOBAL_5_SECONDS_BACKWARD` | n/a |
| Hotkeys | `SortLines` | `GLOBAL_SORT_LINES` | n/a |
| Hotkeys | `SortSelected` | `GLOBAL_SORT_SELECTED_LINES` | n/a |
| Hotkeys | `RecentAudio` | `GLOBAL_RECENT_AUDIO` | n/a |
| Hotkeys | `RecentVideo` | `GLOBAL_RECENT_VIDEO` | n/a |
| Hotkeys | `RecentSubs` | `GLOBAL_RECENT_SUBS` | n/a |
| Hotkeys | `GLOBAL_KEYFRAMES_RECENT` | `GLOBAL_RECENT_KEYFRAMES` | n/a |
| Hotkeys | `SelectFromVideo` | `GLOBAL_SELECT_FROM_VIDEO` | n/a |
| Hotkeys | `PlayActualLine` | `GLOBAL_PLAY_ACTUAL_LINE` | n/a |

## Appendix E — exact packed-choice and rule templates

The source excerpts below preserve implicit enum increments, regex backslashes and localized default descriptions. UI control IDs after the bitfields are excluded.

[HikariSub/FindReplace.h](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/FindReplace.h#L136)

```cpp
CASE_SENSITIVE = 1,
	REG_EX,
	START_OF_TEXT = 4,
	END_OF_TEXT = 8,
	IN_FIELD_TEXT = 16,
	IN_FIELD_TEXT_ORIGINAL = 32,
	IN_FIELD_STYLE = 64,
	IN_FIELD_ACTOR = 128,
	IN_FIELD_EFFECT = 256,
	IN_LINES_ALL = 512,
	IN_LINES_SELECTED = 1024,
	IN_LINES_FROM_SELECTION = 2048,
	SEARCH_SUBFOLDERS = 4096,
	SEARCH_HIDDEN_FOLDERS = 8192,
	SEEK_IN_COMMENTS = 1 << 14,
	SEEK_ONLY_IN_TEXT = 1 << 15,
	SEEK_ONLY_IN_TAGS = 1 << 16,
```

[HikariSub/SelectLines.h](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SelectLines.h#L78)

```cpp
CONTAINS = 1,
	NOT_CONTAINS,
	MATCH_CASE = 1 << 2,
	REGULAR_EXPRESSIONS = 1 << 3,
	FIELD_TEXT = 1 << 4,
	FIELD_STYLE = 1 << 5,
	FIELD_ACTOR = 1 << 6,
	FIELD_EFFECT = 1 << 7,
	FIELD_START_TIME = 1 << 8,
	FIELD_END_TIME = 1 << 9,
	DIALOGUES = 1 << 10,
	COMMENTS = 1 << 11,
	SELECT = 1 << 12,
	ADD_TO_SELECTION = 1 << 13,
	DESELECT = 1 << 14,
	DO_NOTHING = 1 << 15,
	DO_COPY = 1 << 16,
	DO_CUT = 1 << 17,
	DO_MOVE_ON_START = 1 << 18,
	DO_MOVE_ON_END = 1 << 19,
	DO_SET_ASS_COMMENT = 1 << 20,
	DO_DELETE = 1 << 21,
```

[HikariSub/MisspellReplacer.cpp](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/MisspellReplacer.cpp#L706)

```cpp
rules = L"#HikariSub rules file\n0|0|0|0|0|0|0|0|0|0|0|0\n" +
		_("Remove space before comma or dot") + L"\f ([,.!?%])\f\\1\f0\n" +
		_("Remove doubled spaces") + L"\f(  +)\f \f0\n" +
		_("Replace more then three dots to suspension point") + L"\f\\.{4,}\f...\f0\n" +
		_("Replace two dots to suspension point") + L"\f([^.])\\.\\.([^.])\f\\1...\\2\f0\n" +
		_("Replace missing spaces after dot or comma") + L"\f([^.])([,.!?%])([^ ,.!?%\\\"\\\\0-9-])\f\\1\\2 \\3\f0\n" +
		_("Removing japanese suffixes") + L"\f ?- ?(san|chan|kun|sama|nee|dono|senpai|sensei)\\M\f\f0\n" +
		_("Fixing Polish \"sie\"") + L"\f\\msie\\M\fsię\f0\n" +
		_("Fixing Polish \"nie mozliwe\"") + L"\f\\mnie możliwe\\M\fniemożliwe\f0\n" +
		_("Fixing Polish \"nie wazne\"") + L"\f\\mnie ważne\\M\fnieważne\f0\n" +
		_("Fixing Polish \"w ogole\"") + L"\f\\mw ?og[uo]le\\M\fw ogóle\f0\n" +
		_("Fixing Polish \"w ogole\"") + L"\f\\mwogóle\\M\fw ogóle\f0\n" +
		_("Fixing Polish \"bede\"") + L"\f\\mbed[eę]\\M\fbędę\f0\n" +
		_("Fixing Polish \"bede\"") + L"\f\\mbęde\\M\fbędę\f0";
```
