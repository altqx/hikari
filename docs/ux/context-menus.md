# Context menus and ⋯ menus (CX1 step 1)

Inventory for [#557](https://github.com/altqx/hikari/issues/557), read from source at `main` 3c9f13341. It covers every right-click menu and every ⋯ ("More") menu. The menu bar is out of scope; its names come from `tests/ui/menu_bar.txt`. Usage counts from AN1/PostHog were not pulled in this pass.

How to read it:

- Every app menu is a `ShellMenu` (`src/ui/ShellMenu.qml`) with `ShellMenuItem` rows (`src/ui/ShellMenuItem.qml`), unless the entry says otherwise.
- `|` marks a separator. `▸` marks a submenu. `[x]` marks a check item, `(•)` a radio-style check item.
- `{dyn}` marks a list built from data (recent files, streams, suggestions and so on). It counts as one item.
- Icon roles are in parentheses. IC1's rule (`docs/ux/menu-icon-gaps.md`, "Items left without an icon") exempts check items and entries named by their data, so **no-icon** is only raised for other items.
- Keys: "bound" means the item shows the key bound in Settings › Hotkeys. "Hard-coded" means the key is written into the translatable string.
- Counting: an item is an action, a check item or a submenu entry. Separators, headings, sliders and the grammar message row are not counted. Submenu contents count toward the total.

Finding tags: **legacy-name**, **flat-list**, **destructive-next-to-common**, **inconsistent-name**, **wrong-target**, **toggle-hides-state**, **missing-expected**, **no-icon**.

---

## Right-click menus

### R1. Subtitle grid: rows, empty space and header

Opens on a right press anywhere in the grid viewport except a group description row: `src/ui/line_grid.cpp:553-564` emits `contextMenuRequested`, and `Main.qml:4893` pops `gridMenu`. A press on an unselected row selects it first (`line_grid.cpp:561-562`). A press on the header gets `rowAt() == -1` (`line_grid.cpp:44-50`) and opens the same menu. Defined at `src/ui/Main.qml:4944-5257`.

| # | Item | Command | Enabled when / notes | Icon |
| --- | --- | --- | --- | --- |
| 1 | Insert ▸ | | | add |
| 1.1 | Insert before | GRID_INSERT_BEFORE | | insert-before |
| 1.2 | Insert after | GRID_INSERT_AFTER | | insert-after |
| 1.3 | Insert before with video time | GRID_INSERT_BEFORE_VIDEO | video loaded | insert-before |
| 1.4 | Insert after with video time | GRID_INSERT_AFTER_VIDEO | video loaded | insert-after |
| 1.5 | Insert before with video frame time | GRID_INSERT_BEFORE_WITH_VIDEO_FRAME | video loaded | insert-before |
| 1.6 | Insert after with video frame time | GRID_INSERT_AFTER_WITH_VIDEO_FRAME | video loaded | insert-after |
| 2 | Duplicate lines | GRID_DUPLICATE_LINES | key bound | duplicate |
| 3 | Swap | GRID_SWAP_LINES | | swap-lines |
| 4 | Join lines | GRID_JOIN_LINES | | join-lines |
| 5 | Join lines and keep first | GRID_JOIN_TO_FIRST_LINE | | join-keep-first |
| 6 | Join lines and keep last | GRID_JOIN_TO_LAST_LINE | | join-keep-last |
| 7 | Make times continuous with the previous line | GRID_MAKE_CONTINOUS_PREVIOUS_LINE | | continuous-previous |
| 8 | Make times continuous with the next line | GRID_MAKE_CONTINOUS_NEXT_LINE | | continuous-next |
| 9 | Split lines ▸ | | | split-lines |
| 9.1 | Split line at video time | GRID_SPLIT_BY_VIDEO_TIME | video loaded | split-at-video |
| 9.2 | Split lines into frames | GRID_SPLIT_BY_FRAME | video loaded | split-frames |
| 9.3 | Split lines into characters | GRID_SPLIT_BY_CHARS | | split-characters |
| 9.4 | Split lines into words | GRID_SPLIT_BY_WORDS | | split-words |
| 9.5 | Split lines by wraps | GRID_SPLIT_BY_WRAPS | | split-wraps |
| 10 | Select all lines visible on video | GRID_SELECT_VISIBLE_LINES | | select-visible |
| 11 | Make line group | GRID_TREE_MAKE | | make-tree |
| 12 | Show subtitles preview | GRID_SHOW_PREVIEW | `canShowPreview()`; key bound | reference |
| 13 | Paste translation text | GRID_PASTE_TRANSLATION | `canPasteTranslation()`; opens a file dialog | paste-translation |
| 14 | Dialogue shifting window | GRID_TRANSLATION_DIALOG | `canShiftTranslation()` | translation-shift |
| 15 | Hide selected lines | GRID_HIDE_SELECTED | | hide |
| 16 | Filtering ▸ | | | filter |
| 16.1 | [x] Filter after loading subtitles | GRID_FILTER_AFTER_SUBS_LOAD | ASS columns | – |
| 16.2 | [x] Reverse filtering | GRID_FILTER_INVERT | | – |
| 16.3 | [x] Do not reset previous filtering | GRID_FILTER_DO_NOT_RESET | | – |
| 16.4 | Hide lines with styles ▸ {dyn [x] style names} | | ASS columns | styles |
| 16.5 | [x] Hide selected lines | GRID_FILTER_BY_SELECTIONS | | – |
| 16.6 | [x] Hide comments | GRID_FILTER_BY_DIALOGUES | ASS columns | – |
| 16.7 | [x] Show unconfirmed | GRID_FILTER_BY_DOUBTFUL | translation mode | – |
| 16.8 | [x] Show untranslated | GRID_FILTER_BY_UNTRANSLATED | translation mode | – |
| 16.9 | Filter | GRID_FILTER | | filter |
| 16.10 | Turn off filtering | GRID_FILTER_BY_NOTHING | grid filtered | filter-off |
| 17 | [x] Ignore filtering in some actions | GRID_FILTER_IGNORE_IN_ACTIONS | | – |
| 18 | Hide columns ▸ | | | columns |
| 18.1 | [x] Hide empty columns | | then `\|` | – |
| 18.2–18.12 | [x] Hide layer, start time, end time, actor, style, left margin, right margin, vertical margin, effect, characters per second, line wraps | GRID_HIDE_LAYER … GRID_HIDE_WRAPS | ASS-only columns need ASS; end needs the end column | – |
| 18.13 | Reset column widths | | after `\|`; widths edited | restore-default |
| 19 | Set new FPS | GRID_SET_NEW_FPS | | fps |
| 20 | Set FPS from video | GRID_SET_FPS_FROM_VIDEO | video loaded | fps-from-video |
| 21 | Copy | GRID_COPY | "Ctrl+C" hard-coded | edit-copy |
| 22 | Cut | GRID_CUT | "Ctrl+X" hard-coded | edit-cut |
| 23 | Paste | GRID_PASTE | "Ctrl+V" hard-coded | edit-paste |
| 24 | Copy columns | GRID_COPY_COLUMNS | | columns-copy |
| 25 | Paste columns | GRID_PASTE_COLUMNS | | columns-paste |
| 26 | Delete text | GLOBAL_REMOVE_TEXT | | clear |
| 27 | Delete lines | GLOBAL_REMOVE_LINES | "Shift+Del" hard-coded; then `\|` | delete |
| 28 | Font collector | GLOBAL_OPEN_FONT_COLLECTOR | ASS columns | font-collector |
| 29 | Load subtitles from an MKV/OGM file | GRID_SUBS_FROM_MKV | Matroska available | extract-subtitles |

Items: 29 top level, 64 in all, 5 submenus, one separator at top level.

Findings:

- **flat-list**: 29 top-level items with one separator, which comes only after item 27 (`Main.qml:5243`). Insert, join, split, timing, filtering, view, FPS, clipboard and file tools all run together.
- **destructive-next-to-common**: Delete text and Delete lines (26, 27) sit straight after Paste columns with no separator (`Main.qml:5240-5242`).
- Copy, Cut and Paste, the most common commands, are items 21–23. MuseScore and Aegisub put them in their own group (see the comparison section).
- **wrong-target**: a right-click on the header opens this row menu (`line_grid.cpp:44-50`, `553-564`). Aegisub opens a column-visibility menu there (`base_grid.cpp:576-590`).
- **wrong-target**: Font collector, Load subtitles from an MKV/OGM file, Set new FPS and Set FPS from video act on the whole file, not on the clicked lines. Filtering and Hide columns change the view.
- **inconsistent-name**: "Hide selected lines" appears twice and runs two different commands. Item 15 is a one-off action (GRID_HIDE_SELECTED, `Main.qml:5086`). Item 16.5 is a filter switch (GRID_FILTER_BY_SELECTIONS, `Main.qml:5140`).
- **legacy-name**: "Dialogue shifting window" doesn't say what it shifts. "Paste translation text" opens a file dialog (`Main.qml:5072-5076`), so it is not a paste. "Ignore filtering in some actions" doesn't say which actions. "Do not reset previous filtering" and "Reverse filtering" are unclear. "Show subtitles preview" doesn't say which preview. "Insert ▸ Insert before" repeats the parent's word.
- **inconsistent-name**: Swap is the only one-word line command; the rest say "lines". Only "Insert &before" and "with &video time" carry mnemonics inside Insert.
- **missing-expected**: there is no keyboard route to this menu. LineGrid has no context-menu event handler, and the grid's `Keys.onPressed` (`Main.qml:4927-4942`) handles neither the Menu key nor Shift+F10. The Video panel, Home, the Styles panel and the Automation panel do (`Main.qml:2851`, `HomePage.qml:357`, `StylesPanel.qml:152`, `AutomationManager.qml:368`).
- Keys: only Duplicate lines and Show subtitles preview show their bound key. Copy, Cut, Paste and Delete lines hard-code theirs in translatable strings (`Main.qml:5236-5242`), so they go wrong after a rebind. The rest show no key, although every item has a command id.

### R2. Subtitle grid: group description row

Opens on a right press on a group's description row (`GroupRole == 1`, `line_grid.cpp:556-559`). Defined at `src/ui/Main.qml:4904-4918`. Folded generated lines and closed group members get R1.

1. Add lines (tree-add-lines)
2. Copy line group (edit-copy)
3. Change description (edit)
4. Select the group's lines (tree-select)
5. Delete (delete)

Items: 5, no separators, no hotkeys, no command ids.

- **legacy-name** and **destructive-next-to-common**: "Delete" only dissolves the group. `removeGroup` clears each member's group marker and keeps the lines (`src/application/grid_groups.cpp:147-161`). The label reads like "delete these lines", and it sits next to Select with no separator.
- **inconsistent-name**: R1 calls the same object "Make line group" (GRID_TREE_MAKE), and the icon roles say "tree".
- **missing-expected**: there is no fold/unfold item here, although the row toggles on click (`onGroupToggleRequested`, `Main.qml:4896`). The Aegisub fork lists fold create, toggle and clear in its grid menu (`default_menu.json` `grid_context`).

### R3. Line editor and Original text: text fields

`RoleField` (`src/ui/Main.qml:2182`) backs `lineText` (Original text) and `translationText` (Line editor) at `Main.qml:4769` and `4787`. The menu is `ContextMenu.menu` at `Main.qml:2464-2575`. `ContextMenu.onRequested` (`2446-2451`) reads the grammar issue or misspelling under the pointer, and only in the spell-checked field (`spelled`, `2381`).

1. *(grammar only)* message row: the rule's text and "Grammar · group" (disabled display row, spellchecker icon)
2. {dyn} suggestions, in bold (no icon; exempt) |
3. Ignore this rule (hide). Grammar only; its right column shows "Turns off <group>" where keys normally go (`2519-2526`)
4. Add “word” to dictionary (dictionary-add). Grammar issue with a word |
5. Copy (edit-copy). Needs a selection
6. Cut (edit-cut). Needs a selection and an editable field
7. Paste (edit-paste). Editable field |
8. [x] Spellchecker (spellchecker). Spell-checked field only
9. Add word "word" to dictionary (dictionary-add). Misspelling only
10. Delete (delete). Needs a selection and an editable field (`2568-2573`)

Items: 9 (suggestions count as one).

- **missing-expected**: there is no Undo, Redo or Select all, although every other text field has them (R4). There is no "Split line at cursor", although EDITBOX_SPLIT_LINE exists (`Main.qml:7083`) and Aegisub's edit box has it (`subs_edit_ctrl.cpp:419-425`).
- The order is Copy, Cut, Paste. Aegisub, Qt and MuseScore all use Cut, Copy, Paste.
- Delete is cut off from the clipboard group: it comes last, after the Spellchecker switch and the dictionary item.
- **inconsistent-name**: the two dictionary items are worded differently in one menu: `Add “%1” to dictionary` (curly quotes, `2531`) and `&Add word "%1" to dictionary` (straight quotes, an extra "word", a mnemonic; `2559`).
- The hotkey column of Ignore this rule holds a description, not a key.
- No item shows its key. Copy, Cut and Paste have standard keys.

### R4. Every other text field: Qt's Fusion menu

Every `TextField` and `TextArea` except RoleField uses Qt's own Fusion menu. That includes the panel filters, Search, Style manager, Template editor code, Automation and the dialogs. `src/ui/style/TextField.qml` and `TextArea.qml` wrap `F.TextField` and `F.TextArea`. Qt's `Fusion/TextField.qml:28` sets `ContextMenu.menu: TextEditingContextMenu`, defined at `Fusion/impl/TextEditingContextMenu.qml` in the Qt 6.12 SDK (`hikari-sdk-612/qt-6.12.0-linux/6.12.0/gcc_64/qml/QtQuick/Controls/`).

Undo, Redo | Cut, Copy, Paste, Delete | Select All. Items: 7.

- **inconsistent-name**: this is a second text menu with a different order and look from R3. It is a Fusion `Menu`, not a ShellMenu, so it has no status-bar help and none of the Hikari menu styling.
- **no-icon**: its icons are freedesktop theme names (`impl/CopyAction.qml`: `icon.name: "edit-copy"`), not IconTheme roles. They only appear where a system icon theme supplies them.

### R5. Time and number fields: no menu

`TimeField.qml:80-85` and `NumberField.qml:77` swallow the context-menu event with an empty `ContextMenu.onRequested`. A right-drag in a time field steps the value instead (`TimeField.qml:43-77`).

- **missing-expected**: you can't copy or paste a time by mouse. The Start, End and Duration fields are the main users.

### R6. Line editor: tag button

Opens on a right-click on a custom tag button (`LineEditorStrip.qml:262-265`). Defined at `LineEditorStrip.qml:285-322`.

Edit... (edit), Move left (move-left), Move right (move-right) | Remove (remove). Items: 4.

- **inconsistent-name**: "Edit..." uses three dots. The other context menus use "…" (for example "Edit…" in R13).

### R7. Line editor: tag list popup row

Opens on a right-click on a row of the tag autocomplete list (`TagListPopup.qml:90-104`). Defined at `TagListPopup.qml:109-135`.

[x] Show description, [x] Show all tags, [x] Show VSFilterMod tags. Items: 3.

- These options change the list, not the clicked tag. That is acceptable for a list-options menu, but nothing in the list says a right-click opens it.

### R8. Video panel and full-screen video

One component, `src/ui/VideoContextMenu.qml:6-294`. The windowed copy is at `Main.qml:2941-2951` and opens on a right release over the picture (`video_view_controller.cpp:201-202`, `Main.qml:2905-2910`) or the Menu key (`Main.qml:2851`). The full-screen copy is at `VideoFullscreen.qml:491-500` (prefix `fullscreenMenu`). Bound keys are shown with `keys()` (video context 3).

1. Copy video position (copy-position). VIDEO_COPY_COORDS; windowed only
2. Play / Pause, label flips (media-play / media-pause). VIDEO_PLAY_PAUSE; needs video
3. Stop (media-stop). VIDEO_STOP; while playing
4. Full screen / Exit full screen (fullscreen / fullscreen-exit). VIDEO_FULL_SCREEN; key falls back to "F" or "Escape"
5. {dyn} Open in full screen on monitor N / Switch full screen to monitor N (monitor). One per extra monitor
6. Full screen bar ▸ (show-toolbar). Key shown only in full screen. Contents are R8a
7. Open editor (editor). Enabled only in full screen
8. Recently opened subtitles ▸ {dyn} (recent-subtitles)
9. Recently opened videos ▸ {dyn} (recent-video)
10. Open video (open-video). GLOBAL_OPEN_VIDEO
11. Open subtitles (open-subtitles). GLOBAL_OPEN_SUBS
12. Show / hide progress bar (progress-bar). VIDEO_HIDE_PROGRESS_BAR; enabled only in full screen
13. Show / hide render cost (render-cost). GLOBAL_VIDEO_RENDER_COST
14. Change aspect ratio (aspect-ratio). VIDEO_ASPECT_RATIO
15. Save frame with subtitles as PNG (frame-snapshot-subtitles). VIDEO_SAVE_SUBBED_FRAME_TO_PNG
16. Copy frame with subtitles to clipboard (frame-copy-subtitles)
17. Save frame as PNG (frame-snapshot)
18. Copy frame to clipboard (frame-copy) |
19. Unload video (close-video). VIDEO_DELETE_FILE; video loaded
20. Filters ▸ {dyn} (video-filters). Only when the player has filters |
21. {dyn} (•) streams |
22. {dyn} (•) chapters, with their times in the key column

Items: 22 at top level, 28 in all.

- **flat-list**: 18 items before the first separator (1–18), covering playback, full screen, files, view toggles and snapshots.
- **toggle-hides-state**: "Show / hide progress bar" and "Show / hide render cost" are plain actions with no check mark.
- Items 7 and 12 sit disabled in the windowed menu. They only mean something in full screen.
- **wrong-target**: Recently opened subtitles and Open subtitles open a subtitle file from the video picture's menu.
- **inconsistent-name**: "Open video" and "Open subtitles" open dialogs but have no "…". The menu bar has "Open video…" and "File › Open…" (`tests/ui/menu_bar.txt`). The menu bar's "Play / Pause" is "Play" or "Pause" here.
- **missing-expected**: there is no way to reset video zoom or pan. The menu bar has "Turn off video zoom" (GLOBAL_RESET_VIDEO_ZOOM). Aegisub's video menu ends with reset pan (`default_menu.json` `video_context`).
- Streams and chapters appear as unlabelled check groups at the end, with no heading. The menu bar puts them under "Streams ▸" and "Chapters ▸" (`Main.qml:1742-1746`).

#### R8a. Full screen bar ▸ (FullscreenBarMenu)

`src/ui/FullscreenBarMenu.qml:6-30`. A submenu of R8. It is also the full-screen bar's mode button menu (`VideoFullscreen.qml:508-512`, opened at `:467`).

(•) Auto, (•) Always, (•) Never. The longer meanings go to status help only: "Show on pointer movement, hide when idle" and so on. Items: 3.

- **legacy-name**: Auto, Always and Never only make sense with the help text, which the menu doesn't show.

### R9. Full-screen speaker: audio output

Opens on a right-click on the full-screen speaker button (`VideoFullscreen.qml:451-454`). Uses `AudioOutputMenu` (`src/ui/AudioOutputMenu.qml:7`, instance `VideoFullscreen.qml:501-507`). With `audio === null`, the volume slider, speed row and heading are hidden.

{dyn} (•) output devices | Audio settings… (settings-audio). Items: 2.

- **inconsistent-name**: the "Output device" heading shows only in the Audio panel's copy. Full screen lists the devices with no heading.
- The device rows start at a hand-counted index (`firstDevice: 5`, `AudioOutputMenu.qml:14`).

### R10. Panel ⋯ menu and panel tab right-click (panel options)

One menu, `panelOptionsMenu` at `src/ui/Main.qml:665-788`. It opens from:

- the ⋯ button in a panel's tab header (`DockTabBar.qml` → `Docking.requestMenu`, `Main.qml:657-663`);
- a right press on the tab header (`DockTabBar.qml:298-312`);
- Shift+F10 or the Menu key on a focused tab (`DockTabBar.qml:52-62`).

The panel's own items come first (`panelOwnItems`, `Main.qml:604-639`; `panelOwnMenus`, `641-650`), then:

| Panel | Own items |
| --- | --- |
| Video | (•) Show times, (•) Show frames (frames available); Seek on ▸ (video-to-start-time) with 6 (•) choices; Then play ▸ (play-line) with 4 (•) choices (`VideoFollowChoices.qml:12-16`) |
| Reference | Compare with reference (compare) |
| Automation | Load script… (automation), Refresh autoload scripts (refresh), Shortcuts in Settings… (hotkeys), Clear runs (remove) |
| Spellchecker | [x] Ignore comments, [x] Ignore words in capitals, Words added to dictionary… (dictionary) |
| Grid | [x] Fold generated lines |
| Audio, Line editor, Original text, Search, Log, Styles, Fix minor errors, Shift times, Template editor | none |

Then `|` Move panel… (panel-move), Move tab left (tab-move-left), Move tab right (tab-move-right) (both only with 2+ tabs), Undock / Dock (panel-float / panel-dock), Close (tab-close), or "Close reference" for the Reference panel.

Items: 5 shared + 23 panel-owned = 28 (5–19 shown at a time).

- **wrong-target**: Show times / Show frames sits in the Video panel's menu but switches the Line editor's time fields (`LineEditorController::showFrames`, `src/ui/line_editor_controller.h:84`).
- **legacy-name**: "Seek on" and "Then play" are sentence fragments. Settings uses the full labels "Move video to selected line on:" and "On moving to another line play:" (`VideoFollowChoices.qml:10-11`).
- **inconsistent-name**: here it is "Undock" and "Close". View › Panels › *X* calls the same actions "Float" and "Hide" (`Main.qml:1994-2030`; `menu_bar.txt` lists the submenus).
- **missing-expected**: the Audio and Line editor panels, which have many options, have no own items here. Their options live in toolbar ⋯ menus (M3, M4) instead.
- **destructive-next-to-common**: the Automation panel's Clear runs has no separator from the other own items.

### R11. Document tabs

Opens on a right-click on a tab (`DocumentTabBar.qml:228-232`) or on the empty tab bar, with `tabIndex = -1` (`DocumentTabBar.qml:27-31`). Defined at `src/ui/DocumentTabMenu.qml:5-89`, with the submenu at `src/ui/SubtitleComparisonMenu.qml:5-123`.

1. {dyn} (•) open tabs, one per document (exempt)
   |
2. Save (save). The clicked tab, when it can be saved
3. Save all (save-all)
4. Close all tabs (tab-close-all)
5. {dyn} folder items (show-in-folder): "Open the folder containing the subtitles", "Open video containing folder", "Open audio containing folder", "Open keyframes containing folder"
6. Subtitle comparison ▸ (compare): [x] Compare by times, [x] Compare by visible lines, [x] Compare by selections, [x] Compare by styles, [x] Compare by selected styles ▸ {dyn style checks}, Compare (compare), Turn off comparison (compare-off)

Items: 6 at top level, 14 in all.

- **missing-expected**: there is no Close for the clicked tab, no Close other tabs, no Copy path and no New tab. The only close here is Close all.
- **destructive-next-to-common**: Close all tabs sits between Save all and the folder items with no separator (`DocumentTabMenu.qml:56-67`).
- **legacy-name** and **inconsistent-name**: "Open video containing folder" (and the audio and keyframes versions) is broken English, and it differs from "Open the folder containing the subtitles" next to it. Home calls the same action "Show in folder" (R15).
- **wrong-target**: a right-click on one tab still offers Save all and Close all.
- The submenu starts with a "Compare by …" check list. The command that actually runs the comparison is the second-to-last item.

### R12. Title bar

A right press on the custom title bar's empty area (`TitleBarMoveArea.qml:20-25`) or on the app icon (`MainTitleBar.qml:288-292`) calls `MainWindowFrame::showWindowMenu` (`src/ui/window_frame.cpp:546-555`). That shows the OS window menu: Win32 `trackSystemMenu` (`window_frame_win.cpp:50-57`), the Wayland shell surface's `showWindowMenu` (`window_frame.cpp:224-237`), or the X11 equivalent. It holds no app items, and is counted as a menu with 0 app items.

- Not app-defined, so it is fine as it is. The layout switcher and the ⋯ "More menus" are separate (M8 and "Other menus found").

### R13. Styles panel: style row

Opens on a right-click on a style row (`StylesPanel.qml:217-224`). Defined at `src/ui/StylesPanel.qml:438-497`.

1. Apply to the selected lines (style-apply). "Enter" hard-coded |
2. Edit… (edit)
3. Duplicate (duplicate)
4. Delete (delete). "Del" hard-coded |
5. Select lines with this style (select-lines)
6. Copy to the catalog… (copy-to-storage) |
7. Open in Style manager (styles)

Items: 7.

- **inconsistent-name**: the panel's own ⋯ menu (M6) calls these "Edit style" and "Style manager", against "Edit…" and "Open in Style manager" here. M6 has no Select lines or Copy to the catalog; this menu has no New style.
- **destructive-next-to-common**: Delete sits in one group with Edit and Duplicate.

### R14. Automation panel: macro row

Opens on a right-click on a macro row (`AutomationManager.qml:914-920`). Defined at `src/ui/AutomationManager.qml:1185-1210`.

Run (media-play), Set shortcut… (hotkeys), Clear shortcut (clear). Items: 3.

- **missing-expected**: a right-click on a script header opens nothing. Its menu (M9) is only on the ⋯ button (`AutomationManager.qml:847-850`).
- Clear shortcut has no separator from Set shortcut…. The risk is small.

### R15. Home page: recent file row

Opens on a right-click on a recent row (`HomePage.qml:420-427`). Defined at `src/ui/HomePage.qml:534-575`.

Open (open-subtitles; "Enter" hard-coded; not for missing files), Show in folder (show-in-folder), Copy path (edit-copy) | Remove from list (remove; "Del" hard-coded), Remove missing files (remove; when any are missing). Items: 5.

- This is the cleanest menu in the app: common items first, destructive items behind a separator.
- **inconsistent-name**: "Show in folder" here against R11's folder wording.

### R16. Reference tray

Opens on a right press in the reference grid (`ReferenceTray.qml:94-98`, through `LineGrid`). Defined at `src/ui/ReferenceTray.qml:136-153`.

{dyn} (•) occurrences, labelled "Title (row length)" (`src/app/application_reference.cpp:296-299`). Items: 1.

- **wrong-target**: the list comes from the editing grid's active line, not the clicked reference row (`application_reference.cpp:277-293`).
- **legacy-name**: "Title (12 3)" doesn't say what the numbers are.
- **missing-expected**: there is no Copy, and no copying to the edited subtitles. When nothing matches, an empty menu opens.

---

## ⋯ menus

### M1. Panel ⋯

The same menu as R10. Counted once.

### M2. Floating panel window ⋯

The ⋯ button in a floating window's title bar (`DockTitleBar.qml:44-71`, used by KDDW through `docking.cpp:172`).

Dock (panel-dock), Close (tab-close). Items: 2.

- **inconsistent-name** with R10: no own items, no Move panel…, and "Close" without the Reference special case. The menu R10 opens from inside the same window's tab header has them.

### M3. Audio panel "More audio options"

The ⋯ on the audio toolbar when the toolbar folds (`Main.qml:4116-4123`). Defined at `src/ui/Main.qml:4124-4320`. Rows only appear for controls that folded out.

Folded plays (7, keys bound: Play before the mark … Play to the end), Automatically apply changes [x], Go to the next line after applying changes [x], Go to selection (AUDIO_GOTO, key bound) | Line timing (•), Karaoke (•), [x] Auto split, Split syllable at the cursor, Switch long syllables between \k and \kf, [x] Tap to time (AUDIO_TAP), Tap options | Waveform (•), Spectrum (•) | [x] Auto-scroll, [x] Sync with video, [x] Speech (non-linear spectrum).

Items: up to 22.

- **inconsistent-name**: one setting has three names across the audio menus:
  - "Automatically apply changes" (here and the Commit menu, `Main.qml:3848`) and "Auto-commit" (toolbar toggle `3873`, Customise `4503`);
  - "Go to the next line after applying changes" (`3860`, `4170`), "Next after commit" (`3886`) and "Go to the next line after commit" (Customise, `4506-4509`);
  - "Line" (toolbar, `3919`) and "Line timing" (`4192`);
  - "Speech" (`4103`) and "Speech (non-linear spectrum)" (`4312`).
- **no-icon**: Tap options (`Main.qml:4256-4263`) is a plain action with no icon.
- Tap options closes this menu and opens another one at the same place.
- The first group can reach 10 items: the 7 plays and 3 commit items have no separator between them.

### M4. Line editor strip "More"

The ⋯ at the end of the line editor's tag strip when groups fold (`LineEditorStrip.qml:337-348`). Defined at `src/ui/LineEditorStrip.qml:358-505`, with Alignment at `506-525`. Bound keys go in `shortcutText`.

Font selection (tag-font), 4 colours (colour-*) | Paste all, Paste the selected text, Comment out original, [x] Not confirmed, [x] Tags from original | Alignment ▸ (9 (•) positions in numpad order) | {dyn} custom tag buttons, Add a tag button (add).

Items: up to 13 at top level, 22 in all.

- The colour buttons on the strip act on a right-click (picker in dropper mode, `LineEditorStrip.qml:131-135`, `Main.qml:6685-6693`). Their rows here only do the left-click action.

### M5. Original text "More"

`OriginalTextTools.qml:103-160`.

Paste all, Paste the selected text, Comment out original | [x] Not confirmed, [x] Tags from original. Items: 5.

- It has the same items as M4's translation group, under the same names. Consistent.

### M6. Styles panel "More"

`StylesPanel.qml:384-436`, shown when the tool strip folds.

Apply to the selected lines (style-apply) | New style (add), Edit style (edit), Duplicate (duplicate), Delete (delete) | Style manager (styles). Items: 6.

- **inconsistent-name** with R13 (see above).
- **destructive-next-to-common**: Delete sits next to Duplicate.

### M7. Visual tool rail "More"

`VisualToolRail.qml:76-112`. {dyn} the tools that folded out, with tool icons. Items: 1.

- **toggle-hides-state** and **inconsistent-name**: the tools are mutually exclusive, but this menu shows no check on the active one. The full-screen Tools menu checks it (`VideoFullscreen.qml:514-535`).

### M8. Title bar "More menus"

`MainTitleBar.qml:305-348`. {dyn} the titles of the menu-bar menus that no longer fit. Picking one opens that menu at the same place. Items: 1.

- **no-icon**: the entries are menu titles with no icon. They are arguably "menus themselves", which IC1 exempts.

### M9. Automation panel: script ⋯

`AutomationManager.qml:845-850` (button), `1151-1183` (menu).

Reload (refresh), Edit script (edit) | Unload (unload). Items: 3. The states that disable each item are at `1163-1180`.

- Reload is also a separate button on the same row (`AutomationManager.qml:838-843`).

### M10. Style manager: list "More" (catalog and document lists)

`StyleManager.qml:1226-1262`, one copy per list.

Move to top (move-to-top), Move to bottom (move-to-bottom) | Sort by name (sort), Delete unused styles (delete; document list only). Items: 4 (3 in the catalog).

- **destructive-next-to-common**: Delete unused styles shares a group with Sort by name.
- **missing-expected**: style rows in the Style manager have no right-click menu, while the Styles panel's rows do (R13). The only right-click in the Style manager is on a colour field, which opens the colour dropper (`StyleManager.qml:981-984`).

### M11. Automation script dialog ⋯

`AutomationDialog.qml:211-292`.

Last used (dialog-memory) | Presets ▸ (save-as: {dyn} presets | Save as preset… (save-as), Delete preset ▸ (delete: {dyn} presets)) | Reset to the script’s values (refresh). Items: 7.

- Presets and Save as preset… use the same icon.

### M12. Shapes dialog ⋯ and M13. All tags dialog ⋯

`ShapesEditionDialog.qml:424-445` and `AllTagsEditionDialog.qml:652-673`.

Restore default shapes… / Restore default tags… (restore-default) | Undo all changes (undo). Items: 2 each.

---

## Other menus found (dropdowns, not audited item by item)

These are menus behind toolbar or field buttons, not right-click or ⋯ menus:

- Audio toolbar split buttons:
  - Play around (`Main.qml:3736-3768`);
  - Commit (`3832-3866`);
  - Karaoke split (`3951-3967`);
  - Tap (`3980-4056`, with Speed ▸ at `4025`);
  - Zoom (`4346-4395`);
  - Volume and output (`AudioOutputMenu`, `4438`);
  - Customise the audio panel (`4456-4520`).
- Full-screen bar: Tools (`VideoFullscreen.qml:514-535`) and bar mode (R8a).
- Layout switcher in the title bar (`LayoutSwitcher.qml:69`, `LayoutMenu.qml`). It is the same as View › Layout.
- Template editor New (`TemplateEditor.qml:875-930`) and Apply (`854-873`).
- Font picker catalog menus (`FontPicker.qml:457`, `494`) and the font catalog bar's Add (`FontCatalogBar.qml:128`).
- Settings "More resets" (`SettingsDialog.qml:244`).
- Visual tool option choices (`VisualToolOption.qml:160`).
- Menu-bar submenus reused elsewhere: `RecentFilesMenu.qml`, `VideoStreamsMenu.qml`, `VideoChaptersMenu.qml`, `MacroMenuEntries.qml`.

### Right-clicks that open no menu

- The colour buttons in the Line editor strip (`LineEditorStrip.qml:131-135`) and the Style manager's colour fields (`StyleManager.qml:981-984`) open the colour picker in dropper mode. Nothing on screen says so. Settings can swap the clicks (`Main.qml:6686`).
- In the Audio display, a right-click sets the line's end time (`audio_display_item.cpp:423`). Aegisub does the same (`audio_display.cpp:1105-1110`).
- Log, Search, Spellchecker, Fix minor errors, Shift times, Template editor list, Style manager list and Automation script header: no right-click handler (there is no `RightButton` in `LogPanel.qml`, `SearchTool.qml`, `SpellCheckerPanel.qml`, `FixMinorErrors.qml`, `ShiftTimesPanel.qml` or `TemplateEditor.qml`). **missing-expected**: Copy, Go to line or Replace on result rows.
- Log entry text is a read-only `TextEdit` (`LogPanel.qml:196-199`). It is not a Control, so it gets no Fusion menu either. You can select text but can't copy it by mouse; the panel has a Copy button for all shown entries (`LogPanel.qml:109-112`).

---

## Comparison

### Aegisub (arch1t3cht fork, `feature` at 2cdf00436, and upstream TypesettingTools `master`)

Sources:

- https://github.com/arch1t3cht/Aegisub/blob/feature/src/libresrc/default_menu.json
- https://github.com/arch1t3cht/Aegisub/blob/feature/src/base_grid.cpp
- https://github.com/arch1t3cht/Aegisub/blob/feature/src/subs_edit_ctrl.cpp
- https://github.com/arch1t3cht/Aegisub/blob/feature/src/video_display.cpp
- https://github.com/arch1t3cht/Aegisub/blob/feature/src/audio_display.cpp
- upstream https://github.com/TypesettingTools/Aegisub/blob/master/src/libresrc/default_menu.json

Menus by area:

- **Grid rows** (`grid_context`, opened at `base_grid.cpp:576-581`): Insert (before), Insert (after), Insert at video time (before), (after) | Duplicate, split before, split after (`edit/line/split/before`, `/after`) | Swap, Join (concatenate), Join (keep first), Join (as Karaoke) | Make times continuous (change start), (change end), Recombine | Create audio clip | *fork only:* fold create, toggle, clear | Cut, Copy, Paste, Paste over | Delete. That is 26 items in 7 groups upstream and 29 in 8 groups in the fork. Delete is alone at the end.
- **Grid header** (`base_grid.cpp:582-589`): a check list of hideable columns. Upstream has the same logic (`base_grid.cpp:552`).
- **Edit box** (`subs_edit_ctrl.cpp:386-428`, `445-473`), in this order:
  1. spelling suggestions, or "No correction suggestions";
  2. Add "word" to dictionary, or Remove "word" from dictionary, plus Spell checker language ▸ |
  3. thesaurus ▸ and Thesaurus language ▸ |
  4. Cut, Copy, Paste | Select All |
  5. Split at cursor (preserve times), Split at cursor (estimate times), split at video frame.
- **Video** (`video_context`, opened at `video_display.cpp:418-421`): the frame save and copy commands in three pairs (`video/frame/save`/`copy`, `…/raw`, `…/subs`) | Copy coordinates, reset pan (`video/pan_reset` in the fork, `video/reset_pan` upstream). That is 8 items in 4 groups.
- **Audio**: no menu. A right-click goes to the timing controller (`audio_display.cpp:1105-1110`).

Takeaways for Hikari: Aegisub's grid menu is as long as ours but grouped: every pair or family gets its own group, clipboard items come together, and Delete is last and alone. Its header has its own menu. Its edit box keeps Select All and the split commands, and puts spelling first, like R3.

### MuseScore 4 (`main` at e8eabe149; `muse_framework` at 7a2787499)

Sources:

- https://github.com/musescore/MuseScore/blob/main/src/notationscene/qml/MuseScore/NotationScene/notationcontextmenumodel.cpp
- https://github.com/musescore/muse_framework/blob/main/framework/dockwindow/qml/Muse/Dock/dockpanelview.cpp
- https://github.com/musescore/MuseScore/blob/main/src/palette/qml/MuseScore/Palette/internal/palettespanelcontextmenumodel.cpp
- https://github.com/musescore/MuseScore/blob/main/src/playback/qml/MuseScore/Playback/mixerpanelcontextmenumodel.cpp

Menus by area:

- **Score element** (`makeElementItems`, lines 284-309): Cut, Copy, Paste, Swap with clipboard, Delete (`makeDefaultCopyPasteItems`, 116-127), then Select ▸ (Similar, Similar in staff, More…), then | Edit element, then | Style…. While text is being edited, the menu stops after the clipboard group (`isTextEditingStarted`, 288-290).
- **Measure** (129-163): Cut, Copy, Paste, Swap | Clear, Delete | staff properties | Insert measures ▸, Move measures ▸, Make into system | Measure properties.
- **Panel tab menu** (`dockpanelview.cpp:58-71`): the panel's own items | Close, Undock or Dock.
- **Palettes panel** (lines 47-56): three toggles | Expand all, Collapse all.
- **Mixer ⋯** (lines 152-157): Playback setup, View ▸ (section checks).

Takeaways for Hikari:

- The clipboard group always comes first, and destructive items get their own group.
- "Select ▸" is a submenu, not loose items.
- Properties and style come last.
- The panel menu matches R10's shape: own items, separator, then window items. MuseScore's window part is only Close and Undock/Dock, and it says "Close", where our View menu says "Hide".

---

## Cross-menu inconsistencies

| Concept | Menu A | Menu B (and C) |
| --- | --- | --- |
| Play/pause | Menu bar "Play / Pause" | Video R8 "Play" / "Pause" |
| Open video | Menu bar "Open video…" | Video R8 "Open video" (no …) |
| Open subtitles | Menu bar "File › Open…" | Video R8 "Open subtitles" |
| Show file in folder | Home R15 "Show in folder" | Tabs R11 "Open the folder containing the subtitles" / "Open video containing folder" |
| Float a panel | Panel ⋯ R10 "Undock" | View › Panels › *X* "Float" |
| Close a panel | Panel ⋯ R10 "Close" | View › Panels › *X* "Hide"; floating ⋯ M2 "Close" without "Close reference" |
| Edit a style | Styles row R13 "Edit…" | Styles ⋯ M6 "Edit style" |
| Open Style manager | Styles row R13 "Open in Style manager" | Styles ⋯ M6 and menu bar "Style manager" |
| Edit an item | Styles row R13 "Edit…" | Tag button R6 "Edit..." (three dots) |
| Auto-commit | Audio Commit menu and M3 "Automatically apply changes" | Audio toolbar toggle and Customise "Auto-commit" |
| Next line after commit | Commit menu and M3 "Go to the next line after applying changes" | Toolbar "Next after commit"; Customise "Go to the next line after commit" |
| Line timing mode | Audio toolbar "Line" | M3 "Line timing" |
| Non-linear spectrum | Audio toolbar "Speech" | M3 "Speech (non-linear spectrum)" |
| Add to dictionary | R3 grammar "Add “x” to dictionary" | R3 spelling "Add word "x" to dictionary" |
| Hide selected lines | R1 item 15 (GRID_HIDE_SELECTED, action) | R1 Filtering › "Hide selected lines" (GRID_FILTER_BY_SELECTIONS, check) |
| Line groups | R1 "Make line group" | R2 "Copy line group", "Delete" (which only ungroups); icon roles say "tree" |
| Text editing menu | R3 Copy, Cut, Paste … Delete (no Undo, Redo, Select all) | R4 Undo, Redo, Cut, Copy, Paste, Delete, Select All (Fusion, not ShellMenu) |
| Active tool shown | Full-screen Tools menu checks the active tool | Rail ⋯ M7 shows no check |
| Hotkeys shown | Video R8, M3, M4: bound keys | Grid R1: 2 bound, 4 hard-coded, the rest none; R13 and R15 hard-code "Enter" and "Del"; R3 none |
| Ellipsis | "…" (U+2026) almost everywhere | "Edit..." (R6); menu bar "Shift times...", "Import legacy settings..." |

## Counts

Rule: actions, check items and submenu entries count; each `{dyn}` list counts as one; separators, headings and display rows don't count.

| Menu | Items |
| --- | --- |
| R1 Grid rows/header | 64 |
| R2 Group row | 5 |
| R3 Editor text fields | 9 |
| R4 Other text fields (Qt Fusion) | 7 |
| R5 Time/number fields | 0 (no menu) |
| R6 Tag button | 4 |
| R7 Tag list popup | 3 |
| R8 Video / full screen (incl. R8a) | 28 |
| R9 Full-screen speaker | 2 |
| R10 Panel options (= M1) | 28 |
| R11 Document tabs | 14 |
| R12 Title bar (OS menu) | 0 app items |
| R13 Styles row | 7 |
| R14 Automation macro | 3 |
| R15 Home recent row | 5 |
| R16 Reference tray | 1 |
| M2 Floating window ⋯ | 2 |
| M3 Audio More | 22 |
| M4 Line editor More | 22 |
| M5 Original text More | 5 |
| M6 Styles More | 6 |
| M7 Visual tools More | 1 |
| M8 Title bar More menus | 1 |
| M9 Automation script ⋯ | 3 |
| M10 Style manager More | 4 |
| M11 Automation dialog ⋯ | 7 |
| M12 Shapes ⋯ | 2 |
| M13 All tags ⋯ | 2 |

The total is **28 menus with 257 items**: 16 right-click entries (two of them, R5 and R12, have no app items) and 12 ⋯ menus, with the panel ⋯ counted once. They hold 16 submenus between them. On top of these come 16 dropdown menus listed under "Other menus found".
