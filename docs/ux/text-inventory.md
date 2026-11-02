# Text inventory (#596 step 1)

This is every user-visible English string, checked against the [text style guide](text-style.md), for [#596](https://github.com/altqx/hikari/issues/596).

- **Source:** `i18n/hikarisub_pl.ts` at `main` 62b81609b. Its 3,826 messages in 35 contexts are the same in all four interface catalogues.
- **Not covered:** the 1,836 `HikariSub.Automation.Gettext` keys. They are out of scope (text-style.md, "Scope").
- **Files:** each string was traced to the files that hold it (`src/`, `Automation/`). 18 strings are built from pieces across several source lines, so they have no file.
- **Area:** each string has the area of the bulk PR that will fix it.
  - Grid and Line editor: the Grid, the Line editor, columns.
  - Menus: `command_names.cpp`, the menu bar and context menus in `Main.qml`, Home, the tab and title-bar menus.
  - Dialogs: windows and popovers.
  - Settings: the Settings window, Getting started, legacy import.
  - Panels: the dock panels and the visual tools.
  - Messages: text that only C++ sends, which is toasts, the Log, errors and reports.
- **What is in the list:** [text-inventory.tsv](text-inventory.tsv) holds all 3,826 rows, as area, context, source, findings and files. The tables below list only the flagged ones.

**The findings are automatic.** They are what the CI guard will check (text-style.md, "The guard"), plus the term list. Each area PR also reads the strings that were not flagged, against the rules that a pattern can't check:
- verb-first commands;
- noun labels;
- an ellipsis that is missing because a command opens a dialog;
- one name per thing.

The naming list below comes from that reading, and from the menu inventory ([context-menus.md](context-menus.md), "Cross-menu inconsistencies").

## One thing, several names

| Thing | Names on screen today | Guide's name |
|---|---|---|
| Start-time column, sort key, paste column | "The starting time" (column choices, Edit › Sort all lines, Sort selected lines); "Start time" (Grid header, Shift times) | Start time |
| Style column | "Styles" (column choices, sort menus); "Style" (Grid header) | Style |
| Translation-review flag | "Not confirmed" (Line editor ⋯ M4, its tooltip); "Unconfirmed" (Grid, Next unconfirmed line) | Unconfirmed |
| Script properties | "ASS file properties" (Subtitles menu), "ASS subtitle properties", "Subtitle properties"; CONTEXT.md says Script properties | Script properties |
| Translation matching window | "Dialogue shifting window" (Grid menu, hotkey 4549); the window is titled "Translation matching options" | Translation matching… |
| Font chooser | "Font selection" (command 3000, Line editor strip, the window's title) | Choose font… (command); Choose font (window) |
| Audio commit | "Commit", "Auto-commit", "Commit options", "Next after commit", "Go to the next line after commit"; "Automatically apply changes", "Go to the next line after applying changes" (M3) | Apply changes, Apply changes automatically, Go to the next line after applying (rec. 3) |
| Line timing mode | "Line" (audio toolbar), "Line timing" (M3) | Line timing |
| Non-linear spectrum | "Speech" (audio toolbar), "Speech (non-linear spectrum)" (M3) | Speech spectrum |
| Float a panel | "Undock" (panel ⋯ R10), "Float" (View › Panels) | Float (rec. 4) |
| Close a panel | "Close" (panel ⋯), "Hide" (View › Panels) | Close (rec. 4) |
| Show a file's folder | "Show in folder" (Home), "Open the folder containing the subtitles", "Open video containing folder", "Open audio containing folder", "Open keyframes containing folder" (tab menu R11), "Open folder" | Show in folder; Show subtitles / video / audio / keyframes in folder |
| Play or pause | "Play / Pause" (menu bar), "Play" or "Pause" (video menu R8) | Play or pause; the button's states are Play and Pause |
| Open video, subtitles, audio | "Open video…" (menu bar), "Open video", "Open subtitles" (video menu R8), "Open audio", "Open keyframes" (menu bar, no "…") | Open video…, Open subtitles…, Open audio…, Open keyframes… |
| Edit an item | "Edit…" (Styles row R13), "Edit..." (tag button R6), "Edit style" (Styles ⋯ M6) | Edit style… in both style menus; Edit… on a tag button |
| Style manager | "Open in Style manager" (Styles row R13), "Style manager" (M6, menu bar) | Style manager |
| Add to dictionary | “Add “%1” to dictionary” (grammar), "Add word "%1" to dictionary" (spelling) | Add “%1” to dictionary |
| Shift times command | "Shift times..." and "Shift times / run timing postprocessor" (Subtitles menu) | Shift times… and Timing postprocessor… (two panels, #598) |
| Find | "Find", "Find and replace", the "Search" panel, "Search text:" (#597 merges them) | Set by #597's design |
| Spelling tool | "Spellchecker" (panel), "spell checker", "Spell checking" (messages) | Spellchecker; Check spelling |
| Autosave | "Autosave", "Autosaves…" (Home), "Open auto save" (File menu) | autosave; Open autosave… |
| Shortcut | "shortcut" in 39 strings, "hotkey" in 21, Settings › Hotkeys, "Edit box hotkey", "Subtitle grid hotkey" | shortcut (rec. 1); Line editor shortcuts, Grid shortcuts |
| Grid | "Subtitle grid" (hotkey section, accessible name), "subtitles list", "Grid" (panel) | Grid |
| Line editor | "Edit box hotkey", "Editor shortcuts", "Line editor" (panel) | Line editor |
| Colour picker | "color picker", "color selection window" (the colour popover's option) | colour picker; colour window |
| Catalogue | "catalog" in 59 strings | catalogue (rec. 2) |
| Hide selected lines | Two Grid menu items with that name: GRID_HIDE_SELECTED (an action) and GRID_FILTER_BY_SELECTIONS (a filter check item) | Two names, chosen by CX2 [#578](https://github.com/altqx/hikari/issues/578) with the context menus |

## Counts

| Area | Strings | Flagged |
|---|---|---|
| Grid and Line editor | 403 | 70 |
| Menus | 1014 | 82 |
| Dialogs | 851 | 86 |
| Settings | 532 | 31 |
| Panels | 523 | 36 |
| Messages | 503 | 157 |
| All | 3826 | 462 |

The counts are of messages, so a source that appears in two contexts counts twice. The tables further down list each distinct source once per area.

| Finding | Grid and Line editor | Menus | Dialogs | Settings | Panels | Messages |
|---|---|---|---|---|---|---|
| spelling | 0 | 22 | 52 | 7 | 2 | 24 |
| quotes | 43 | 6 | 14 | 8 | 10 | 67 |
| term | 14 | 26 | 5 | 6 | 8 | 8 |
| leading-the | 11 | 2 | 11 | 4 | 10 | 18 |
| hard-wrap | 2 | 15 | 2 | 2 | 5 | 14 |
| whitespace | 1 | 2 | 0 | 2 | 3 | 41 |
| title-case | 7 | 9 | 4 | 2 | 3 | 8 |
| colon | 2 | 0 | 1 | 1 | 10 | 8 |
| hard-coded-key | 4 | 0 | 5 | 0 | 0 | 0 |
| ellipsis | 1 | 2 | 0 | 0 | 0 | 0 |

## Sample for review

These are the strings the owner reviews with the guide. Once the guide is approved, the area PRs apply the same rules to every flagged string without a review of each one.

| # | Area | Before | After | Rule |
|---|---|---|---|---|
| S1 | Grid and Line editor | The starting time | Start time | Noun label |
| S2 | Grid and Line editor | Styles | Style | Singular for one value |
| S3 | Grid and Line editor | Not confirmed | Unconfirmed | One name (CONTEXT.md) |
| S4 | Grid and Line editor | Copy\tCtrl+C | Copy | Key from the binding |
| S5 | Grid and Line editor | Copy these columns of the selected line: | Columns to copy from the selected line | No colon |
| S6 | Grid and Line editor | Insert Tag at text beginning | Insert tag at start of text | Sentence case |
| S7 | Grid and Line editor | End is before Start. | End time is before start time. | Sentence case, one name |
| S8 | Grid and Line editor | Characters per second.\nShould not exceed 15 characters per second | Characters per second. Keep it at 15 or under. | No hard wrap |
| S9 | Grid and Line editor | Edit... | Edit… | "…" |
| S10 | Grid and Line editor | Allows quick editing of the current line's style | Edits the current line’s style | Say what it does, ’ |
| S11 | Grid and Line editor | Dialogue shifting window | Translation matching… | One name (window is “Translation matching options”) |
| S12 | Menus | Shift times... | Shift times… | "…" |
| S13 | Menus | Import legacy settings... | Import legacy settings… | "…" |
| S14 | Menus | ASS file properties | Script properties | One name (CONTEXT.md) |
| S15 | Menus | Open auto save | Open autosave… | One name, "…" |
| S16 | Menus | Play / Pause | Play or pause | Command wording |
| S17 | Menus | Open video containing folder | Show video in folder | One name |
| S18 | Menus | Open the folder containing the subtitles | Show subtitles in folder | One name |
| S19 | Menus | Show / hide render cost | Render cost | Check item names its state |
| S20 | Menus | Font selection | Choose font… | Verb-first command |
| S21 | Menus | Auto-commit | Apply changes automatically | One name (rec. 3) |
| S22 | Menus | Next after commit | Go to the next line after applying | One name (rec. 3) |
| S23 | Menus | Undock | Float | One name (rec. 4) |
| S24 | Menus | Kanji timer… | Kanji timer | No "…" on a tool (rec. 5) |
| S25 | Menus | Border color | Border colour | British |
| S26 | Menus | Top-center | Top-centre | British |
| S27 | Dialogs | A catalog named “%1” already exists. | A catalogue named “%1” already exists. | British (rec. 2) |
| S28 | Dialogs | Apply to All | Apply to all | Sentence case |
| S29 | Dialogs | The line’s selected text | Selected text | No leading “the” |
| S30 | Dialogs | The Style editor’s sample text | Style editor sample | No leading “the” |
| S31 | Dialogs | Swap shortcuts between the color picker\nand the color selection window | Click opens the picker, right-click the colour window | British, no hard wrap, one name |
| S32 | Settings | The system’s | System | No leading “the” |
| S33 | Settings | Filtering mode: | Filtering mode | No colon |
| S34 | Settings | Range from 0 to 10000, where 0 turns off\nremoving audio cache files. | 0–10000. At 0, audio cache files are never removed. | No hard wrap, en dash |
| S35 | Settings | Language (program restart required) | Language (applies after a restart) | Plain wording |
| S36 | Panels | Ignore All | Ignore all | Sentence case |
| S37 | Messages | Failed to initialize spell checker. | The spellchecker could not start. | British, one name, says what happened |
| S38 | Messages | Cannot retrieve the font file sizes and names;\ncopying will be canceled.\n | Nothing was copied: the fonts’ file sizes and names could not be read. | British, no hard wrap |
| S39 | Messages | Error loading Lua script: %1\n%2 | %1 did not load. Its error is in Details. | Message form (%2 goes to Details) |
| S40 | Messages | In line %1, value '%2' cannot be scaled\nin tag '%3' | Line %1: the value “%2” in %3 cannot be scaled. | Curly quotes, no hard wrap |
| S41 | Messages | %n line(s) get the conversion style "%1". | %n line(s) get the conversion style “%1”. | Curly quotes |

## Flagged strings by area

One row per distinct source in each area. The finding codes are the guide's rules, and `term:X` names the word the guide uses instead. [text-inventory.tsv](text-inventory.tsv) has every string, flagged or not, with the files it is in.

### Grid and Line editor (63)

| Context | Source | Findings |
|---|---|---|
| Editor | &Add word "%1" to dictionary | quotes |
| Editor | 0: the Style's (%1) | quotes title-case |
| Editor | Allows quick editing of the current line's style | quotes term:say-what-it-does |
| Editor | Characters per second.\nShould not exceed 15 characters per second | hard-wrap |
| Editor | Edit style: allows quick editing of the current line's style | quotes term:say-what-it-does |
| Editor | Edit... | ellipsis term:Edit… |
| Editor | End is before Start. | title-case |
| Editor | Error. Word "%1" was not added. | quotes |
| Editor | Insert Tag at text beginning | title-case |
| Editor | Number of characters in each line.\nNo more than 43 characters per line (maximum 2 lines). | hard-wrap |
| Editor | The document can't be saved right now. | quotes |
| Editor | This document can't be saved here. | quotes |
| GettingStarted | Styles | term:Style-(column) |
| Grid | Copy\tCtrl+C | hard-coded-key title-case |
| Grid | Copy these columns of the %1 selected lines: | colon |
| Grid | Copy these columns of the selected line: | colon |
| Grid | Cut\tCtrl+X | hard-coded-key title-case |
| Grid | Delete lines\tShift+Del | hard-coded-key title-case |
| Grid | Paste\tCtrl+V | hard-coded-key title-case |
| Grid | Select the group's lines | quotes |
| Grid | The starting time | leading-the term:Start-time |
| Search | ,  | whitespace |
| TemplateEditor | Each syllable swells as it is sung and settles back to its Style's size in the sung colour. | quotes |
| TemplateEditor | every global, karaskel's included | quotes |
| TemplateEditor | Line end, from the video's start (ms) | quotes |
| TemplateEditor | Line start, from the video's start (ms) | quotes |
| TemplateEditor | Lua's math library | quotes |
| TemplateEditor | Lua's string library | quotes |
| TemplateEditor | middle of the line, from the video's start (ms) | quotes |
| TemplateEditor | middle of the syllable, from the line's start (ms) | quotes |
| TemplateEditor | sets the generated line's layer | quotes |
| TemplateEditor | sets the generated line's Style | quotes |
| TemplateEditor | sets the generated line's time, by mode | quotes |
| TemplateEditor | sets the loop's counter and count | quotes |
| TemplateEditor | skdur; the line's duration in centiseconds in a pre-line template | quotes |
| TemplateEditor | smid, from the line's start; lmid, from the video's start, in a pre-line template (ms) | quotes |
| TemplateEditor | syllable end, from the line's start (ms) | quotes |
| TemplateEditor | syllable start, from the line's start (ms) | quotes |
| TemplateEditor | the karaoke line | leading-the |
| TemplateEditor | the line being generated | leading-the |
| TemplateEditor | the line's actor | quotes leading-the |
| TemplateEditor | the line's layer | quotes leading-the |
| TemplateEditor | the line's Style | quotes leading-the |
| TemplateEditor | the loop's count | quotes leading-the |
| TemplateEditor | the loop's counter, from 1 | quotes leading-the |
| TemplateEditor | the script's resolution and properties | quotes leading-the |
| TemplateEditor | the syllable | leading-the |
| TemplateEditor | the syllable a furigana part is on | leading-the |
| TemplateEditor | x of the line's centre (px) | quotes |
| TemplateEditor | x of the line's left edge (px) | quotes |
| TemplateEditor | x of the line's right edge (px) | quotes |
| TemplateEditor | x of the syllable's centre (px) | quotes |
| TemplateEditor | x of the syllable's left edge (px) | quotes |
| TemplateEditor | x of the syllable's right edge (px) | quotes |
| TemplateEditor | y of the line's bottom (px) | quotes |
| TemplateEditor | y of the line's middle (px) | quotes |
| TemplateEditor | y of the line's top (px) | quotes |
| TemplateEditor | y of the syllable's bottom (px) | quotes |
| TemplateEditor | y of the syllable's middle (px) | quotes |
| TemplateEditor | y of the syllable's top (px) | quotes |
| Translation | Not confirmed | term:Unconfirmed |
| Translation | Not confirmed: marks the selected lines for review | term:Unconfirmed |
| Translation | Tags from original: the translation gets the Original text's tags | quotes |

### Menus (61)

| Context | Source | Findings |
|---|---|---|
| Audio |  or  | whitespace |
| Audio | Auto-commit | term:apply |
| Audio | Commit | term:apply |
| Audio | Commit options | term:apply |
| Audio | Go to the next line after commit | term:apply |
| Audio | Next after commit | term:apply |
| Audio | Second shortcut can be set using Control + Click | title-case |
| Audio | Shift+wheel and Ctrl+wheel on the waveform zoom too | title-case |
| Audio | Shortcut can be set using Shift + Click | title-case |
| Audio | Volume: %1\nOutput device: %2 | hard-wrap title-case |
| Documents | Subtitle file  | whitespace |
| Editor | Border color | spelling:colour |
| Editor | Bottom-center | spelling:centre |
| Editor | Center | spelling:centre |
| Editor | Primary color | spelling:colour |
| Editor | Secondary color for karaoke | spelling:colour |
| Editor | Shadow color | spelling:colour |
| Editor | Top-center | spelling:centre |
| Grid | Dialogue shifting window | term:Line |
| Hotkeys | ASS file properties | term:Script-properties |
| Hotkeys | Commit alt | term:apply |
| Hotkeys | Open auto save | term:autosave |
| Hotkeys | Play / Pause | term:Play-or-pause |
| Hotkeys | Shift times / run timing postprocessor | term:two-commands |
| MainWindow | ASS subtitle properties | term:Script-properties |
| MainWindow | Import legacy settings... | ellipsis |
| MainWindow | Shift &times... | ellipsis |
| MainWindow | Shows or hides the Fix minor errors panel | title-case |
| MainWindow | What's &new | quotes |
| Reports | The browser did not open | leading-the |
| Reports | The crash report could not be sent | leading-the |
| Spelling | Error. Word "%1" was not added. | quotes |
| Tabs | Open audio containing folder | term:Show-in-folder |
| Tabs | Open keyframes containing folder | term:Show-in-folder |
| Tabs | Open the folder containing the subtitles | term:Show-in-folder |
| Tabs | Open video containing folder | term:Show-in-folder |
| VisualTools | A double "m" was blocked because of a VSFilter bug | quotes |
| VisualTools | Add B-spline | title-case |
| VisualTools | Add Bézier curve | title-case |
| VisualTools | After-center | spelling:centre |
| VisualTools | Before-center | spelling:centre |
| VisualTools | Center-above | spelling:centre |
| VisualTools | Center-below | spelling:centre |
| VisualTools | Changing all Z-rotation tags | title-case |
| VisualTools | How each line's number changes the value | quotes |
| VisualTools | Line is not visible on video\nor has zero duration | hard-wrap |
| VisualTools | List of ASS drawings with an edit option.\nAfter choosing a drawing from the list, put the cursor\nwhere the drawing should start, then hold the left mouse b… | hard-wrap |
| VisualTools | Lock outer quad\nOff: the inner quad is resized within the plane. On: the outer quad is, and the inner one sets the perspective | hard-wrap |
| VisualTools | Move drawings;\nuse only when you want\nto move drawing points.\nDo not combine with the first three options | hard-wrap |
| VisualTools | Preserve proportions so that vector drawings and text\nkeep their original positions after rotation. | hard-wrap |
| VisualTools | Preserve proportions so that vector drawings and text\nkeep their original positions after scaling. | hard-wrap |
| VisualTools | Set a custom rectangle for the current scale.\nIf automatic size calculation does not work correctly,\nyou can set a rectangle for the original scale. | hard-wrap |
| VisualTools | Set angle from 2 points.\nAfter setting 2 points below text on video\nit calculates angle from it. | hard-wrap |
| VisualTools | Set movement using 2 points and the video position.\nAfter placing the first point before the moving text,\nadvance the video by enough frames\nso that the s… | hard-wrap |
| VisualTools | Set position by rectangle.\nAfter drawing a rectangle, the line will be\npositioned on one or both axes\nfor the selected alignment. | hard-wrap |
| VisualTools | Set scale by rectangle.\nAfter drawing rectangle text is scaled\nin two or one axis. | hard-wrap |
| VisualTools | Show surrounding plane\nA rectangle of the text's plane: match it to one in the shot, then place the text inside it | quotes hard-wrap |
| VisualTools | Tag change options:\nSet - inserts the tag at the cursor position for one line,\nor at the start for multiple lines.\nAdd - changes all tags by adding the sl… | hard-wrap |
| VisualTools | Video must be set at least one frame after the line's start time | quotes |
| VisualTools | Visual editing tools\ndo not work on comments | hard-wrap |
| Workspace | Undock | term:Float |

### Dialogs (82)

| Context | Source | Findings |
|---|---|---|
| Automation | Don't load | quotes |
| Colours | Checking this option opens the color picker on left-click,\nand the color selection window on right-click. | spelling:colour hard-wrap term:colour-picker |
| Colours | Swap shortcuts between the color picker\nand the color selection window | spelling:colour hard-wrap term:colour-picker |
| Documents | Apply to All | title-case |
| Documents | Saves %1 · Discards %2 | title-case |
| Fonts | %1 of %2 catalogs in “%3” already exist. | spelling:catalogue |
| Fonts | A catalog named “%1” already exists. | spelling:catalogue |
| Fonts | Add to a catalog | spelling:catalogue |
| Fonts | Add “%1” to a catalog | spelling:catalogue |
| Fonts | Adds fonts to a previously created catalog | spelling:catalogue |
| Fonts | Allows managing font catalogs | spelling:catalogue term:say-what-it-does |
| Fonts | Catalog name | spelling:catalogue |
| Fonts | Catalogs | spelling:catalogue |
| Fonts | Choose a catalog or type a new name. | spelling:catalogue |
| Fonts | Choose font catalog file | spelling:catalogue |
| Fonts | Delete font catalog | spelling:catalogue |
| Fonts | Delete the catalog “%1”? | spelling:catalogue |
| Fonts | Enter a name for the catalog. | spelling:catalogue |
| Fonts | Font catalog: %1 | spelling:catalogue |
| Fonts | Font catalogs | spelling:catalogue |
| Fonts | fonts.toml in the "catalogs" folder of your user folder could not be written. Saving is tried again on exit. | spelling:catalogue quotes |
| Fonts | In the catalog | spelling:catalogue |
| Fonts | Its fonts stay installed and in their other catalogs. | spelling:catalogue |
| Fonts | Load %n catalogs | spelling:catalogue |
| Fonts | Load 1 catalog | spelling:catalogue |
| Fonts | Load font catalogs | spelling:catalogue |
| Fonts | Loaded %n font catalogs | spelling:catalogue |
| Fonts | Loaded 1 font catalog | spelling:catalogue |
| Fonts | Manage catalogs… | spelling:catalogue |
| Fonts | Manage font catalogs | spelling:catalogue |
| Fonts | New catalog | spelling:catalogue |
| Fonts | New catalog… | spelling:catalogue |
| Fonts | No catalogs yet | spelling:catalogue |
| Fonts | Not in a catalog | spelling:catalogue |
| Fonts | Rename font catalog | spelling:catalogue |
| Fonts | Replace the catalog’s fonts | spelling:catalogue |
| Fonts | The catalogs are saved automatically to fonts.toml in the "catalogs" folder of your user folder. | spelling:catalogue quotes |
| Fonts | The file holds no font catalog. | spelling:catalogue |
| Fonts | The file’s new catalogs are added. | spelling:catalogue |
| Fonts | The line’s selected text | leading-the |
| Fonts | The line’s text | leading-the |
| Fonts | The Style editor’s sample text | leading-the |
| Karaoke | Ctrl+Enter | hard-coded-key |
| Karaoke | Ctrl+Z | hard-coded-key |
| Karaoke | The kanji timing could not be applied | leading-the |
| Karaoke | The subtitles have no styles | leading-the |
| Karaoke | Write each further syllable of a group as a # syllable, for the templater's multi effects | quotes |
| Karaoke | Write each kanji's reading after it (漢\|かん), as the romaji sings it | quotes |
| MainWindow | All: | colon |
| MainWindow | Don't ask again | quotes |
| MainWindow | What's new | quotes |
| Reports | Don't send | quotes |
| Reports | The whole report | leading-the |
| Reports | What you'd like | quotes |
| SelectLines | Dialogue | term:Line |
| Styles | Add to catalog | spelling:catalogue |
| Styles | Catalog styles | spelling:catalogue |
| Styles | Copy the selected catalog styles into every open file | spelling:catalogue |
| Styles | Copy the selected catalog styles into this file | spelling:catalogue |
| Styles | Copy the selected file styles into the catalog | spelling:catalogue |
| Styles | Ctrl+Z puts them back. | hard-coded-key |
| Styles | Delete catalog | spelling:catalogue |
| Styles | Font catalog | spelling:catalogue |
| Styles | Move down (Alt+Down) | hard-coded-key |
| Styles | Move up (Alt+Up) | hard-coded-key |
| Styles | Style catalog | spelling:catalogue |
| Styles | the catalog | spelling:catalogue leading-the |
| Styles | The catalog already has a style named “%1”. | spelling:catalogue |
| Styles | The catalog “%1” is shown now; the changes go to “%2”. | spelling:catalogue |
| Styles | The file shown now uses the catalog “%1”. | spelling:catalogue |
| Translation | &lt;b>Original&lt;/b>: the text with the right timing. The pasted dialogue is matched to it, and it is deleted later. | term:Line |
| Translation | Turn off Translation mode? | title-case |
| Translation | Turn on Translation mode for this file? | title-case |
| VisualTools | A drawing can't contain “;”. | quotes |
| VisualTools | A name can't contain “,”. | quotes |
| VisualTools | A name can't contain “;”. | quotes |
| VisualTools | A tag can't contain “,”. | quotes |
| VisualTools | Put the active line's drawing in this shape | quotes |
| VisualTools | The list keeps at least one shape | leading-the |
| VisualTools | The list keeps at least one tag | leading-the |
| VisualTools | The shape keeps its last drawing until this one can be used | leading-the |
| VisualTools | The tag takes %n value(s) | leading-the |

### Settings (28)

| Context | Source | Findings |
|---|---|---|
| Conversion | Catalog | spelling:catalogue |
| GettingStarted | I said I’d be there by nine. | title-case |
| GettingStarted | Later: Settings › General › Saving and Settings › Spelling & grammar | whitespace |
| GettingStarted | Open folder | term:Show-in-folder |
| GettingStarted | The system’s | leading-the |
| Hotkeys | Edit box hotkey | term:Line-editor |
| Hotkeys | Keys for "%1" | quotes |
| Hotkeys | Subtitle grid hotkey | term:Grid |
| Options | Accent color | spelling:colour |
| Options | catalog for style | spelling:catalogue |
| Options | Don't play after changing the line | quotes |
| Options | Don't warn about a resolution mismatch | quotes |
| Options | Filtering mode: | colon |
| Options | In the line editor and the subtitle grid. | term:Grid |
| Options | Light or Dark as the system's colour scheme sets it; high contrast stays as chosen | quotes |
| Options | Merge every "n" with the previous syllable | quotes |
| Options | Open video at the active line's time | quotes |
| Options | Put files .dic and .aff to "Dictionary" folder | quotes |
| Options | Range from 0 to 10000, where 0 turns off\nremoving audio cache files. | hard-wrap |
| Options | Screen size draws subtitles at the size the picture takes on screen, so they stay sharp on large and high-DPI screens. The script's layout is the same either… | quotes |
| Options | Spelling & grammar | whitespace |
| Options | Subtitle grid | term:Grid |
| Options | Subtitle properties | term:Script-properties |
| Options | The script hotkeys of a legacy Hotkeys.txt, added at once | leading-the |
| Options | The selected %1 for conversion does not exist\nand will be changed to the default | hard-wrap leading-the |
| Options | The settings of the page shown back to their defaults | leading-the |
| Options | UI colors | spelling:colour |
| SettingsImport | %1 holds no legacy settings (Config, Dictionary). | title-case |

### Panels (33)

| Context | Source | Findings |
|---|---|---|
| Automation | Filter the run's log | quotes |
| Automation | Save the run's log | quotes |
| Automation | The script's process ended. Reload to start it again; its macro is not rerun. | quotes |
| MainWindow | The starting time | leading-the term:Start-time |
| MisspellReplacer | Line %1:  | colon whitespace |
| Search | Catalog: | spelling:catalogue colon |
| Search | Filters: | colon |
| Search | Replace with: | colon |
| Search | Search text: | colon |
| Search | Subtitle search folder: | colon |
| Search | Windows search filters separated by semicolons, e.g. "*.ass; *.srt". | quotes |
| Spelling | Grammar check no longer uses its %n rules. Turn it back on in Settings › Spelling & grammar. | whitespace |
| Spelling | Ignore All | title-case |
| Styles | Copy to the catalog… | spelling:catalogue |
| Styles | the current line's style | quotes leading-the |
| Styles | The current line's style | quotes leading-the |
| TemplateEditor | Keep the line's override tags | quotes |
| TemplateEditor | Only the selected lines are regenerated; the other lines' output stays as it is. | quotes |
| TemplateEditor | The new line goes above the first karaoke line, with that line's Style. | quotes |
| TemplateEditor | This file's templates run Lua code with your rights. Preview them if you trust the file. | quotes |
| TimeShift | Introduction time in milliseconds | term:ms |
| TimeShift | Lead-out time in milliseconds | term:ms |
| TimeShift | Shifts %1, %2, and %3 tag times\nso their positions on video do not change\n(slows down time shifting) | hard-wrap |
| TimeShift | The choice of lines to move | leading-the |
| TimeShift | The choice of times to move | leading-the |
| TimeShift | The maximum shift to a keyframe \nafter the end time in milliseconds | hard-wrap leading-the term:ms |
| TimeShift | The maximum shift to a keyframe \nafter the start time in milliseconds | hard-wrap leading-the term:ms |
| TimeShift | The maximum shift to a keyframe \nbefore the end time in milliseconds | hard-wrap leading-the term:ms |
| TimeShift | The maximum shift to a keyframe \nbefore the start time in milliseconds | hard-wrap leading-the term:ms |
| Video | %1 event(s), %2 bitmap(s), %3 Mpx | title-case |
| Video | %1 · Wheel: volume · Right-click: output device | title-case |
| Video | Move video to selected line on: | colon |
| Video | On moving to another line play: | colon |

### Messages (154)

| Context | Source | Findings |
|---|---|---|
| Automation | Error loading Lua script: %1\n%2 | hard-wrap |
| Automation | The last run's macro is no longer loaded, so it can't be repeated. | quotes |
| Automation | The line being edited can't be committed, so the macro can't start. | quotes term:apply |
| Automation | The macro's changes could not be applied; nothing was changed. | quotes |
| Automation | The protected reference can't be changed. | quotes |
| Automation | The script has no such macro | leading-the |
| Automation | The script or its macro changed | leading-the |
| Automation | The script's helper ended; reload the script to use it again. | quotes |
| Automation | Validation Lua script '%1' failed | quotes |
| Colours | The desktop's screenshot portal is not in use here. | quotes |
| Conversion | %n line(s) get the conversion style "%1". | quotes |
| Conversion | In line %1, value '%2' cannot be scaled\nin tag '%3' | quotes hard-wrap |
| Conversion | This document can't be converted to that format. | quotes |
| Documents | "%1" has no time | quotes |
| Documents | "%1" is not a time | quotes |
| Documents | Associated files:\n | colon whitespace |
| Documents | Audio:  | colon whitespace |
| Documents | Keyframes:  | colon whitespace |
| Documents | The edited line cannot be committed. | term:apply |
| Documents | the elements are nested more than %1 deep at "%2" | quotes leading-the |
| Documents | the end %1 is earlier than the begin | leading-the term:the-start |
| Documents | the text is not UTF-8 | leading-the |
| Documents | the time %1 is outside the times of the element around it | leading-the |
| Documents | the word at %1 starts before the word before it | leading-the |
| Documents | the XML is broken at "%1" | quotes leading-the |
| Documents | Video from directory:\n | colon whitespace |
| Documents | Video:  | colon whitespace |
| Fonts | \nWill not be copied. | whitespace |
| Fonts |  tabs:  | colon whitespace |
| Fonts | Added font "%1" to the archive. | quotes |
| Fonts | Cancelled; nothing was written.\n | whitespace |
| Fonts | Cannot check the characters in font "%1". | quotes |
| Fonts | Cannot copy %1.\n | whitespace |
| Fonts | Cannot copy font "%1". | quotes |
| Fonts | Cannot find "%1" font in Fonts folder. | quotes title-case |
| Fonts | Cannot get the contents of font "%1". | quotes |
| Fonts | Cannot open file "%1". | quotes |
| Fonts | Cannot retrieve the font file sizes and names;\ncopying will be canceled.\n | spelling:cancelled hard-wrap whitespace |
| Fonts | Cannot save font named " | quotes |
| Fonts | Cannot zip font "%1". | quotes |
| Fonts | Characters "%1" were drawn by the fallback font "%2". | quotes |
| Fonts | Characters drawn by a fallback font of this computer: %1.\n | whitespace |
| Fonts | Characters no font has: %1.\n | whitespace |
| Fonts | Checked by the subtitle renderer (%1).\n | whitespace |
| Fonts | Completed Successfully, %1 %2.\n | whitespace title-case |
| Fonts | Completed, copied %1 fonts.\nFailed to copy %2 fonts. | hard-wrap |
| Fonts | Copied font "%1". | quotes |
| Fonts | Different fonts with the same file name: %1. Only one of them is kept in a folder.\n | whitespace |
| Fonts | Families answered by a font of another name: %1.\n | whitespace |
| Fonts | Families the renderer did not find: %1.\n | whitespace |
| Fonts | Finished, %1 %2.\n | whitespace |
| Fonts | Font "%1" belongs to a style\nthat is not used. | quotes hard-wrap |
| Fonts | Font "%1" does not contain characters: "%2". | quotes |
| Fonts | Font "%1" is missing bold italics. | quotes |
| Fonts | Font "%1" is missing bold. | quotes |
| Fonts | Font "%1" is missing italics. | quotes |
| Fonts | Font "%1" is missing normal style. | quotes |
| Fonts | Font named "%1".\n | quotes whitespace |
| Fonts | Font not found " | quotes |
| Fonts | Found "%1" font file. | quotes |
| Fonts | Found font "%1"\n | quotes whitespace |
| Fonts | Found font "%1". | quotes |
| Fonts | In lines:  | colon whitespace |
| Fonts | In styles:\n | colon whitespace |
| Fonts | Not found %1.\n | whitespace |
| Fonts | Ready to add %1 to the archive "%2".\n | quotes whitespace |
| Fonts | Ready to copy %1 to "%2".\n | quotes whitespace |
| Fonts | Retrieved sizes and names of %1 fonts, elapsed time %2ms.\n\n | whitespace |
| Fonts | Saved a font named " | quotes |
| Fonts | Some fonts do not contain all glyphs used in the text.\n | whitespace |
| Fonts | Tab %1: the renderer could not read this document; it was not checked.\n | whitespace |
| Fonts | Tab %1: with the collected fonts alone, %2 of %3 frames differ.\n | whitespace |
| Fonts | The collected fonts alone reproduce every frame.\n | whitespace |
| Fonts | The output does not hold every collected font under its own name: the collection is incomplete.\n | whitespace |
| Fonts | The output is labelled incomplete ("%1").\n | quotes whitespace |
| Fonts | The renderer also used "%1" (%2); it is not collected. | quotes |
| Fonts | The renderer cannot reproduce the subtitles from these fonts alone: the collection is incomplete.\n | whitespace |
| Fonts | The subtitle renderer is not available; the fonts cannot be checked.\n | whitespace |
| Fonts | The video shows the subtitles through %1, whose output this check does not verify.\n | whitespace |
| Fonts | This collection is incomplete. It is written only when you acknowledge that, and the output is then labelled incomplete.\n | whitespace |
| Fonts | Video: %1\n\n | whitespace |
| Fonts | Without catalog | spelling:catalogue |
| Fonts | Writing was cancelled.\n | whitespace |
| Hotkeys | Shortcut "%1" is invalid | quotes |
| Hotkeys | The shortcut %1 of "Enable / Disable editor" was removed: the editor is always shown; hide panels or choose a layout instead. | quotes |
| Hotkeys | The shortcut %1 of "Open blank 2h30m audio" was removed: the command is retired and a dummy video brings its own silent audio. | quotes |
| Hotkeys | The shortcut %1 of "Open shortcut mapping window" was removed: the window is gone and each macro's shortcut is set in the Automation panel. | quotes |
| Hotkeys | The shortcut %1 of "Open video with FFMS2" was removed: the command is retired and videos are always indexed. | quotes |
| Hotkeys | The shortcut %1 of "View all" was removed: the View arrangements are retired; choose a layout from the layout switcher instead. | quotes |
| Hotkeys | The shortcut %1 of "View audio and subtitles" was removed: the View arrangements are retired; choose a layout from the layout switcher instead. | quotes |
| Hotkeys | The shortcut %1 of "View only subtitles" was removed: the View arrangements are retired; choose a layout from the layout switcher instead. | quotes |
| Hotkeys | The shortcut %1 of "View only video" was removed: the View arrangements are retired; choose a layout from the layout switcher instead. | quotes |
| Hotkeys | The shortcut %1 of "View video and subtitles" was removed: the View arrangements are retired; choose a layout from the layout switcher instead. | quotes |
| Hotkeys | This hotkey already exists for "%1".\nWhat to do? | quotes hard-wrap |
| Hotkeys | This shortcut already exists in another window as a shortcut for "%1".\nWhat would you like to do? | quotes hard-wrap |
| Hotkeys | This shortcut already exists in other windows as a shortcut for "%1".\nWhat would you like to do? | quotes hard-wrap |
| Karaoke | No karaoke tag's duration starts with a digit, as in \k50, so the audio editor splits the line anew | quotes |
| Karaoke | Syllables don't match the line | quotes |
| Karaoke | The audio editor splits at every \k tag and reads its number its own way, so a tag such as \keep, \kfo50 or \k(50) gives it syllables unlike playback's | quotes leading-the |
| Karaoke | The syllables last %1 s; the line lasts %2 s | leading-the |
| MainWindow | This portable copy keeps its settings in %1. An earlier version's settings are still in %2: to use them, quit HikariSub, copy hikari.ini from there and delet… | quotes |
| MisspellReplacer | Cannot change rules\nwhen the find results window is open | hard-wrap |
| MisspellReplacer | Cannot replace "%1" with "%2" using rule "%3" in line %4. | quotes |
| MisspellReplacer | Line %1 cannot be replaced,\nbecause it was edited. | hard-wrap |
| MisspellReplacer | Rule "%1" is invalid. | quotes |
| Search | Find all, Find in all open subtitles and Find in subtitles list their matches here; check the ones to change and replace them. | term:Grid |
| Search | Invalid regular expression '%1': %2 | quotes |
| Session | "closed" is not true or false. | quotes |
| Session | "tabs" is not a list. | quotes |
| Session | Auto save folder is empty | term:autosave |
| Session | Cannot open auto save folder | term:autosave |
| Session | Tab %1: "%2" has the wrong type. | quotes |
| Spelling | Capitalization and case | spelling:capitalis |
| Spelling | Failed to initialize spell checker. | spelling:initialis term:spellchecker |
| Spelling | Names of places, organizations and products keep their standard capitalization. | spelling:organis |
| Spelling | No dictionary files were found in the "%1" folder.\nSpell checking will be disabled | quotes hard-wrap |
| Spelling | The grammar checker (harper-ls) stopped: %1 | leading-the |
| StatusBar |  (%1 hidden) | whitespace |
| Styles | Copy of  | whitespace |
| Styles | Style named "%1" already exists. | quotes |
| Styles | Styles used:\n%1\nStyles deleted:\n%2 | hard-wrap title-case |
| Styles | The catalog “%1” no longer exists. | spelling:catalogue |
| TagList | Gradient of border color | spelling:colour |
| TagList | Gradient of primary color | spelling:colour |
| TagList | Gradient of secondary color | spelling:colour |
| TagList | Gradient of shadow color | spelling:colour |
| TagList | PNG mask of border color | spelling:colour |
| TagList | PNG mask of primary color | spelling:colour |
| TagList | PNG mask of secondary color | spelling:colour |
| TagList | PNG mask of shadow color | spelling:colour |
| TagList | Secondary color | spelling:colour |
| TagList | Transparency gradient of border color | spelling:colour |
| TagList | Transparency gradient of primary color | spelling:colour |
| TagList | Transparency gradient of secondary color | spelling:colour |
| TagList | Transparency gradient of shadow color | spelling:colour |
| TagList | Transparency of border color | spelling:colour |
| TagList | Transparency of primary color | spelling:colour |
| TagList | Transparency of secondary color | spelling:colour |
| TagList | Transparency of shadow color | spelling:colour |
| Video | Cannot add Direct Sound renderer | title-case |
| Video | Cannot change YCbCr matrix | title-case |
| Video | Cannot create VideoSource. | title-case |
| Video | Cannot enumerate Direct Sound pins | title-case |
| Video | Failed to initialize DirectShow | spelling:initialis |
| Video | Frame %1 of %2  %3 | whitespace |
| Video | Frame time; frame number; frames from the line's start frame; milliseconds from the line's start and end | quotes |
| Video | No IMediaTypes | title-case |
| Video | the file could not be read | leading-the |
| Video | the file has no usable video track | leading-the |
| Video | The file is not a valid video file or is corrupted,\nor codecs or a splitter may be missing | hard-wrap leading-the |
| Video | the media helper ended | leading-the |
| Video | the video could not be opened | leading-the |
| Workspace | the docking engine could not restore it | leading-the |
| Workspace | the file is larger than %1 bytes | leading-the |
