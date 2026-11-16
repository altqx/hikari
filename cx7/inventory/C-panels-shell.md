# CX7 inventory, area C: panels and shell menus

Read from source at `origin/main` 8dbf88f41 (2026-11-16). Notation as in `docs/ux/context-menus.md` lines 1-16. Baseline for "changed": CX2's approved design (menus 16-22, 26-28), built by CX3-CX6 on 1d122c203. The Grid's own panel rows (Filtering ▸, Columns ▸, Ignore filtering, Fold generated lines) belong to area A and are left out.

Counting: action, check item or submenu entry. `{dyn}` = 1. Separators, headings and disabled placeholder rows are counted as the inventory did (placeholders count as items).

---

## 1. Menus

### C-M1. Panel ⋯ and panel tab right-click (panel options)

`panelOptionsMenu`, `src/ui/Main.qml:1522-1778`. Own rows come from `panelOwnItems` (`Main.qml:1423-1471`), `panelOwnMenus` (`1483-1493`), `panelOwnTail` (`1472-1478`) and `pluginPanelItems` (`1479-1482`).

Opens from: the ⋯ in a docked tab header (`DockTabBar.qml:72` → `Docking.requestMenu` → `Main.qml:1514-1520`); a right press on the tab header (`DockTabBar.qml:285-298`); the Menu key or Shift+F10 on a focused header tab or the ⋯ button (`DockTabBar.qml:52-62`, through `MenuKey`); a side bar's header ⋯ and right-click (`SideBar.qml:153-162`); a side bar icon's right-click and Menu key (`SideBar.qml:88-92`). The ⋯ tooltip is "%1 options" (`DockMenuButton.qml:17`).

Shared rows:

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| | {panel's own rows, see below} | | | |
| | `|` (only when the panel has own rows or submenus) | | | |
| 1 | Move panel… | — | dockable, layout unlocked | panel-move |
| 2 | Move to the left side bar | — | shown when the panel may sit in a side bar and isn't in the left one | side-bar-left |
| 3 | Move to the right side bar | — | as 2, right | side-bar-right |
| 4 | Move out of the side bar | — | shown when in a side bar | panel-move |
| 5 | Move left | — | shown with 2+ panels in the side bar; not first | tab-move-left |
| 6 | Move right | — | as 5; not last | tab-move-right |
| 7 | Move tab left | — | shown with 2+ tabs; RTL-aware | tab-move-left |
| 8 | Move tab right | — | as 7 | tab-move-right |
| 9 | Float / Dock | — | label and icon switch; unlocked; Dock needs dockable | panel-float / panel-dock |
| | `|` | | | |
| 10 | [x] Lock layout | GLOBAL_TOGGLE_LOCK_LAYOUT (bound) | disabled while a panel floats, with a reason | layout-lock |
| | `|` | | | |
| 11 | About <plugin>… | — | plugin panel whose plugin is installed; then a `|` follows | about |
| 12 | Close | — | closable, unlocked; Reference: also unloads the reference; missing plugin: forgets the dock | tab-close |

Own rows (Grid's: area A):

| Panel | # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|---|
| Line editor | 13 | (•) Show times | — | frames available; drawn as a check box (ShellMenuItem, `radio` only keeps the menu closing) | — |
| | 14 | (•) Show frames | — | as 13 | — |
| Video | 15 | Move video to the selected line ▸ | — | 6 choices, check boxes (`Main.qml:1617-1640`, `VideoFollowChoices.qml:10-12`) | video-to-start-time |
| | 16-21 | Double-clicking a line (always on), Every line change, Clicking a line or editing when paused, Clicking a line or editing, Editing line when paused, Editing | — | | — |
| | 22 | After moving, play ▸ | — | 4 choices | play-line |
| | 23-26 | Nothing, Audio to the line end time, Video and audio to the line end time, Video and audio to the next line start time | — | | — |
| Reference | 27 | Compare with reference | — | has an editing target | compare |
| Automation | 28 | Load script… | — | loadScriptAction | automation |
| | 29 | Refresh autoload scripts | — | not running | refresh |
| | 30 | Shortcuts in Settings… | — | | hotkeys |
| | | `|` | | | |
| | 31 | Clear runs | — | runs to clear | remove |
| Spellchecker | 32 | [x] Ignore comments | — | | — |
| | 33 | [x] Ignore words in capitals | — | | — |
| | 34 | Words added to dictionary… | — | | dictionary |
| QC notes | 35 | Import notes file… | — | editable | import |
| | 36 | Import notes from the clipboard | — | editable and clipboard has notes; asks Add/Replace when the file has notes | edit-paste |
| | 37 | Export notes… | — | any notes | export |
| | 38 | Mark all as not done | — | editable, some done | qc-done-reset |
| | 39 | Remove all notes… | — | editable, any notes; asks | delete |
| Plugin panel | 40 | {dyn} the plugin's `menu` rows, [x] when the row has `checked` | — | while the plugin runs | — (plugin API has no icon) |
| Audio, Original text, Find, Log, Styles, Shift times, Timing postprocessor, Template editor, Review, Agent, Graph, Node, Source lines, Explorer | | none | | | |

Items: 12 shared + 28 own (Grid excluded) = 40 in all; 2 submenus; separators: own-tail `|` (Automation), own `|`, after Float, after Lock, after About. Visible at once: 5-14.

### C-M2. Floating window title bar ⋯

`dockWindowMenu`, `src/ui/DockTitleBar.qml:57-72`; button `DockTitleBar.qml:44-56` (Tab-focusable; no `MenuKey`). Shown only on a floating window holding 2+ groups (single-panel windows show their tab header and C-M1).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Dock | — | KDDW `floatButtonClicked` | panel-dock |
| | `|` | | | |
| 2 | Close | — | `closeButtonEnabled`; KDDW `closeButtonClicked`, not `root.closePanel` | tab-close |

Items: 2, 0 submenus, 1 separator.

### C-M3. Document tab: right-click a tab

`documentTabMenu`, `src/ui/DocumentTabMenu.qml:5-204`; submenus `SubtitleComparisonMenu.qml:5-112`, `MoveTabMenu.qml:5-36`. Opens on a right-click on a tab and the Menu key / Shift+F10 on the focused tab (`DocumentTabBar.qml:372-381`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Save | — (shows GLOBAL_SAVE_SUBS key) | tab can be saved | save |
| 2 | Save all | — | | save-all |
| | `|` | | | |
| 3 | Show in folder | — | tab with one file | show-in-folder |
| 4 | Show in folder ▸ | — | tab with 2+ files | show-in-folder |
| 5 | {dyn} Subtitles / Video / Audio / Keyframes | — | one per file | open-subtitles / open-video / open-audio / open-keyframes |
| 6 | Copy path | — | tab with one file | copy-path |
| 7 | Copy path ▸ | — | tab with 2+ files | copy-path |
| 8 | {dyn} Subtitles / Video / Audio / Keyframes | — | | as 5 |
| 9 | Subtitle comparison ▸ | — | `menuState.enabled` | compare |
| 10 | Compare | — | can compare | compare |
| 11 | Turn off comparison | — | comparison on | compare-off |
| | `|` | | | |
| 12 | [x] Compare by times | — | | — |
| 13 | [x] Compare by visible lines | — | | — |
| 14 | [x] Compare by selections | — | | — |
| 15 | [x] Compare by styles | — | | — |
| 16 | [x] Compare by selected styles ▸ | — | | styles |
| 17 | {dyn} [x] style names | — | | — |
| 18 | Switch to ▸ | — | | tab-switch |
| 19 | {dyn} (•) tab titles | — | MenuRadioItem | — |
| | `|` | | | |
| 20 | Move tab to new window | GLOBAL_MOVE_TAB_TO_NEW_WINDOW (bound) | a document | tab-to-new-window |
| 21 | Move tab to window ▸ | — (shows GLOBAL_MOVE_TAB_TO_WINDOW key) | another window exists | tab-to-window |
| 22 | {dyn} other windows by current tab title | — | | window-restore |
| | `|` | | | |
| 23 | Close | — (shows GLOBAL_CLOSE_PAGE key) | | tab-close |
| 24 | Close other tabs | — | 2+ tabs | tab-close-others |
| 25 | Close tabs to the right | — | not last | tab-close-right |
| 26 | Close all tabs | — | | tab-close-all |

Items: 15 top level (one of 3/4 and one of 6/7 shown, so 13 at once), 26 in all; 6 submenus; 4 separators.

### C-M4. Document tabs: right-click the empty tab bar

Same component with `tabIndex = -1` (`DocumentTabBar.qml:33-37`). No keyboard route (the bar takes no focus).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | New tab | — (shows GLOBAL_ADD_PAGE key) | | tab-new |
| 2 | Save all | — | | save-all |
| 3 | Switch to ▸ {dyn (•)} | — | | tab-switch |
| | `|` | | | |
| 4 | Move tab to new window | GLOBAL_MOVE_TAB_TO_NEW_WINDOW | acts on the window's current tab (`DocumentTabMenu.qml:35`) | tab-to-new-window |
| 5 | Move tab to window ▸ {dyn} | — | current tab; another window | tab-to-window |
| | `|` | | | |
| 6 | Close all tabs | — | | tab-close-all |

Items: 6 top level, 8 in all; 2 submenus; 2 separators.

### C-M5. Move tab to window (command popup)

`moveTabToWindowPopup`, `DocumentTabBar.qml:52-57` (a `MoveTabMenu`), opened by GLOBAL_MOVE_TAB_TO_WINDOW at the current tab. {dyn} other windows (window-restore). Items: 1.

### C-M6. Title bar right-click

`TitleBarMoveArea.qml:20-25`, app icon `MainTitleBar.qml:316-327` → `MainWindowFrame::showWindowMenu`. OS window menu, 0 app items. Unchanged.

### C-M7. Title bar "More menus" (»)

`titleBarMoreMenu`, `MainTitleBar.qml:337-379`. {dyn} the folded menu-bar titles, no icons (IC1 "menus themselves"). Button is `Qt.NoFocus`, reached through the menu-bar keyboard target. Items: 1.

### C-M8. Home: right-click a recent file

`homeRowMenu`, `RecentList.qml:281-323` (instance `HomePage.qml:344-363`). Right-click a row; Menu key / Shift+F10 via `MenuKey` (`RecentList.qml:126-129`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Open | — ("Enter", fixed) | file not missing | open-subtitles |
| 2 | Show in folder | — | | show-in-folder |
| 3 | Copy path | — | | copy-path |
| | `|` | | | |
| 4 | Remove from list | — ("Del", fixed) | | remove |
| 5 | Remove missing files | — | any missing | remove |

Items: 5, 0 submenus, 1 separator.

### C-M9. Home: right-click a recent project

`homeProjectsRowMenu`, same component, instance `HomePage.qml:323-342`; shown when there are recent projects.

Open (open-project; "Enter"; not missing), Show in folder (show-in-folder), Copy path (copy-path) | Remove from list (remove; "Del"), Remove missing folders (remove; any missing). Items: 5, 1 separator.

### C-M10. File › Recently opened … (menu bar, listed for Home naming)

`RecentFilesMenu.qml:6-33`, used at `Main.qml:3590-3630`. {dyn} recent files (tooltip `qsTr("Open") + " " + path`), "None" (disabled, empty list). Items: 2.

### C-M11. Explorer: right-click a row

`explorerRowMenu`, `ExplorerPanel.qml:389-448`. Right-click (`ExplorerPanel.qml:244-250`); Menu key / Shift+F10 through the list's own `Keys.onPressed` (`ExplorerPanel.qml:210-212`), not `MenuKey`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Open in a new tab | — | subtitles | tab-new |
| 2 | Open in this tab | — | subtitles or media | open-subtitles |
| | `|` | | | |
| 3 | New subtitle file here | — | | new-subtitle-file |
| 4 | Rename | — ("F2", fixed) | not the project settings row | edit |
| 5 | Move to the trash | — ("Del", fixed) | as 4; asks first (`ExplorerPanel.qml:94-106`) | delete |
| | `|` | | | |
| 6 | Show in folder | — | | show-in-folder |
| 7 | Copy path | — | | copy-path |

Items: 7, 0 submenus, 2 separators.

### C-M12. Reference tray: right-click a row

`referenceOccurrenceMenu`, `ReferenceTray.qml:137-176`. Right press via `LineGrid.contextMenuRequested` (`94-98`); Menu key via `MenuKey` (`99`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Copy | — ("Ctrl+C", fixed) | | edit-copy |
| | `|` | | | |
| | heading "Also in" | | when found | |
| 2 | {dyn} (•) "title, line N" | — | MenuRadioItem | — |
| 3 | No other occurrences | — | disabled; when none | — |

Items: 3, 1 separator, 1 heading.

### C-M13. Styles panel: right-click a style

`stylesRowMenu`, `StylesPanel.qml:454-525`. Right-click (`226-233`); Menu key via `MenuKey` (`149-153`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Apply to the selected lines | — ("Enter", fixed) | editable ASS | style-apply |
| 2 | Select lines with this style | — | | select-lines |
| | `|` | | | |
| 3 | New style… | — | | add |
| 4 | Edit style… | — | can change | edit |
| 5 | Duplicate | — | | duplicate |
| 6 | Copy to the catalogue… | — | | copy-to-storage |
| | `|` | | | |
| 7 | Style manager | — | | styles |
| | `|` | | | |
| 8 | Delete | — ("Del", fixed) | | delete |

Items: 8, 0 submenus, 3 separators.

### C-M14. Styles panel » (tool strip overflow)

`stylesMoreMenu`, `StylesPanel.qml:405-452`; button `393-403` (menu-more, "More").

Apply to the selected lines (style-apply) | New style… (add), Edit style… (edit), Duplicate (duplicate) | Style manager (styles) | Delete (delete). Items: 6, 3 separators.

### C-M15. Style manager: list ⋯ (catalogue and document lists)

`<prefix>MoreMenu`, `StyleManager.qml:1260-1296`; button `1252-1258` (panel-menu, tooltip "More").

Move to top (move-to-top; selection), Move to bottom (move-to-bottom; selection), Sort by name (sort) | Delete unused styles (delete; document list only). Items: 4 (3 in the catalogue list), 1 separator.

### C-M16. Style manager: catalogue tiers ⋯

`catalogTierMenu`, `StyleManager.qml:1436-1452`; button `1428-1435` ("Catalogue tiers", panel-menu, only with a Project).

Copy to project / Copy to mine (copy-to-tier), Move to project / Move to mine (move-to-tier; not Mine's Default). Items: 2.

### C-M17. Font catalogue window: tiers (⋯ and right-click a catalogue)

`fontCatalogTierMenu`, `FontCatalogWindow.qml:835-852`; button `385-392` ("Catalogue tiers"), right-click on a catalogue row only with a Project (`463-470`).

Copy to project / Copy to mine (copy-to-tier), Move to project / Move to mine (move-to-tier). Items: 2.

### C-M18. Style editor font: Add to a catalogue (+)

`styleEditorCatalogAddMenu`, `FontCatalogBar.qml:127-166`, from the Add button (`66-70`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Add fonts from subtitles… | — | | fonts-from-subtitles |
| 2 | {dyn} [x] catalogue names | — | inserted after row 1, no separator | — |
| 3 | No catalogues yet | — | disabled; only when none | — |
| 4 | New catalogue… | — | only when none | add |

Items: 4, 0 separators.

### C-M19. Font picker: catalogue chooser

`fontPickerCatalogMenu`, `FontPicker.qml:455-491`, from the catalogue chip (`264`).

{dyn} [x] catalogue names with a count in the detail column (exclusive choice drawn as check boxes) | Manage catalogues (font-catalogs). Items: 2, 1 separator.

### C-M20. Font picker: Add to a catalogue

`fontPickerAddMenu`, `FontPicker.qml:492-540`, from "Add to a catalogue" (`284`).

No catalogues yet (disabled; when none), {dyn} [x] catalogue names | New catalogue… (add), Add fonts from subtitles… (fonts-from-subtitles). Items: 4, 1 separator.

### C-M21. Layout switcher ▾ and View › Layout ▸

`LayoutMenu.qml:7-110`; instances `LayoutSwitcher.qml:72-82` (chevron-down button) and `Main.qml:4070-4085`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1-5 | [x] Editing, Timing, Translation, Typesetting, Karaoke | — | unlocked; exclusive, drawn as check boxes | — |
| | `|` (when saved layouts) | | | |
| 6 | {dyn} [x] saved layouts | — | usable, not hidden, unlocked; reason "Hidden by the project’s copy" | project / mine (with a Project) |
| | `|` | | | |
| 7 | Save layout as… | — | | layout-save |
| 8 | Reset this layout | — | unlocked | layout-reset |
| 9 | Manage layouts… | — | any saved layout | layout-manage |
| | `|` | | | |
| 10 | [x] Lock layout | GLOBAL_TOGGLE_LOCK_LAYOUT (bound) | | layout-lock |

Items: 10, 0 submenus, 3 separators.

### C-M22. Manage layouts: layout tiers ⋯ and right-click a layout

`layoutTierMenu`, `ManageLayoutsDialog.qml:113-129`; ⋯ button on the highlighted row (`253-260`, `Qt.NoFocus`), right-click only with a Project (`191-198`).

Copy to project / Copy to mine (copy-to-tier), Move to project / Move to mine (move-to-tier). Items: 2.

### C-M23. QC notes

No row or panel right-click menu (`QcNotesPanel.qml` has none). Its ⋯ rows are C-M1 rows 35-39.

**Totals:** 23 menus (C-M1 counted once for all its openers; C-M6 has 0 app items).

---

## 2. Changed since CX2 (1d122c203)

- **C-M1 panel ⋯**
  - Added: [x] Lock layout with its own group between Float/Dock and Close (DK4 #657, 517e7d398, 2026-11-03).
  - Added: About <plugin>… restyled with its own separator before Close (PL4 #416, a7dc6a81d); plugin rows {dyn} (PL4).
  - Added: Move to the left side bar, Move to the right side bar, Move out of the side bar, Move left, Move right (DK3 side bars, 8667645f6, 2026-11-12); side bar icon and header open this menu (8667645f6, 0618a1b66).
  - Added: QC notes own rows: Import notes file…, Import notes from the clipboard, Export notes…, Mark all as not done, Remove all notes… (QC #577, 75e1cd7a2, 2026-11-04).
  - As approved (CX6 52fb5c3c2): Float/Dock switching row, Close alone after a separator, "Close" for Reference, Clear runs in its own tail group, Show times / Show frames moved to the Line editor, "Move video to the selected line ▸" and "After moving, play ▸".
  - Differs from approved 17: the choices of Show times / Show frames and the two video submenus are drawn as check boxes, not (•) (see C9).
- **C-M2 floating ⋯**: unchanged since CX6 (Dock | Close), which overrode approved 18.1 with the window-menu decision.
- **C-M3/C-M4 tabs**
  - Added: `|` Move tab to new window [GLOBAL_MOVE_TAB_TO_NEW_WINDOW], Move tab to window ▸ {dyn} before the closes, on both the tab and the empty bar (MW2 #630, f8823b596, 2026-11-09).
  - Icon change: Copy path is copy-path, not edit-copy (IC3, e55a1eaf3, 2026-11-04); Home and Explorer use it too.
  - Otherwise as approved 19/20.
- **C-M5**: new (MW2 f8823b596).
- **C-M8 Home row**: as approved 26; Copy path icon is copy-path (IC3). `RecentList.qml` was split out for projects (PJ2, 551cc10b5).
- **C-M9 Home project row**: new (PJ2 #770 area, 551cc10b5, 2026-11-04).
- **C-M11 Explorer row**: new (PJ4 #772, 0c5d36c85, 2026-11-13).
- **C-M12 Reference tray**: as approved 27 (CX6).
- **C-M13/C-M14 Styles**: as approved 21. "Copy to the catalog…" became "Copy to the catalogue…" (text style #596, 7602bfc24).
- **C-M15 Style manager list ⋯**: as approved 22.
- **C-M16, C-M17, C-M22 tier menus**: new (PJ5a #897 bca6de709; 60cd8075f "Give font catalogues, layouts, shapes and tags a Project tier", 2026-11-12).
- **C-M21 layouts**: saved layouts take project/mine icons and a hidden reason (60cd8075f); Lock layout row (DK4 517e7d398).
- **C-M6, C-M7, C-M10, C-M18-20**: no row changes in menus since the baseline beyond text style (#596) and Project fonts (e966caa23).

---

## 3. Findings

**C1. destructive-next-to-common** — QC notes ⋯: Remove all notes… sits in one group with Import, Export and Mark all as not done. Evidence: `Main.qml:1463-1464` (in `panelOwnItems`, not `panelOwnTail`). Recommend: move it to `panelOwnTail` like Automation's Clear runs, giving Import notes file…, Import notes from the clipboard…, Export notes…, Mark all as not done `|` Remove all notes… `|` Move panel… Visible change: yes.

**C2. destructive-next-to-common** — Explorer row: Move to the trash sits in the middle, followed by Show in folder and Copy path, and it asks a question but has no "…". Evidence: `ExplorerPanel.qml:427-446`, question `94-106`. Recommend: Open in a new tab, Open in this tab `|` Show in folder, Copy path `|` New subtitle file here, Rename `|` Move to the trash… (delete, Del). This matches the tab and Home order (open, then Show in folder / Copy path, destructive last). Update `docs/ux/explorer.md:29` and `context_menus.txt`. Visible change: yes.

**C3. wrong-target** — Empty tab bar: Move tab to new window and Move tab to window ▸ show and act on the window's current tab, not a clicked tab. Approved 20 kept the empty bar to rows that make sense without a tab. Evidence: `DocumentTabMenu.qml:35, 162-175` (plain `ShellMenuItem`/`MoveTabMenu`, not `TabRow`). Recommend: show both rows and their separator only when `onTab`; the empty bar goes back to New tab, Save all, Switch to ▸ `|` Close all tabs. Visible change: yes.

**C4. missing-expected** — Explorer: the tool strip (New subtitle file, Group by name, Show all files, Refresh) hides completely when the header is narrow. There is no » overflow, and the Explorer has no own rows in its ⋯, so those commands can't be reached. Evidence: `ExplorerPanel.qml:352` (`visible: tools.width + 0.5 >= implicitWidth`); `Main.qml:1423-1471` has no Explorer branch. Recommend: Explorer own rows in its ⋯: New subtitle file (new-subtitle-file), Refresh (refresh), [x] Group by name, [x] Show all files. Or a » overflow like the Styles panel. Visible change: yes.

**C5. missing-expected (keyboard)** — Side bar icons: Shift+F10 does nothing. Only the Menu key works, through `Keys.onMenuPressed`, not `MenuKey`. Evidence: `SideBar.qml:64, 88`; the filter forwards an unhandled Shift+F10 and nothing takes it (`menu_key.cpp:78-90`). Recommend: `MenuKey.onRequested: bar.iconMenu(modelData.name, icon)` on the icon. Visible change: no.

**C6. inconsistent-name** — A missing plugin panel's ⋯ says Close (which forgets the dock, `Main.qml:1504-1505`), while View › Panels › that panel says "Remove from the layout" (remove) for the same act (`Main.qml:4181-4188`). Recommend: in the panel ⋯ for a missing plugin, label the row "Remove from the layout" (remove), still alone after the separator. Visible change: yes.

**C7. inconsistent-name** — QC notes: one command, two names. The ⋯ says "Import notes from the clipboard" and the empty panel's button says "Paste notes" (`QcNotesPanel.qml:479-484`, both `importClipboard()`). The ⋯ row can ask Add/Replace (`QcNotesPanel.qml:99-108`) but has no "…". Recommend: "Import notes from the clipboard…" in the ⋯, and "Import from the clipboard" on the empty-panel button (it never asks there), or one label for both. Update `docs/ux/qc-notes.md:44`. Visible change: yes.

**C8. inconsistent-name (uncertain)** — "Remove all notes…" deletes notes stored in the file (undo restores them). text-style.md's "Deleting versus removing" says Delete destroys data and Remove takes an item out of a list. Evidence: `Main.qml:1463`, question `QcNotesPanel.qml:139-144` ("Remove all"). Recommend: "Delete all notes…" with the question's button "Delete all". Uncertain: the owner may count notes as list items. Visible change: yes.

**C9. toggle-hides-state** — Exclusive choices are drawn as check boxes, not radio rows (`MenuRadioItem`, CX5). Approved 17 drew Show times / Show frames and the video follow choices as (•).
- Line editor ⋯ Show times / Show frames: `Main.qml:1425-1430`, delegate `1593-1612` (`radio` only sets `keepsMenuOpen`).
- Video ⋯ submenu choices: `Main.qml:1625-1636`.
- Layout presets and saved layouts: `LayoutMenu.qml:31-71`.
- Font picker catalogue chooser: `FontPicker.qml:470-480`.

Recommend: a `MenuRadioItem` delegate where `modelData.radio` is set, and for those lists. Visible change: yes.

**C10. legacy-name** — ⋯ tooltips break text-style's "A menu of more items: More options (tooltip of ⋯), not %1 options". Evidence: `DockMenuButton.qml:17` ("%1 options", panel ⋯ and floating ⋯); `StyleManager.qml:1256` ("More" on a ⋯ panel-menu button). Recommend: tooltip "More options"; keep the panel name for screen readers via `Accessible.description` (or an allow-list row if the owner wants the name in the tooltip). The Styles » "More" is overflow and can stay. Visible change: yes.

**C11. inconsistent-name** — The two "add this font to a catalogue" menus differ.
- Font picker: {catalogues} `|` New catalogue…, Add fonts from subtitles… (`FontPicker.qml:492-540`).
- Style editor's +: Add fonts from subtitles…, {catalogues}, with no separator, and New catalogue… only when there are no catalogues (`FontCatalogBar.qml:127-166`).

Recommend: the picker's order for both, with New catalogue… always shown: No catalogues yet / {[x] catalogues} `|` New catalogue…, Add fonts from subtitles…. Visible change: yes.

**C12. inconsistent-name (uncertain)** — Window-command naming:
- "Manage layouts…" has an ellipsis (`LayoutMenu.qml:92`, `menu_bar.txt:191`) and opens the "Manage layouts" window (`ManageLayoutsDialog.qml:12`), whose edits act at once (only a Close button).
- "Manage catalogues" has none and opens "Manage font catalogues" (`FontPicker.qml:434, 489`; `FontCatalogWindow.qml:14`).

text-style decision 5 says no ellipsis on tool windows, and a window takes its command's name. Recommend: "Manage layouts" (no …) and "Manage font catalogues" for the picker row and button. Uncertain whether the owner counts Manage layouts as a tool window. Visible change: yes.

**C13. missing-expected (keyboard)** — Tier menus have no keyboard route.
- Manage layouts: the tier ⋯ button is `Qt.NoFocus` and the row has no `MenuKey` (`ManageLayoutsDialog.qml:191-198, 253-260`), so Copy/Move to project can't be reached without a pointer.
- Font catalogue rows: no `MenuKey` on right-click (`FontCatalogWindow.qml:463-470`). Its ⋯ button can be reached.
- Right-click on these rows does nothing without a Project, and with one it offers only the tier rows, not Rename/Delete.

Recommend: `MenuKey.onRequested` on the list opening the tier menu, and make the ⋯ focusable. Optionally add Rename (F2) and Delete… under a separator to the row's right-click. Visible change: no for the keys, yes for the optional rows.

**C14. wrong-target (PLAUSIBLE)** — The floating window ⋯'s Close calls KDDW's `closeButtonClicked()` (`DockTitleBar.qml:66-71`). It bypasses `root.closePanel` (`Main.qml:1500-1509`), which also unloads the reference for the Reference panel and forgets a missing plugin's dock. So Close from this menu can leave a reference loaded with its panel closed. I didn't check whether KDDW's close reaches `closePanel` some other way. Recommend: route the window's Close through `root.closePanel` for each dock in the window. Visible change: yes (behaviour).

**C15. text-style (keys)** — Keys are written into labels: "Rename (F2)" (`ManageLayoutsDialog.qml:266`), and in the same area "Move up (Alt+Up)" / "Move down (Alt+Down)" tooltips (`StyleManager.qml:1234, 1240`; these are tooltips, not menu rows). text-style: "A bound key is never written into a label". Recommend: "Rename", "Move up", "Move down", and let the tooltip show the key (or the key column in a menu). Visible change: yes.

**C16. text-style (pieces)** — The recent-file tooltip is built from translated pieces: `qsTr("Open") + " " + modelData.path` (`RecentFilesMenu.qml:20`). Recommend: `qsTr("Open %1").arg(modelData.path)`. Visible change: no (English is the same).

**C17. inconsistent-name (low, uncertain)** — Side bar rows "Move left" / "Move right" use the tab-move-left/right icons and read like the tab rows "Move tab left/right" in the same menu. "Move out of the side bar" uses panel-move, the same icon as Move panel… (`Main.qml:1687-1713`). Recommend: "Move left in the side bar" / "Move right in the side bar", and a distinct icon for Move out of the side bar (new role, e.g. side-bar-off). Visible change: yes.

**C18. inconsistent-name (low)** — The Styles tool strip's icon tooltips say "New style" / "Edit style" (`StylesPanel.qml:355, 363`), while both menus say "New style…" / "Edit style…". text-style says an icon-only tooltip is the command's name, the same as its menu item, and other strips keep the "…" (`StyleManager.qml:1249` "Load from a file…"). Recommend: "New style…", "Edit style…" on the buttons. Visible change: yes.

**C19. test-coverage** — `tests/ui/context_menus.txt` pins the panel ⋯ only as "Grid panel options" (`context_menus.txt:291-307`). The other panels' own rows (QC notes' 5, Automation's 4, Spellchecker, Line editor, Video ▸, Reference) and the empty-bar variant of the tab menu are not pinned, so C1/C3-style drift passes the ledger. Recommend: list one "Panel options (<panel>)" section per panel with own rows, and a "Document tab bar" section for `tabIndex -1`. Visible change: no.

**C20. missing-expected (uncertain)** — QC notes rows have no right-click menu. Edit (F2), Delete, Done and going to the line are reachable only by double-click, keys and the row's controls (`QcNotesPanel.qml`). `docs/ux/qc-notes.md` doesn't ask for one. If wanted: Go to line, Edit (F2) `|` [x] Done `|` Delete (Del), with `MenuKey`. Visible change: yes.

Clean against the rules (no finding):
- Styles row and »: names, order, Delete last.
- Style manager list ⋯.
- Home file and project rows.
- Reference tray.
- Tab menu Save / Show in folder / Copy path order; submenu rows drop the parent's word.
- Close alone last in the panel ⋯.
- » used only for overflow (Styles, title bar); the layout switcher uses a chevron.
- Icons on every non-check, non-data row except plugin rows (no API for icons).

Out of area, seen in passing: View › "Toggle left side bar" / "Toggle right side bar" are check items named "Toggle…" (`Main.qml:889, 900`), which text-style forbids ("Show / hide …", "Toggle …"). The menu-bar area should check them.
