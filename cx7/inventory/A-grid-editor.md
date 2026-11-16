# CX7 inventory, area A: Grid and Line editor menus

Read from source at `main` 8dbf88f41 (2026-11-16). Notation as in `docs/ux/context-menus.md` lines 1-16: `|` separator, `▸` submenu, `[x]` check, `(•)` radio, `{dyn}` data list (counts as one). Keys: "bound" = `command` shows the Settings › Shortcuts key; "std" = `standardKey`; "fixed" = `shortcutText` read from a binding (`boundKeys`). Unless noted, files are under `src/ui/`. "Approved" means the CX2 approved design (`context-menus.md` "CX2 approved design", menus 1-9).

The approved design has no 1d122c203 copy of `tests/ui/context_menus.txt` (CX3 created it in c593851da). The "changed since" lists compare against the approved drawings and the listing as CX4 left it (57027a885, PR #680). `TierSaveMenu.qml` does not appear in any menu in this area. It is used only by `SaveLayoutDialog.qml:65`, `SpellCheckerPanel.qml:523` and `TierSaveButton.qml:73`.

---

## Menus

### A-M1. Grid: right-click a row (`gridMenu`)

Right-click a row or empty space, or press the Menu key or Shift+F10 (`LineGrid::requestMenu`, `line_grid.cpp:468-485`, through `MenuKey.onRequested: grid.requestMenu()`, `Main.qml:7336`). Defined at `Main.qml:7401-7715`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| r1 | Keep AI change | GLOBAL_KEEP_AI_CHANGE | shown only when Review is on and the line has a change (`reviewShown`, `7407-7431`); bound | review-keep |
| r2 | Undo AI change | GLOBAL_UNDO_AI_CHANGE | same; bound | undo |
| r3 | Next AI change | GLOBAL_NEXT_AI_CHANGE | same; `review.waiting > 0`; bound | chevron-down |
| r4 | Show in Review panel | GLOBAL_SHOW_AI_REVIEW | same; bound; the menu bar calls it "Show panel" under Edit › Review | show |
| | `\|` | | shown with r1-r4 (`7432-7436`) | |
| 1 | Insert ▸ | | | add |
| 1.1 | Before | GRID_INSERT_BEFORE | bound | insert-before |
| 1.2 | After | GRID_INSERT_AFTER | bound; then `\|` | insert-after |
| 1.3 | Before, at video time | GRID_INSERT_BEFORE_VIDEO | video loaded | insert-before |
| 1.4 | After, at video time | GRID_INSERT_AFTER_VIDEO | video loaded | insert-after |
| 1.5 | Before, at video frame time | GRID_INSERT_BEFORE_WITH_VIDEO_FRAME | video loaded | insert-before |
| 1.6 | After, at video frame time | GRID_INSERT_AFTER_WITH_VIDEO_FRAME | video loaded | insert-after |
| 2 | Duplicate %n line(s) | GRID_DUPLICATE_LINES | bound | duplicate |
| 3 | Blur and glow layers… | GLOBAL_BLUR_AND_GLOW_LAYERS | bound | layers |
| 4 | Build layers with the last values | GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT | bound | layers-repeat |
| 5 | Typesetting graph… | GLOBAL_TYPESETTING_GRAPH | `!layoutLocked`; no `reason` | node-editor |
| 6 | Cut | GRID_CUT | bound, else std | edit-cut |
| 7 | Copy | GRID_COPY | bound, else std | edit-copy |
| 8 | Paste | GRID_PASTE | bound, else std | edit-paste |
| 9 | Paste special ▸ | | | paste-special |
| 9.1 | Copy columns | GRID_COPY_COLUMNS | bound | columns-copy |
| 9.2 | Paste columns | GRID_PASTE_COLUMNS | bound; then `\|` | columns-paste |
| 9.3 | Paste translation from a file… | GRID_PASTE_TRANSLATION | `canPasteTranslation()`; `help` = translationHint | paste-translation |
| 10 | AI ▸ (InlineAiMenu, `7524-7531`) | | shown when `inlineAi.on`; see A-M11 | ai-assistant |
| | `\|` | | | |
| 11 | Join ▸ | | | join-lines |
| 11.1-11.3 | Join, Keep first, Keep last | GRID_JOIN_LINES, GRID_JOIN_TO_FIRST_LINE, GRID_JOIN_TO_LAST_LINE | bound | join-lines, join-keep-first, join-keep-last |
| 12 | Split ▸ | | | split-lines |
| 12.1 | At video time | GRID_SPLIT_BY_VIDEO_TIME | video loaded | split-at-video |
| 12.2 | Into frames | GRID_SPLIT_BY_FRAME | video loaded | split-frames |
| 12.3 | [x] Bake animations | | setting `grid.splitFramesBakeAnimations`; then `\|` | – |
| 12.4-12.6 | Into characters, Into words, By wraps | GRID_SPLIT_BY_CHARS / _WORDS / _WRAPS | bound | split-characters, split-words, split-wraps |
| 12.7 | At tag blocks | GRID_SPLIT_AT_TAG_BLOCKS | bound; then `\|` | split-tag-blocks |
| 12.8 | Every N frames or seconds… | GRID_SPLIT_EVERY_N | bound | split-every-n |
| 13 | Remove tags… | GLOBAL_REMOVE_TAGS | `removeTagsAction`; document open | remove-tags |
| 14 | Interpolate across lines… | GLOBAL_INTERPOLATE_LINES | ≥ 3 lines selected; why it is disabled is only in `help` | interpolate-lines |
| 15 | Swap lines | GRID_SWAP_LINES | bound | swap-lines |
| 16 | Make times continuous ▸ | | | continuous-next |
| 16.1-16.2 | With the previous line, With the next line | GRID_MAKE_CONTINOUS_PREVIOUS_LINE / _NEXT_LINE | bound | continuous-previous, continuous-next |
| 17 | Make line group | GRID_TREE_MAKE | bound | make-tree |
| 18 | Convert Aegisub folds to line groups | GLOBAL_CONVERT_FOLDS_TO_GROUPS | `canConvertFolds()`, greyed otherwise | convert-folds |
| | `\|` | | | |
| 19 | Line breaks ▸ (`LineBreaksMenu.qml`) | | | line-break-insert |
| 19.1-19.4 | Insert balanced break, Move break left, Move break right, Remove breaks | GLOBAL_TEXT_INSERT_BALANCED_BREAK / _MOVE_BREAK_LEFT / _MOVE_BREAK_RIGHT / _REMOVE_BREAKS | bound; then `\|` | line-break-insert, -left, -right, -remove |
| 19.5 | [x] Wrap only at \N (\q2) | GLOBAL_TEXT_WRAP_AT_BREAKS_ONLY | checked from the selection | – |
| 20 | Change case ▸ (`ChangeCaseMenu.qml`) | | | change-case |
| 20.1-20.4 | lowercase, UPPERCASE, Title Case, Sentence case | GLOBAL_TEXT_LOWERCASE / _UPPERCASE / _TITLE_CASE / _SENTENCE_CASE | bound; then `\|` | case-lower, case-upper, case-title, case-sentence |
| 20.5-20.6 | Full-width, Half-width | GLOBAL_TEXT_FULL_WIDTH / _HALF_WIDTH | bound | fullwidth, halfwidth |
| 21 | Swap dialogue alternatives | GLOBAL_SWAP_DIALOGUE_ALTERNATIVES | bound | dialogue-swap |
| | `\|` | | | |
| 22 | Change alignment, keep position ▸ (`ChangeAlignmentMenu.qml`) | | | change-alignment |
| 22.1-22.9 | Top left (\an7) … Bottom right (\an3), numpad order | GLOBAL_ALIGNMENT_KEEP_POSITION_AN1-9 | bound | alignment-1…9 |
| 23 | Align and distribute ▸ (`AlignDistributeMenu.qml`) | | heading "Relative to" | align-center |
| 23.1-23.2 | (•) Active line, (•) Video frame | | setting `grid.alignToVideoFrame`; then `\|` | – |
| 23.3-23.5 | Align left edges, Align centres, Align right edges | GLOBAL_ALIGN_LEFT_EDGES / _CENTRES / _RIGHT_EDGES | bound; then `\|` | align-left, align-center, align-right |
| 23.6-23.8 | Align top edges, Align middles, Align bottom edges | GLOBAL_ALIGN_TOP_EDGES / _MIDDLES / _BOTTOM_EDGES | bound; then `\|` | align-top, align-middle, align-bottom |
| 23.9-23.10 | Distribute horizontally, Distribute vertically | GLOBAL_DISTRIBUTE_HORIZONTALLY / _VERTICALLY | bound | distribute-horizontal, distribute-vertical |
| 24 | Position to margins | GLOBAL_POSITION_TO_MARGINS | bound | pos-to-margins |
| 25 | Margins to position | GLOBAL_MARGINS_TO_POSITION | bound | margins-to-pos |
| | `\|` | | | |
| 26 | Select lines visible on video | GRID_SELECT_VISIBLE_LINES | bound | select-visible |
| 27 | Show subtitles preview | GRID_SHOW_PREVIEW | `canShowPreview()`; bound | reference |
| 28 | Translation matching… | GRID_TRANSLATION_DIALOG | `canShiftTranslation()`; bound | translation-shift |
| | `\|` | | | |
| 29 | Delete text | GLOBAL_REMOVE_TEXT | bound | clear |
| 30 | Delete %n line(s) | GLOBAL_REMOVE_LINES | bound | delete |
| | `\|` | | | |
| 31 | Add QC note… | GLOBAL_ADD_QC_NOTE | `qcNotes.editable`; bound | qc-notes |

Items: 31 top level (35 while Review is on), 91 in all (95), 10 submenus, 6 top-level separators (7 while Review is on). Approved menu 1 had 16 top-level rows (18 with AU18's two).

### A-M2. Grid: right-click the column header (`gridHeaderMenu`)

`LineGrid::headerMenuRequested` → `Main.qml:7335`. The menu is `GridColumnsMenu`, defined at `Main.qml:1780-1835`, with the instance at `7716-7720`. There is no keyboard route here, because the header takes no focus. Grid ⋯ › Columns ▸ (A-M3) covers it.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1-11 | [x] Layer, Start time, End time, Actor, Style, Left margin, Right margin, Vertical margin, Effect, Characters per second, Line wraps | GRID_HIDE_LAYER … GRID_HIDE_WRAPS | ticked = shown; ASS-only columns need ASS; End time needs the end column; then `\|` | – |
| 12 | [x] Hide empty columns | | `shell.autoHideColumns` | – |
| 13 | Reset column widths | | widths edited | restore-default |

Items: 13, 0 submenus, 1 separator. Matches approved menu 2.

### A-M3. Grid ⋯ (panel options, Grid)

This menu opens from the Grid's ⋯, a right-click on its tab, or the Menu key on the tab (`Docking.menuRequested` → `panelOptionsMenu.openFor`, `Main.qml:1514-1520`). Defined at `Main.qml:1522-1778`. Filtering ▸ (`1847-1936`) and Columns ▸ (`1937-1942`) are inserted at the top only for `gridDock` (`1543-1556`). The Grid's own items are at `1450-1457`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Filtering ▸ | | | filter |
| 1.1 | Filter | GRID_FILTER | bound | filter |
| 1.2 | Turn off filtering | GRID_FILTER_BY_NOTHING | Grid filtered; bound; then `\|` | filter-off |
| 1.3 | [x] Hide selected lines (filter) | GRID_FILTER_BY_SELECTIONS | | – |
| 1.4 | [x] Hide comments | GRID_FILTER_BY_DIALOGUES | ASS | – |
| 1.5 | Hide lines with styles ▸ {dyn [x] style names} | | ASS | styles |
| 1.6 | [x] Show unconfirmed | GRID_FILTER_BY_DOUBTFUL | Translation mode | – |
| 1.7 | [x] Show untranslated | GRID_FILTER_BY_UNTRANSLATED | Translation mode; then `\|` | – |
| 1.8 | [x] Reverse filtering | GRID_FILTER_INVERT | | – |
| 1.9 | [x] Filter after loading subtitles | GRID_FILTER_AFTER_SUBS_LOAD | ASS | – |
| 1.10 | [x] Do not reset previous filtering | GRID_FILTER_DO_NOT_RESET | | – |
| 2 | Columns ▸ | | = A-M2 rows 1-13 | columns |
| 3 | [x] Ignore filtering in some actions | GRID_FILTER_IGNORE_IN_ACTIONS | own item | – |
| 4 | [x] Fold generated lines | | own item | – |
| | `\|` | | (the empty own-tail separator is hidden) | |
| 5 | Move panel… | | dockable, not locked | panel-move |
| 6 | Move to the left side bar / Move to the right side bar | | shown when the panel can sit in a side bar (DK3) | side-bar-left / side-bar-right |
| 7 | Move out of the side bar | | shown in a side bar | panel-move |
| 8 | Move left / Move right | | shown in a side bar with 2+ panels | tab-move-left / tab-move-right |
| 9 | Move tab left / Move tab right | | shown with 2+ tabs | tab-move-left / tab-move-right |
| 10 | Float / Dock | | by state | panel-float / panel-dock |
| | `\|` | | | |
| 11 | [x] Lock layout | GLOBAL_TOGGLE_LOCK_LAYOUT | `reason: lockReason` | layout-lock |
| | `\|` | | (the About row and its separator are plugin-only) | |
| 12 | Close | | closable, not locked | tab-close |

Items: 8 top level always (Filtering, Columns, two own items, Move panel…, Float, Lock layout, Close), up to 15 with the side bar and tab rows. 32 in all (39 at most). 3 submenus, 3 visible separators.

### A-M4. Grid: right-click a line group's description row (`groupMenu`)

Right-click a group description row (`groupMenuRequested`, `Main.qml:7342-7346`), or press the Menu key or Shift+F10 on it (`line_grid.cpp:481-482`). Defined at `Main.qml:7347-7375`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Select the group’s lines | | | tree-select |
| 2 | Add %n selected line(s) | | | tree-add-lines |
| 3 | Copy line group | | | edit-copy |
| 4 | Change description… | | opens a popover | edit |
| 5 | Convert Aegisub folds to line groups | GLOBAL_CONVERT_FOLDS_TO_GROUPS | `canConvertFolds()`, greyed otherwise; bound | convert-folds |
| | `\|` | | | |
| 6 | Ungroup | | | remove |

Items: 6, 0 submenus, 1 separator.

### A-M5. Line editor and Original text: text fields (`RoleField` `ContextMenu.menu`)

Right-click in `lineText` or `translationText` (`Main.qml:7164`, `7194`), or press the Menu key or Shift+F10. These go through Qt Quick's ContextMenu. While the tag list is open, the Menu key opens the tag list options instead (`4694-4695`). Defined at `Main.qml:4708-4892`. The marks under the pointer are read at `4690-4693`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 0 | *grammar message row* | | grammar mark only; disabled display row | spellchecker |
| 1 | {dyn} suggestions (bold) | | spelling or grammar mark | – (data) |
| | `\|` | | grammar only | |
| 2 | Ignore this rule | | grammar only; detail "Turns off <group>" | hide |
| 3 | Add “%1” to dictionary | | a word is marked; detail "This project" / "Mine" when a Project is open | dictionary-add |
| 4 | Add to my dictionary / Add to project dictionary | | Project open and row 3 shown; the other tier | mine / project |
| | `\|` | | any mark | |
| 5 | Undo | (std Undo) | line, editable | undo |
| 6 | Redo | (std Redo); then `\|` | line, editable | redo |
| 7 | Cut | std | selection, editable | edit-cut |
| 8 | Copy | std | selection | edit-copy |
| 9 | Paste | std | editable | edit-paste |
| 10 | Delete | std; then `\|` | selection, editable | clear |
| 11 | Select all | std | text not empty | select-all |
| | `\|` | | **hidden when the field is read-only** (`4834`) | |
| 12 | Line breaks ▸ | | = A-M1 19; disabled when read-only | line-break-insert |
| 13 | Change case ▸ | | = A-M1 20; disabled when read-only | change-case |
| 14 | Swap dialogue alternatives | GLOBAL_SWAP_DIALOGUE_ALTERNATIVES | disabled when read-only; bound | dialogue-swap |
| | `\|` | | | |
| 15 | Blur and glow layers… | GLOBAL_BLUR_AND_GLOW_LAYERS | line; bound | layers |
| 16 | Remove tags… | GLOBAL_REMOVE_TAGS | `removeTagsAction`; bound | remove-tags |
| 17 | Phantom ▸ (`PhantomMenu.qml`) | | disabled when read-only | phantom |
| 17.1 | Keep the start aligned | GLOBAL_PHANTOM_KEEP_START | bound | align-left |
| 17.2 | Keep the end aligned | GLOBAL_PHANTOM_KEEP_END | bound | align-right |
| | `\|` | | with AI ▸ | |
| 18 | AI ▸ | | `inlineAi.on`, `role === 0`, line; see A-M11 | ai-assistant |
| | `\|` | | spell-checked field | |
| 19 | [x] Check spelling | | spell-checked field (`spelled`); `app.spellingOn` | spellchecker |

Items: 19 top level at most (15 with no mark and no Project), 40 in all, 4 submenus (Line breaks, Change case, Phantom, AI), up to 8 separators. Approved menu 5 had 9 rows.

### A-M6. Every other text field (`TextMenu.qml`)

The style's TextField and TextArea (`style/TextField.qml:9-12`, `style/TextArea.qml:9-12`) and editable ComboBoxes (`style/ComboBox.qml:66-68`) take this menu from `TextMenus.menuFor` (`style/text_menus.cpp:31-52`). It is defined at `TextMenu.qml:1-17`, with the rows at `TextMenuItem.qml:1-31`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Undo | std | editable, canUndo | undo |
| 2 | Redo | std; then `\|` | editable, canRedo | redo |
| 3 | Cut | std | editable, selection | edit-cut |
| 4 | Copy | std | selection | edit-copy |
| 5 | Paste | std | editable, canPaste | edit-paste |
| 6 | Delete | std; then `\|` | editable, selection | clear |
| 7 | Select all | std | text not empty | select-all |

Items: 7, 0 submenus, 2 separators. Matches approved menu 6. Read-only fields grey rows out (6.2).

### A-M7. Time and number fields

`TimeField.qml:44-94`, `NumberField.qml:35-87`. A right-click that does not move opens A-M6 on release, and a right-drag scrubs. The Menu key reaches the style TextField's ContextMenu, because the swallowing item is visible only while the right button is held. This matches approved menu 7, and has the same 7 items as A-M6.

### A-M8. Line editor: custom tag button (`tagButtonMenu`)

Right-click a custom tag button, or press the Menu key on it while it has Tab focus (`LineEditorStrip.qml:262-266`). Defined at `LineEditorStrip.qml:290-332`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Edit… | | | edit |
| 2 | Move left | | can move (mirrored in RTL) | move-left |
| 3 | Move right | | can move; then `\|` | move-right |
| 4 | Remove | | | remove |

Items: 4, 1 separator. Matches approved menu 8.

### A-M9. Tag autocomplete list options (`optionsMenu`)

Opens from the list's ⋯ ("Tag list options", panel-menu, `TagListPopup.qml:41-53`), a right-click on a row (`120-131`), or the Menu key in the Line editor while the list is open (`Main.qml:4694-4695`). Defined at `TagListPopup.qml:135-163`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | [x] Show description | | | – |
| 2 | [x] Show all tags | | | – |
| 3 | [x] Show VSFilterMod tags | | | – |

Items: 3. Matches approved menu 9.

### A-M10. Line editor strip "More" (`moreMenu`, M4)

The ⋯ (menu-more, tooltip "More") at the end of the tag strip, shown when a group folds (`LineEditorStrip.qml:341-354`). Defined at `LineEditorStrip.qml:372-518`. Alignment ▸ is at `520-540`, and its names come from `AlignmentChoice.qml:13-17`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Choose font… | (fixed: EDITBOX_CHANGE_FONT) | look group folded; editable | tag-font |
| 2-5 | {4} Primary colour, Secondary colour for karaoke, Border colour, Shadow colour | (fixed: EDITBOX_CHANGE_COLOR_*) | look group folded; then `\|` | colour-primary/-secondary/-outline/-shadow |
| 6 | Paste all | (fixed: EDITBOX_PASTE_ALL_TO_TRANSLATION) | translation group folded | paste-translation |
| 7 | Paste the selected text | (fixed: EDITBOX_PASTE_SELECTION_TO_TRANSLATION) | same | paste-selection-translation |
| 8 | Comment out original | (fixed: EDITBOX_HIDE_ORIGINAL) | same | comment-original |
| 9 | [x] Unconfirmed | | same | unconfirmed |
| 10 | [x] Tags from original | | same; then `\|` | moving-tags |
| 11 | Text position ▸ | | alignment group folded | alignment-* (current) |
| 11.1-11.9 | [x] Top-left (an7) … Bottom-right (an3), numpad order | | **check items, not (•) radio**; then `\|` | alignment-1…9 |
| 12 | {dyn} custom tag buttons | (fixed: EDITBOX_TAG_BUTTONn) | custom group folded | button's icon |
| 13 | Add a tag button | | custom group folded, below the maximum; opens the tag button editor | add |

Items: up to 13 at top level (with the 4 colours counted separately, as CX1 did), 22 in all, 1 submenu, up to 3 separators.

### A-M11. AI ▸ (`InlineAiMenu.qml`)

`InlineAiMenu.qml:6-71`. It is used as Grid › AI ▸ (`Main.qml:7524`), Line editor text › AI ▸ (`4870`), on its own from the GLOBAL_AI_ACTIONS shortcut in the Grid (`gridAiHotkeyMenu`, `7326-7334`, through `openInlineAiMenu`, `9969-9977`), and from the strip's "AI actions" button (`editorAiMenu`, `7130-7137`).

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Rewrite… | | chat model; detail "%n line(s)" in the Grid | edit |
| 2 | Shorten to %1 CPS | | chat model | compact |
| 3 | Fix spelling and grammar | | reviewer model | dictionary |
| 4 | Translate to %1 | | translator model | language |
| 5 | Translate to… | | translator model | language |
| 6 | {dyn} custom actions | | per role; then `\|` | ai-assistant |
| 7 | Repeat last AI action | GLOBAL_REPEAT_AI_ACTION | a last action; detail = its label when unbound | repeat-run |
| 8 | Edit actions… / Add a model in Settings › AI… | | by `ready.any` | settings |

Items: 8, 1 separator. The submenu row is "AI" (`ai-assistant`) and is hidden when AI is off.

### A-M12. Original text "More" (`moreMenu`, M5)

The ⋯ (menu-more, tooltip "More") in the Original text tools, shown when folded (`OriginalTextTools.qml:103-113`). Defined at `OriginalTextTools.qml:114-162`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Paste all | | editable; **no key shown** | paste-translation |
| 2 | Paste the selected text | | editable; no key shown | paste-selection-translation |
| 3 | Comment out original | | editable; no key shown; then `\|` | comment-original |
| 4 | [x] Unconfirmed | | editable | unconfirmed |
| 5 | [x] Tags from original | | | moving-tags |

Items: 5, 1 separator.

**Totals for area A:** 12 menus (A-M7 shares A-M6's rows; A-M11 is shared by four entry points).

---

## Changed since CX2 (1d122c203 / approved design)

- **A-M1 Grid row**
  - Added: Review rows Keep AI change, Undo AI change, Next AI change and Show in Review panel, with a separator, at the top while Review is on. AI2 #430, 356dffd69, PR #798.
  - Added: Blur and glow layers… and Build layers with the last values. AU18 #852, PR #943. The approved doc already lists them.
  - Added: Typesetting graph…, AU9 #844, PR #1025.
  - Added: AI ▸, AI8 cfe68d6df, PR #965.
  - Added inside Split ▸:
    - [x] Bake animations: FA15, ea0bba49d, PR #690.
    - At tag blocks, a separator and Every N frames or seconds…: AU10, a0372fcc2, PR #970.
  - Added: Remove tags…, AU4, d0c0a8da3, PR #928.
  - Added: Interpolate across lines…, AU22, 9b15d9efe, PR #960.
  - Added: Convert Aegisub folds to line groups. FA5 #562, 67ff89071, PR #762.
  - Added a new group: Line breaks ▸, Change case ▸ and Swap dialogue alternatives. AU20, 80e0204e0, PR #954.
  - Added a new group: Change alignment, keep position ▸, Align and distribute ▸, Position to margins and Margins to position. AU21, 067f0b859, PR #1011.
  - Added: a separator and Add QC note… after the deletes. #577, 75e1cd7a2, PR #798.
  - Removed, renamed or re-iconed: none against approved menu 1. The approved groups keep their order. The two new groups sit between "structure" and "view and selection", so the menu grew from 16 (+2 AU18) to 31 top-level rows.
- **A-M2 Grid header:** no change.
- **A-M3 Grid ⋯**
  - Added: Move to the left side bar, Move to the right side bar, Move out of the side bar, and Move left / Move right. DK3 #1040, 8667645f6, PR #1157.
  - Added: [x] Lock layout between two separators. DK4 #657, 517e7d398, PR #762.
  - Otherwise as approved: Filtering ▸ and Columns ▸ first, then Ignore filtering and Fold generated lines, then Float. Close sits alone at the end.
- **A-M4 Group row:** added Convert Aegisub folds to line groups before the separator (FA5 #562, 9fc6a9f6f, PR #762). "Select the group's lines" took a curly apostrophe (text style, 1dac4c228).
- **A-M5 Line editor text**
  - Differs from the approved drawing since CX3: CX3 (c593851da, PR #638) built approved "[x] Spellchecker" as "[x] Check spelling".
  - Added: Add to my dictionary / Add to project dictionary. PJ5d #900, 7e5f2677c, PR #1068.
  - Added: Line breaks ▸, Change case ▸ and Swap dialogue alternatives (AU20, PR #954).
  - Added: Remove tags… (AU4, PR #928).
  - Added: Phantom ▸ (AU21, PR #1011).
  - Added: AI ▸ with its own separators (AI8, PR #965).
  - Blur and glow layers… is AU18 (PR #943) and is in the approved doc.
- **A-M6 / A-M7 text, time and number fields:** no change.
- **A-M8 Tag button:** no change ("Edit…" as approved).
- **A-M9 Tag list:** no change (⋯ "Tag list options" as approved).
- **A-M10 Strip More:** the approved doc says "No change". The text style renamed rows: "Font selection" became "Choose font…", "* color" became "* colour", and "Not confirmed" became "Unconfirmed" (1dac4c228, a8f80546b). Alignment names became "-centre". There are no added or removed rows. The strip also gained an "AI actions" button outside the menu (AI8, PR #965).
- **A-M11 AI ▸:** new menu (AI8, PR #965).
- **A-M12 Original text More:** "Not confirmed" became "Unconfirmed" (text style). There are no added or removed rows.

---

## Findings

**A1. flat-list (Grid row menu is back to 31 top-level rows).** Approved menu 1 cut CX1's 29 rows to 16 in four groups. Wave 8 added 15 top-level rows, 4 of them in a new text group and 4 in a new position group, and now there are 31 (35 with Review), in 7 groups (`Main.qml:7401-7715`). The structure group alone has 8 rows: Join ▸, Split ▸, Remove tags…, Interpolate across lines…, Swap lines, Make times continuous ▸, Make line group, Convert Aegisub folds to line groups.

Recommendation:
- Put the typesetting rows in one submenu, **Typesetting ▸** (icon `layers`):
  - Blur and glow layers…, Build layers with the last values and Typesetting graph…;
  - a separator, then Interpolate across lines…;
  - a separator, then Change alignment, keep position ▸, Align and distribute ▸, Position to margins and Margins to position.
- Put the text rows in a second submenu, **Text ▸** (icon `change-case`): Line breaks ▸, Change case ▸, Swap dialogue alternatives and Remove tags….

The top level then reads:
1. Insert ▸, Duplicate, Cut, Copy, Paste, Paste special ▸, AI ▸
2. Join ▸, Split ▸, Swap lines, Make times continuous ▸, Make line group
3. Text ▸, Typesetting ▸
4. Select lines visible on video, Show subtitles preview, Translation matching…, Add QC note…
5. Delete text, Delete N lines

That is 20 rows. This is an owner decision (it changes the CX2 approval). Visible change: yes.

**A2. destructive-next-to-common (deletes are no longer last).** Add QC note… sits after Delete text and Delete N lines, behind its own separator (`Main.qml:7706-7714`). Approved menu 1 puts the deletes "alone at the end" (the menu's Changes list). Recommendation: move Add QC note… into the "view and selection" group, after Translation matching… (`7695`), so Delete text and Delete N lines end the menu again. Visible change: yes.

**A3. inconsistent-name (one label, two commands: "Check spelling").** The Line editor check item "Check spelling" (`Main.qml:4881-4891`) switches `app.spellingOn`, the underlining while typing. The menu bar's Subtitles › Check spelling (`2586-2591`, GLOBAL_OPEN_SPELLCHECKER, `menu_bar.txt:104`) opens the Spellchecker panel. CX1 raised the same pattern for "Hide selected lines". CX3 also departed from the approved "[x] Spellchecker" (5.1). Recommendation: rename the check item **"Check spelling as you type"**, keeping the `spellchecker` icon, and keep "Check spelling" for the panel command. Visible change: yes.

**A4. inconsistent-name (alignment positions named two ways).** Grid › Change alignment, keep position ▸ uses "Top left (\an7)" … "Middle centre (\an5)" (`ChangeAlignmentMenu.qml:11-13`, AU21). Line editor ⋯ › Text position ▸ and the strip's alignment picker use "Top-left (an7)" … "Centre (an5)" (`AlignmentChoice.qml:13-17`). The positions are the same, but the hyphen, the backslash and the name of the middle cell differ. The `AlignmentChoice` labels are also built by joining `qsTr("Top-left") + " (an7)"`, which text-style.md forbids ("Never build a sentence out of translated pieces"). Recommendation: use one set everywhere, `qsTr("Top left (%1)").arg("\\an7")` … `qsTr("Middle centre (%1)")`. That is, take ChangeAlignmentMenu's strings into AlignmentChoice.names, and leave the icon manifest labels as they are. Visible change: yes.

**A5. toggle-hides-state (Text position ▸ uses check items for a one-of-nine choice).** `LineEditorStrip.qml:526-538` gives each position `checkable: true` with a check mark. CX5 made one-of-N choices into `MenuRadioItem` rows (approved 10-15, "radio rows"). Because `keepsMenuOpen` defaults to `checkable`, picking a position also leaves the menu open, unlike a radio row. Recommendation: make each row a `MenuRadioItem` so it draws (•) and closes the menu. With mixed alignment, `current` is 0, so no row is selected, and that is correct. Visible change: yes.

**A6. missing-expected (Original text ⋯ shows no keys).** OriginalTextTools' Paste all, Paste the selected text and Comment out original (`OriginalTextTools.qml:117-137`) have neither a `command` nor a `shortcutText`. The same three rows in the strip ⋯ show their bound keys (`LineEditorStrip.qml:405-433`). CX1 M5 said the two menus were "consistent", and the approved design says M4 "keeps its bound keys". Recommendation: add `shortcutText: tools.shellRoot.boundKeys("EDITBOX_PASTE_ALL_TO_TRANSLATION", 2)`, and the same for EDITBOX_PASTE_SELECTION_TO_TRANSLATION and EDITBOX_HIDE_ORIGINAL. Visible change: yes.

**A7. inconsistent-name (ellipsis: "Add a tag button").** The strip ⋯ row "Add a tag button" (`LineEditorStrip.qml:486-493`) and the strip's + button (`279`) open the tag button editor, which needs a name and a tag before anything is added. text-style.md puts "…" on a command that "cannot finish without more input from the user", and the tag button's own "Edit…" has it. Recommendation: **"Add a tag button…"** in both places. Visible change: yes.

**A8. inconsistent-name (one AI menu, three names).** The same InlineAiMenu is:
- the submenu "AI" in the Grid and the Line editor (`InlineAiMenu.qml:15`);
- the strip button's tooltip "AI actions" (`LineEditorStrip.qml:366`);
- the shortcut command "AI actions…" (`command_names.cpp:375`).

The shortcut opens a menu, not a window, so text-style.md gives it no "…". Recommendation: name the submenu and the command **"AI actions"** (submenu "AI actions ▸"), without the ellipsis. Visible change: yes.

**A9. inconsistent-name (grouping of the same rows differs between Grid and Line editor).** In the Line editor (`Main.qml:4856-4869`), Remove tags… sits with Blur and glow layers… In the Grid it sits in the structure group (`7607`) and Blur and glow layers… sits next to Duplicate (`7477-7490`). AI ▸ is in the Grid's clipboard group (`7524`), but in the Line editor it is in its own group near the end (`4869-4879`). Line breaks ▸, Change case ▸ and Swap dialogue alternatives form the same group in both, and that part is consistent. Recommendation: after A1, give both menus the same order for the shared rows: Text ▸ (or its rows) | Typesetting rows | AI ▸. In the Grid, AI ▸ moves out of the clipboard group into its own group before the view group. Visible change: yes.

**A10. destructive-next-to-common / grouping (read-only fields lose a separator).** In A-M5, the separator after Select all is `visible: !field.readOnly` (`Main.qml:4834`). Line breaks ▸, Change case ▸ and Swap dialogue alternatives stay shown and are only disabled when the field is read-only (`4835-4851`). So a read-only Line editor shows Select all directly on top of three greyed text rows. Approved 6.2 says read-only fields keep their shape. Recommendation: drop the `visible`/`height` binding on that separator. Visible change: yes, in read-only only.

**A11. wrong-target (whole-document row in two line menus, greyed most of the time).** Convert Aegisub folds to line groups acts on the whole document. It sits in the Grid row menu (`Main.qml:7638-7645`) and in the group row menu (`7365-7372`). It is greyed whenever the file has no Aegisub folds (`canConvertFolds()`), which is almost always. CX1 R1 and CX2 moved whole-file rows out of the row menu ("Where moved rows go"). Recommendation:
- Hide the row instead of greying it (`visible: canConvertFolds()` plus `height: visible ? implicitHeight : 0`), in both menus.
- If it should keep a permanent home, add Subtitles › "Convert Aegisub folds to line groups" to the menu bar.

Owner call. Visible change: yes.

**A12. missing-expected (the reason a row is disabled never shows).**
- Interpolate across lines… explains why it is disabled only in `help` (`Main.qml:7613-7615`: "Select at least three lines…").
- Paste translation from a file… does the same (`7515`, `translationHint`: "Translation needs an ASS file…").

`help` is status-bar text shown on hover, and `ShellMenuItem.qml` says itself that "a disabled row gets no hover, so a tooltip would never show". It added `reason` for this (DK4, used by Lock layout, `Main.qml:1750`). Typesetting graph… is greyed while the layout is locked and has no explanation at all (`7496`). Recommendation:
- Interpolate across lines…: set `reason: root.shell.selectedCount < 3 ? qsTranslate(…"Select at least three lines…") : ""` and keep `help` for the enabled text.
- Paste translation from a file…: set `reason: root.app.translationHint`.
- Typesetting graph…: set `reason: root.layoutLocked ? <lock reason> : ""`.

Visible change: yes.

**A13. inconsistent-name (dictionary rows in the Line editor).** With a Project open, the menu shows:
- "Add “word” to dictionary", with a dimmed detail "This project" or "Mine";
- "Add to my dictionary" or "Add to project dictionary" (`Main.qml:4790-4813`).

The two rows run the same action for two tiers, but word one with the word and no tier and the other with the tier and no word. One shows the tier as "Mine" and the other as "my", and they use different icons (`dictionary-add` against `mine`/`project`). Recommendation: two parallel rows, **Add “word” to project dictionary** and **Add “word” to my dictionary**, both with `dictionary-add`. The default tier comes first, and the detail column is dropped. Without a Project, keep the single "Add “word” to dictionary". Visible change: yes.

**A14. no-icon (one icon, two meanings).** Phantom › Keep the start aligned and Keep the end aligned use `align-left` and `align-right` (`PhantomMenu.qml:16`, `23`). The Grid's Align and distribute › Align left edges and Align right edges use the same icons (`AlignDistributeMenu.qml:39`, `53`). `docs/ux/menu-icon-gaps.md` states "one command, one icon; one icon, one meaning". Recommendation: draw `phantom-keep-start` and `phantom-keep-end`, or use `phantom` on both rows and let the label tell them apart. Visible change: yes.

**A15. inconsistent-name (⋯ tooltip "More").** The strip ⋯ (`LineEditorStrip.qml:345`) and the Original text ⋯ (`OriginalTextTools.qml:110`) are named "More". text-style.md "One name per thing" says the tooltip of ⋯ is "More options", and so does the visual tool rail (`VisualToolBar.qml:255`). The Styles panel, Style manager, Shapes and All tags ⋯ also say "More", so this needs one decision app-wide. Recommendation: **"More options"** on every ⋯ / » button that opens an overflow menu. Visible change: yes.

**A16. missing-expected, uncertain (no keyboard route to the strip ⋯ and Original text ⋯).** Both ⋯ buttons are `focusPolicy: Qt.NoFocus` (`LineEditorStrip.qml:346`, `OriginalTextTools.qml:111`), and neither has a `MenuKey` owner. When groups fold, Tab cannot reach the folded rows that have no shortcut, which are Unconfirmed, Tags from original, Text position and Add a tag button. The tag buttons themselves did get Tab focus in CX4. I have not checked whether those four have another keyboard route, such as the menu bar or command search. Recommendation: give the ⋯ buttons `Qt.TabFocus`, as the tag buttons have. Visible change: no (focus order only).

**A17. missing-expected (ledger gap, not visible).** `context_menus.txt` should list conditional rows "even when they are hidden" ("What CX3 built"). It lists the Grid's Review separator (`Grid > -`, line 62) but not the four Review rows above it. They are fixed commands built by an Instantiator with an inline model (`Main.qml:7413-7431`), not data rows. Recommendation: have the outline test list them, or build them as four static `ShellMenuItem`s with `visible: gridMenu.reviewShown`, and regenerate the listing. Visible change: no.

**A18. inconsistent-name (shortcut list name lacks the ellipsis).** The menu row reads "Add QC note…" (`Main.qml:7711`), but its shortcut name is "Add QC note" (`command_names.cpp:360`). The other "…" rows keep the ellipsis in the shortcut list: "Translation matching…", "Blur and glow layers…", "Remove tags…", "Typesetting graph…" and "Interpolate across lines…". Recommendation: rename the command to **"Add QC note…"** in `command_names.cpp`. Visible change: yes, in Settings › Shortcuts and command search.

**Clean:**
- Grid header (A-M2), the shared text menu (A-M6), time and number fields (A-M7), tag button (A-M8) and tag list options (A-M9) match the approved design.
- No label in area A hard-codes a key or a tab.
- No label carries an "&" mnemonic.
- No "..." or straight quotes.
- Every non-check, non-data row has an icon whose role exists in `icons/manifest.json`.
- Every row with a `command` shows its bound key.
- Ungroup, Remove and Close sit alone after a separator.
