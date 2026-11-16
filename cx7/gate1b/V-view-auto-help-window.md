# CX7 #810 gate 1b: menu bar, View, Automation, Help and the window's own menus

Read-only, at `origin/main` 8dbf88f41. `src/` and `tests/ui/menu_bar.txt` in this worktree match it.

Where things are defined:
- **Menus:** `src/ui/Main.qml`: Automation `3960-4063`, View `4064-4259`, Help `4260-4320`.
- **Shared helpers:** `src/ui/LayoutMenu.qml` (View › Layout ▸ and the title bar's layout switcher) and `src/ui/MacroMenuEntries.qml` (plugin commands and macros).
- **Actions:**
  - side bars: `Main.qml:888-914`;
  - Lock layout: `Main.qml:1110-1121`;
  - Automation, Node effects, Explorer, Hide tags in the line editor and the Help rows: `Main.qml:2863-2960`.
- **Default keys:** `kMainDefaults` (`src/application/hotkeys.cpp:843-931`).
- **Shortcut names:** `kNames` (`hotkeys.cpp`, mirrored in `src/ui/command_names.cpp`).

Notation is CX1's (`docs/ux/context-menus.md:1-16`). `{key}` is the default binding and "—" means unbound. Icon exemptions follow IC1 (`docs/ux/menu-icon-gaps.md`, "Items left without an icon").

Already decided or raised, and not repeated here:
- audit rule 23: "Toggle left/right side bar" become check items named "Left side bar" / "Right side bar", and macros and plugin commands show their shortcut (D-F5);
- C6: a missing plugin's "Remove from the layout" against Close;
- C9: layout presets drawn as check boxes;
- C12: "Manage layouts…";
- C17: side bar move rows;
- D-F7: "About %1…".

## View

`Main.qml:4064-4259`. The menu is filled the first time it opens (`lazyContent`, #971 PR #1081).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | &Layout ▸ | — | always; the same `LayoutMenu` as the title bar's layout switcher | layout |
| | `|` | | | |
| 2 | [x] Toggle left side bar | GLOBAL_TOGGLE_LEFT_SIDE_BAR {Ctrl+Alt+B} | always; checked = `leftSideBarShown` (rule 23 renames it) | side-bar-left |
| 3 | [x] Toggle right side bar | GLOBAL_TOGGLE_RIGHT_SIDE_BAR — | always; checked = `rightSideBarShown` | side-bar-right |
| 4 | Swap side bars | GLOBAL_SWAP_SIDE_BARS — | always; flips `appearance.aiSideBar` | side-bars-swap |
| 5 | Reset side bars | — (no command) | `!layoutLocked` | layout-restore |
| | `|` | | | |
| 6 | &Panels ▸ | — | always | panels |
| 7 | &Move panel… | — (no command) | `!layoutLocked`; opens "Move panel" at the focused panel, else the Grid; the window has a panel chooser | panel-move |
| 8 | Restore the previous layout | — (no command) | `workspaceLayout.hasBackup && !layoutLocked`; restores `<layout>.bak`, the arrangement before the last save | layout-restore |
| | `|` | | | |
| 9 | [x] Node effects | GLOBAL_NODE_EFFECTS — | on, or `hasEditingTarget && !layoutLocked`; also the title bar's node-editor button | node-editor |
| | `|` | | | |
| 10 | Explorer | GLOBAL_SHOW_EXPLORER — | `!layoutLocked`; only shows the panel | explorer |
| 11 | Log | — (no command) | `!layoutLocked`; shows the Log, or hides it when it is shown; no check | log |
| 12 | Hide tags | GLOBAL_HIDE_TAGS — | always; flips `grid.hideTags`, which swaps each tag block in the Grid for ☀; no check | hide-tags |
| 13 | [x] Hide tags in the line editor | GLOBAL_EDITOR_HIDE_TAGS — | always; checked = `!editor.showTags` | hide-tags |

Items: 13 top-level, 4 separators. With the submenus below: 108 in all with Review and Agent on (100 with both off). `menu_bar.txt` lists 43 rows and 8 separators for View, because the listing stops at the second level (VW12).

### View › Layout ▸

`LayoutMenu.qml:7-110`, with `prefix: "viewLayout"` (`Main.qml:4070-4084`). C-M21 audited it as the switcher's menu, and the rows are the same.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1-5 | [x] Editing, Timing, Translation, Typesetting, Karaoke | — | `!layouts.locked`; checked when that preset is in use and there's no saved layout; one-of-five drawn as check boxes (C9) | — (check) |
| | `|` (only when there are saved layouts) | | | |
| 6 | {dyn} [x] saved layouts | — | usable, not hidden, unlocked; reason "Hidden by the project’s copy" | project / mine (with a Project), else — |
| | `|` | | | |
| 7 | Save layout as… | — | always | layout-save |
| 8 | Reset this layout | — | `!locked` | layout-reset |
| 9 | Manage layouts… | — | there is a saved layout (C12 on the "…") | layout-manage |
| | `|` | | | |
| 10 | [x] Lock layout | GLOBAL_TOGGLE_LOCK_LAYOUT — | locked, or no panel floats; `reason` = `lockReason` | layout-lock |

Items: 10 (the saved layouts count as 1), 3 separators (1 conditional).

### View › Panels ▸

`Main.qml:4112-4199`. The panels come from `builtInDocks` in registry order (`Main.qml:925-934`). Review is listed only while AI Review is on, and Agent only while the Agent is on.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Video ▸ | — | | — (IC1: panels named by data) |
| 2 | Audio ▸ | — | | — |
| 3 | Original text ▸ | — | its Show needs Translation mode (`panelAvailable`, `Main.qml:1318-1320`) | — |
| 4 | Line editor ▸ | — | | — |
| 5 | Grid ▸ | — | | — |
| 6 | Reference ▸ | — | | — |
| 7 | Shift times ▸ | — | | — |
| 8 | Timing postprocessor ▸ | — | | — |
| 9 | Find ▸ | — | | — |
| 10 | Automation ▸ | — | | — |
| 11 | Spellchecker ▸ | — | | — |
| 12 | Log ▸ | — | | — |
| 13 | Styles ▸ | — | | — |
| 14 | Template editor ▸ | — | | — |
| 15 | QC notes ▸ | — | | — |
| 16 | Review ▸ | — | only while `review.on` | — |
| 17 | Agent ▸ | — | only while `agent.on` | — |
| 18 | Graph ▸ | — | node effects | — |
| 19 | Node ▸ | — | node effects | — |
| 20 | Source lines ▸ | — | node effects | — |
| 21 | Explorer ▸ | — | | — |
| | `|` (only with plugin panels) | | | |
| 22 | {dyn} plugin panel ▸ | — | one per plugin panel that is on in this window (`runtime.onIn(plugin, shellWindow)`, `Main.qml:939-948`) or kept in the layout; plugin name in the detail column | plugins |

Items: 22 (19 + plugin {dyn} + Review, Agent), 1 conditional separator.

### View › Panels › *panel* ▸ (each built-in panel)

`Main.qml:4119-4145`. This is CX2 menu 16, as built by CX6.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Show | — | `panelAvailable && !layoutLocked`; enabled even when the panel is already shown (it raises a hidden tab) | show |
| 2 | Float / Dock | — | `panelShown && !layoutLocked`; the label and icon switch with `panelFloating` | panel-float / panel-dock |
| | `|` | | | |
| 3 | Close | — | `dock.isOpen && !layoutLocked`; `closePanel` | tab-close |

Items: 3 per panel, 1 separator; 63 across 21 panels.

### View › Panels › *plugin panel* ▸

`Main.qml:4155-4195`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Show | — | plugin running here (`!kept`), `!layoutLocked` | show |
| 2 | Float / Dock | — | `panelShown && !layoutLocked` | panel-float / panel-dock |
| | `|` | | | |
| 3 | Close | — | dock exists and is open, `!layoutLocked` | tab-close |
| 4 | Remove from the layout | — | shown only when `kept` (plugin off or missing); `!layoutLocked`; `forgetPluginDock` (C6) | remove |

Items: 3 or 4, 1 separator.

## Automation

`Main.qml:3960-4063`. `onAboutToShow` calls `automation.menuOpened()`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | &Load script… | GLOBAL_AUTOMATION_LOAD_SCRIPT — | always; file picker; help "Load script" | automation |
| 2 | Refresh autoload scripts | GLOBAL_AUTOMATION_RELOAD_AUTOLOAD — | always, even during a run (VW5); help "Refresh autoload scripts" | automation |
| 3 | Run the last loaded script | GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT — | `!automation.running && !modalOpen` | run-script |
| 4 | Rerun last macro | — (no command) | `automation.canRerun && !modalOpen`; runs it again, with its dialogs | rerun-macro |
| 5 | Repeat last run | GLOBAL_AUTOMATION_REPEAT_LAST_RUN — | `automation.canRepeat && !modalOpen`; replays the last answers | repeat-run |
| 6 | Cancel macro | — (no command) | shown only while `automation.running`; enabled unless already cancelling | run-cancelled |
| 7 | Show &panel | — (no command) | always; `showPanel(automationDock)`, the same as View › Panels › Automation › Show | automation-manager |
| 8 | Pl&ugins… | GLOBAL_PLUGINS — | `!modalOpen`; opens the "Plugins" window | plugins |
| 9 | {dyn} plugin ▸ (one per plugin, titled with its name) › {dyn} its commands | — | plugins that are on in this window (`Main.qml:4033-4039`); placed directly after Plugins…, with no separator; no keys shown (D-F5) | — (IC1: named by data) |
| | `|` | | always present (`macrosSeparator`, `Main.qml:4044-4047`) | |
| 10 | {dyn} macros, with "/" opening submenus ▸ | — | `!automation.running`; Shift+click records a shortcut; no keys shown (rule 23) | — (IC1) |

Items: 10 (the two {dyn} groups count 1 each; Cancel macro counts though usually hidden), 1 separator.

## Help

`Main.qml:4260-4320`. Every row has status-bar help in the right form ("Opens …", "Checks …").

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | &Getting started | GLOBAL_GETTING_STARTED — | always; opens the "Getting started" dialog | getting-started |
| 2 | User &guide | GLOBAL_HELP {F1} | always; opens the browser | help |
| 3 | &Report an issue | GLOBAL_ANSI — | always; opens the "Report an issue" form window | report-issue |
| 4 | Check for &updates | GLOBAL_CHECK_FOR_UPDATES — | `!updates.checking` | check-updates |
| 5 | Open user &folder | — (no command) | shown only when `app.userFolder` is set | show-in-folder |
| 6 | What’s &new | — (no command) | always; "What’s new" dialog | whats-new |
| 7 | &About | GLOBAL_ABOUT — | always; "About HikariSub" | about |
| 8 | &Credits | GLOBAL_HELPERS — | always | credits |

Items: 8, 0 separators.

## The window's own menus

### Title bar "More menus" (»)

`MainTitleBar.qml:339-379` (`moreMenu`). The button shows only while the bar is too narrow (`foldFrom < menus.count`). It is `Qt.NoFocus` and is reached through the menu-bar keyboard target, and the folded titles keep their mnemonics (`MainTitleBar.qml:188-213`). Tooltip and accessible name: "More menus".

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | {dyn} the folded menu titles (from the right: Help, View, Automation, …) | — | each opens that menu under » (`openFolded`, `:166-173`) | — (IC1: menus themselves) |

Items: 1 ({dyn}), 0 separators. Unchanged since CX1 M8, which approved it as it is.

### Title bar right-click and the app icon

`TitleBarMoveArea.qml:20-26` and the app icon (`MainTitleBar.qml:301-327`, "Window menu") call `MainWindowFrame::showWindowMenu` (`window_frame.cpp:546-556`). That shows the OS window menu:
- natively on Windows (HTSYSMENU);
- through `_GTK_SHOW_WINDOW_MENU` on X11 (`:164`);
- through the xdg-shell `show_window_menu` request on Wayland (`:224-233`).

It has 0 app rows, which CX2 R12 kept. On a compositor without a window menu, the right-click shows nothing. That is platform behaviour, so there's no finding.

### Secondary windows (MW1/MW2)

Every shell window is a `Main.qml` with its own `shellWindow` number (`Main.qml:24`), so every window builds the same menu bar. The only differences are in data rows:
- the Automation plugin ▸ submenus and the plugin panel rows under View › Panels list only the plugins that are on in that window (`Main.qml:943, 4038`);
- the layouts are that window's tab's layouts, and the side bars belong to the window (ADR 0032).

No row is added or removed for window 0. File › Close window and Exit appear in every window (other slice).

### macOS and native menu bars

None. No `Platform.MenuBar`, `QMenuBar` or `Q_OS_MACOS` menu code exists in `src/`. The only `Q_OS_MACOS` guards are in `window_frame.cpp:25, 166`, which exclude the X11 path. On macOS the in-window bar would be the only menu bar, and About, Settings and Exit would not move to the application menu. macOS isn't a shipped target, so there's no finding.

## Counts

| Menu | Items | Separators |
|---|---|---|
| View (top-level) | 13 | 4 |
| View › Layout ▸ | 10 | 3 (1 conditional) |
| View › Panels ▸ | 22 | 1 (conditional) |
| View › Panels › panel ▸ ×21 | 63 | 21 |
| View › Panels › plugin panel ▸ | {dyn}, 3–4 each | 1 each |
| Automation | 10 | 1 |
| Help | 8 | 0 |
| Title bar » | 1 ({dyn}) | 0 |
| Title bar right-click | 0 app rows | — |

That makes 9 menus with app rows (the plugin panel submenu counted once), and 0 for the OS window menu. In all: View 108, Automation 10, Help 8, the » menu 1, so 127 rows with Review and Agent on.

## Changed since 1d122c203

The changes come from `diff <(git show 1d122c203:tests/ui/menu_bar.txt) tests/ui/menu_bar.txt` and `git log -S` on each row. Each one is mapped to the first-parent merge on `main` that brought it in.

| Change | Row | Commit | PR |
|---|---|---|---|
| removed | View › Panels › Fix minor errors ▸ | 003f6fa18 | #634 (#608) |
| renamed | View › Panels › "Search" → "Find" | e2253ea5d | #660 (#597) |
| added | Automation › Plugins… (GLOBAL_PLUGINS) | 1e3c7f924 | #663 (PL1 #413) |
| added | View › Panels › Timing postprocessor ▸ | bc48cbc63 | #674 (#598) |
| added | Automation › {dyn} plugin ▸ › commands, after Plugins… | dbceffc98 | #683 (PL2) |
| renamed | Help › "What's new" → "What’s new" (curly apostrophe) | a8f80546b | #709 (#596) |
| renamed, merged | View › Panels › *panel* › Show, Hide, Float, Dock → Show, Float/Dock (one switching row), `|`, Close (approved 16.1, 16.2) | 52fb5c3c2 | #721 (CX6 #582) |
| added | View › Panels › `|` and {dyn} plugin panels ▸ (with Remove from the layout) | a7dc6a81d | #722 (PL4 #416) |
| added | View › Layout › `|`, [x] Lock layout | 517e7d398 | #762 (DK4 #657, pin train) |
| added | View › Panels › QC notes ▸ | 75e1cd7a2 | #798 (#577, pin train 2) |
| added | View › Panels › Review ▸ (only while AI Review is on) | e3ceae9f4 | #798 (AI2 #430, pin train 2) |
| added | View › `|`, [x] Node effects; View › Panels › Graph ▸, Node ▸, Source lines ▸ | 8adb1acc9 | #826 (NE2 #424) |
| added | View › Panels › Agent ▸ (only while the Agent is on) | 101d3e743 | #864 (AI7b #435) |
| added | View › `|`, Explorer (GLOBAL_SHOW_EXPLORER); View › Panels › Explorer ▸ | 0c5d36c85 | #1065 (PJ4 #772) |
| none (build) | the title-bar menus build their rows on first open | 2022a60c5 | #1081 (#971) |
| added | View › `|`, Toggle left side bar, Toggle right side bar, Swap side bars, Reset side bars | 8667645f6, b8e6349d8 | #1157 (DK3 #1040) |

Nothing in these three menus moved between menus. Help changed only by the apostrophe. Automation changed only by Plugins… and the plugin ▸ rows. The title bar » menu and the right-click didn't change.

## Findings

### VW1. toggle-hides-state: View › "Hide tags" is a switch without a check, and doesn't say it means the Grid

- **Tag:** toggle-hides-state, inconsistent-name.
- **Evidence:**
  - `Main.qml:4244-4251`: a plain `ShellMenuItem`, `text: qsTr("Hide tags")`, which calls `app.toggleHideTags()`.
  - `application.cpp:6620-6624`: it flips the boolean `grid.hideTags`.
  - `shell_controller.h:39`: the state is exposed as `shell.hideTags`.
  - The next row, "Hide tags in the line editor" (`Main.qml:4252-4257`, `2909-2919`), is a check item.
  - So the two neighbours look like one plain command and one switch, and only the second says where it acts.
  - text-style's "Check item, toggle" row: the label names the state when it is on.
  - Shortcut name: "Hide tags" (`hotkeys.cpp:573`, `command_names.cpp:145`).
- **Recommend:** "[x] Hide tags in the Grid", checkable, `checked: root.shell.hideTags`. Rename the shortcut "Hide tags in the Grid" too.
  - The legacy import key `HideTags` stays (`settings_import_tables.cpp:441`).
  - Both rows keep the hide-tags icon, which check items may keep.
  - Regenerate `menu_bar.txt`.
  - parity.md row 62 and note 17 quote "View › Hide tags".
- **Visible:** yes.

### VW2. toggle-hides-state, duplicate: View › Log toggles without a check, and Explorer next to it only shows

- **Tag:** toggle-hides-state, inconsistent-name, duplicate (of View › Panels › *name* › Show/Close).
- **Evidence:**
  - Log (`Main.qml:4230-4243`) hides the Log when it is shown, else shows it, with no check mark.
  - Explorer (`Main.qml:4223-4229`, action `2903-2907`) only calls `showPanel(explorerDock)`.
  - So two adjacent rows that name a panel behave differently.
  - Both also sit in View › Panels (rows 12 and 21), with Show and Close.
- **Recommend:** make both check items named after the panel: "[x] Explorer" and "[x] Log".
  - `checked: root.panelShown(dock)`; triggering shows or closes the panel.
  - Keep GLOBAL_SHOW_EXPLORER, and rename its shortcut "Explorer" (VW9).
  - Check items need no icon, but may keep explorer / log.
  - These rows are kept, not dropped, because parity.md:244 offers "View › Log" as the Aegisub Log window, and DK3/PJ4 put Explorer here.
- **Visible:** yes.

### VW3. flat-list: View's last group mixes panels with tag display, and Restore the previous layout sits outside Layout ▸

- **Tag:** flat-list.
- **Evidence:** `Main.qml:4200-4257`:
  - Restore the previous layout (`:4207-4213`) undoes the last layout save (`workspace_layout.cpp:1180-1181, 1345-1365`). It sits with Panels ▸ and Move panel…, away from Layout ▸'s Reset this layout.
  - Explorer and Log (panels) share a group with Hide tags and Hide tags in the line editor (display options).
  - The layout notice's button uses the same name (`Main.qml:8298-8303`), so the name itself is fine.
- **Recommend:** keep the same 13 rows (12 once Restore moves), in this order:
  1. Layout ▸, which gains Restore the previous layout after Reset this layout, enabled `hasBackup && !locked`.
  2. `|` [x] Left side bar, [x] Right side bar, Swap side bars, Reset side bars
  3. `|` Panels ▸, Move panel…, [x] Explorer, [x] Log
  4. `|` [x] Node effects
  5. `|` [x] Hide tags in the Grid, [x] Hide tags in the line editor

  The layout switcher then gets Restore too, since it shares `LayoutMenu`. Regenerate `menu_bar.txt`.
- **Visible:** yes.

### VW4. one icon, two meanings: Reset side bars uses Restore the previous layout's icon

- **Tag:** inconsistent-name (icon).
- **Evidence:**
  - `Main.qml:4106` (Reset side bars) and `Main.qml:4208` (Restore the previous layout) both use `layout-restore`.
  - IC1 gives `layout-restore` to "Restore the previous layout" (`menu-icon-gaps.md`, View table).
  - B's Audio Customise › Classic arrangement (`Main.qml:6984`) is a third use, which is area B's to fix.
  - Reset side bars puts the side bars back to their defaults (`Main.qml:806-814`, `SideBarModel.defaults`).
- **Recommend:** `restore-default` for Reset side bars. It is the existing "back to defaults" icon (Settings resets, Reset column widths). Otherwise add it to rule 18's new roles as `side-bars-reset`.
- **Visible:** yes.

### VW5. name-differs-from-context-menu: Automation › Refresh autoload scripts has a different icon and enabling from the panel's row

- **Tag:** name-differs-from-context-menu (icon and state), inconsistent-name.
- **Evidence:**
  - Menu bar: `iconRole: "automation"` (`Main.qml:3977`), the same icon as Load script… directly above (`:3970`). It is enabled at all times (`reloadAutoloadAction`, `:2868-2872`, has no `enabled`).
  - Automation panel ⋯: `iconRole: "refresh"`, `enabled: reloadAutoloadAction.enabled && !root.automation.running` (`Main.qml:1436-1438`).
  - The panel's tool strip has the same icon and guard (`AutomationManager.qml:299-301`).
- **Recommend:** `iconRole: "refresh"`, and `enabled: !root.automation.running` on `reloadAutoloadAction`, so the menu row, the ⋯ row and the shortcut all agree.
  - **Uncertain:** what a refresh during a run does. I didn't run it. The panel deliberately blocks it.
- **Visible:** yes.

### VW6. flat-list: Automation has no groups, its plugin rows run into Plugins…, and it can end on a separator

- **Tag:** flat-list.
- **Evidence:**
  - The 8 fixed rows have no separator (`Main.qml:3967-4029`).
  - Cancel macro appears in the middle only while a macro runs (`:4003-4013`), so the rows below it move.
  - The plugin ▸ submenus are inserted directly after Plugins… (`:4030-4043`, `MacroMenuEntries.qml:68-78`).
  - `macrosSeparator` (`:4044-4047`) has no `visible` binding, so with no macros loaded the menu ends on a separator. That is what `menu_bar.txt:180` shows as the last Automation line.
- **Recommend:**
  1. Load script…, Refresh autoload scripts
  2. `|` Run the last loaded script, Rerun last macro, Repeat last run, Cancel macro
  3. `|` Show panel, Plugins
  4. `|` {plugin ▸}
  5. `|` {macros}

  Show each separator only when the group after it has rows: `visible: shown.length > 0` from `MacroMenuEntries`. Regenerate `menu_bar.txt`.
- **Visible:** yes.

### VW7. inconsistent-name (uncertain): three neighbouring "run again" rows that differ only in their help

- **Tag:** inconsistent-name, missing-expected.
- **Evidence:**
  - "Run the last loaded script" (`:2873-2878`), "Rerun last macro" (`:3987-3995`) and "Repeat last run" (`:2879-2884`) are adjacent.
  - Rerun shows the macro's dialogs again (`automation_shell.cpp:1189-1192`). Repeat replays the last answers (`:1202-1233`).
  - Only Repeat's status-bar help says so.
  - Rerun has no command, so it can't take a shortcut.
  - The panel's tool strip shows the macro in Repeat's tooltip (`repeatText`, "Repeat last run: %1, as %2 with the same answers", `automation_shell.cpp:1235-1252`; `AutomationManager.qml:314-315`). The menu row never names the macro.
- **Recommend:**
  - "Run the last macro again" for Rerun.
  - "Repeat last run" stays, with the macro's name in the detail column (`detailText`, from `repeatText`'s macro).
  - Give Rerun a command: a new id in `kActions`/`kNames`, unbound.
  - **Uncertain:** the names came with AD2 and may be the owner's. The strip and the menu must change together.
- **Visible:** yes.

### VW8. text-style "…": "Plugins…" opens a tool window, and "Report an issue" opens a form

- **Tag:** text-style (ellipsis), name-differs-from-context-menu.
- **Evidence:**
  - **Plugins…:** `Pl&ugins…` (`Main.qml:2886`) opens the "Plugins" window (`PluginManagerDialog.qml:16`), where you install and manage plugins and changes act at once. That makes it a tool window, like Style manager and Settings (text-style decision 5). Its shortcut name is "Plugins" (`command_names.cpp:289`), without "…".
  - **Report an issue:** "&Report an issue" (`Main.qml:2931`) opens a form you fill in before anything is sent (`ReportIssueWindow.qml:28`). The "…" rule covers "a dialog that asks for values".
- **Recommend:**
  - "Pl&ugins" (certain). Add it to rule 19's table.
  - "Report an issue…" (**uncertain**: the owner may count the form as a window you work in, like Settings).
  - Update `menu_bar.txt` for both.
- **Visible:** yes.

### VW9. inconsistent-name: shortcut names differ from the View and Automation rows

- **Tag:** inconsistent-name.
- **Evidence:** Settings › Shortcuts and the docs export show the `kNames` names:

  | Command | Shortcut name | Menu row |
  |---|---|---|
  | GLOBAL_SHOW_EXPLORER | "Explorer panel" (`hotkeys.cpp`, id 5337; `command_names.cpp:399`) | "Explorer" |
  | GLOBAL_AUTOMATION_LOAD_SCRIPT | "Load script" (`command_names.cpp:137`) | "Load script…" |
  | GLOBAL_HIDE_TAGS | "Hide tags" | see VW1 |

  Other shortcut names keep their "…" ("Open autosave…", "Translation matching…"; there are 21 in `command_names.cpp`). The Review panel's three names are FE9's.
- **Recommend:** shortcut names "Explorer", "Load script…" and "Hide tags in the Grid". VW8 makes "Plugins" match already.
- **Visible:** yes (Settings › Shortcuts, command search, docs export).

### VW10. text-style: status-bar help in Automation and View breaks the help rule

- **Tag:** text-style.
- **Evidence:**
  - Automation help that only restates the label: "Load script" (`Main.qml:3969`) and "Refresh autoload scripts" (`:3976`).
  - Imperative help: "Run the last macro again with the same answers…" (`:3998`) and "Install, turn on and off, and uninstall plugins" (`:4025`).
  - Rows with no help at all:
    - Automation: Run the last loaded script, Rerun last macro, Cancel macro, Show panel;
    - View: Layout ▸, the side bar rows, Reset side bars, Panels ▸, Move panel…, Restore the previous layout, Log, both Hide tags rows.
  - The rule is one third-person clause starting with the verb (text-style "Kinds of text"). Help (`:4266-4315`), Node effects and Explorer are already right.
- **Recommend:**
  - **Fix the four wrong texts:**
    - "Loads an Automation script from a file"
    - "Loads the autoload folder’s scripts again"
    - "Runs the last macro again with the same answers, without its dialogs when they are unchanged"
    - "Installs, turns on and off, and uninstalls plugins"
  - **Add help to the rows that have none**, for example:
    - "Shows or hides the Log"
    - "Shows or hides tags in the Grid"
    - "Puts the side bars back as they started"
  - FE11 covers the same pattern in File and Edit.
- **Visible:** yes (status bar).

### VW11. flat-list (uncertain): View › Panels lists 19–21 panels in registry order, with no groups

- **Tag:** flat-list.
- **Evidence:**
  - `Main.qml:925-933` builds the list in registry order: Video … Template editor, then QC notes, Review, Agent, Graph, Node, Source lines and Explorer, in the order they were added.
  - Log sits between Spellchecker and Styles.
  - The three node effects panels sit among the general ones with one-word names ("Graph", "Node").
  - There are no separators (`Main.qml:4117-4148`).
- **Recommend:** keep the rows and add separators, in this order:
  1. Video, Audio, Grid, Line editor, Original text, Reference
  2. `|` Shift times, Timing postprocessor, Find, Spellchecker, Styles, QC notes, Template editor, Automation, Explorer, Log
  3. `|` Review, Agent (when on)
  4. `|` Graph, Node, Source lines
  5. `|` {plugin panels}

  Use a separate display order, so that `panels` and `dockList` (registry indices) keep their order. **Uncertain:** the owner may want registry order, which the Move panel chooser also uses.
- **Visible:** yes.

### VW12. test-coverage: menu_bar.txt doesn't pin View › Panels › *panel* rows or the Automation separator rule

- **Tag:** test-coverage.
- **Evidence:**
  - `outlineMenu` recurses only one level (`tests/ui/shell_tests.cpp:6543-6548`, `depth < 1`). So `menu_bar.txt:200-222` lists "View > Panels > Video >" and so on with no rows.
  - CX6's approved 16 rows (Show, Float/Dock, `|`, Close) and the plugin panel's Remove from the layout can change without the listing failing.
  - The listing also prints hidden separators, such as the two "View > Layout > -" lines.
- **Recommend:** outline View › Panels one level deeper, for one panel only (e.g. "View > Panels > Video > Show" … "Close"), so that the 21 copies don't swamp the file. Also mark hidden rows, e.g. a trailing " (hidden)", the way `context_menus.txt` lists conditional rows.
- **Visible:** no.

### VW13. missing-expected (low): disabled View rows don't say why

- **Tag:** missing-expected.
- **Evidence:**
  - View › Panels › Original text › Show is greyed outside Translation mode (`Main.qml:4127`, `1318-1320`).
  - Node effects is greyed without an editing target (`:2896`).
  - Every layout-changing row is greyed while the layout is locked (Show, Float, Close, Move panel…, Restore, Reset side bars, Explorer, Log).
  - None sets `reason`, which `ShellMenuItem` draws in place of the key (`ShellMenuItem.qml:118`). LayoutMenu already uses it for Lock layout and hidden layouts.
  - Audit section 1 raised the same thing for the Grid menu.
- **Recommend:**
  - `reason: qsTr("Only in Translation mode")` on Original text's Show.
  - `reason: root.lockReason`-style text "Layout is locked" on the rows greyed by the lock.
  - "Open a file first" on Node effects.
- **Visible:** yes.

### VW14. flat-list (low): Help has 8 rows and no separator

- **Tag:** flat-list.
- **Evidence:** `Main.qml:4263-4319` has no separator. Learning rows, support rows, the update check and program info form one run.
- **Recommend:**
  1. Getting started, User guide, What’s new
  2. `|` Report an issue, Open user folder
  3. `|` Check for updates
  4. `|` About, Credits

  Regenerate `menu_bar.txt`.
- **Visible:** yes.

### VW15. doc drift (low): approved 16 drew Show disabled on a shown panel; the code keeps it enabled

- **Tag:** inconsistent-name (doc against code).
- **Evidence:**
  - `docs/ux/context-menus.md:1157` draws "Show (show; disabled)" for the shown Reference panel.
  - Since 1d122c203, Show has been `enabled: root.panelAvailable(modelData) && !root.layoutLocked` (`Main.qml:4127`).
  - It brings a panel forward from behind another tab, which is useful, so the code is right.
- **Recommend:** keep the code. In the "What CX7 built" note, say that Show stays enabled on a shown panel and raises it.
- **Visible:** no.

### VW16. missing-expected: Help has two "G" mnemonics

- **Tag:** missing-expected (keyboard).
- **Evidence:**
  - "&Getting started" (`Main.qml:2921`) and "User &guide" (`:2926`) both take **G**.
  - The other Help letters (R, U, F, N, A, C) are used once.
  - Underlines show with Alt (`MainTitleBar.qml:50`).
  - **Uncertain:** whether Qt Quick's Menu cycles between them or fires the first, as in FE10.
- **Recommend:** "Getting &started" (S) and "User &guide" (G).
- **Visible:** yes (underlines).

## Checked and clean

- **Duplicates with context menus:**
  - View › Panels › *panel* has Show, Float/Dock, `|`, Close, with the same labels and icons as the panel ⋯ (approved 16).
  - Lock layout appears in View › Layout, the switcher and the panel ⋯, with the same label, icon and reason (DK4).
  - Move panel… matches the panel ⋯'s row.
  - Load script… matches the Automation panel ⋯ in name and icon (only Refresh differs, VW5).
  - Node effects is the same Action as the title bar button.
- **Duplicates within the menu bar:**
  - Automation › Show panel is View › Panels › Automation › Show.
  - Explorer and Log duplicate their Panels rows (VW2).
  - These are quick routes kept since legacy, apart from Explorer (PJ4). They are not raised beyond VW2.
- **Icons:**
  - Every non-exempt row has one.
  - The exempt rows are the check items, the panels under Panels ▸ (named by data), the plugin ▸, the macros and the » titles.
  - The two shared icons are VW4 and VW5.
  - Plugin panel submenus carry `plugins` and the plugin's name as detail text, which marks them apart from the built-in panels.
- **Mnemonics:**
  - View: L, P and M, each used once.
  - Automation: L, P and U, each used once.
  - Help: G is duplicated (VW16).
- **Keys:** only Toggle left side bar {Ctrl+Alt+B} and User guide {F1} are bound by default. ADR 0032 Q438 decided the side bar keys. No label writes a key.
- **Destructive rows last:** none of the three menus has a destructive row. In the plugin panel ▸, Remove from the layout comes last.
- **British English and sentence case:** no violations.
- **The » overflow:** it uses menu-more for overflow only, and its tooltip says what it holds ("More menus"), which rule 17 keeps.
