# CX7 inventory D: Automation, AI, plugins, settings, dialogs

Read from source at `origin/main` 8dbf88f41. QML is the source of truth. Notation as in `docs/ux/context-menus.md` lines 1–16. Paths are `src/ui/` unless given.

Scope check: `grep -l 'ShellMenu {' src/ui` shows these extra files in this area: `InlineAiMenu.qml` (AI ▸, shared by Grid and Line editor menus; listed here once), `TierSaveMenu.qml` (shared dropdown). `NodeLibraryMenu.qml`, `NodePresetMenu.qml`, `NodeModeBar.qml` (Node effects) and `PhantomMenu.qml` are not in this slice. In `Main.qml`, the AI-related menus are the `reviewMenu` (Edit › Review ▸, menu bar, out of scope), the Automation menu bar's plugin and macro entries (dropdown section) and the plugin rows of the panel ⋯ (menu D12).

---

## Menus

### D1. Automation panel: right-click a macro (approved menu 23)

Opens on a right-click on a macro row (`AutomationManager.qml:1127-1133`), or the Menu key / Shift+F10 on the macro list (`:484-487`). Defined at `AutomationManager.qml:1638-1669` (`automationMacroMenu`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Run | — (runs the macro) | script ready, controller not busy; key column shows the macro's own shortcut (`shortcutText` = `keysOf(legacyName)`) | run-script |
| | `|` | | | |
| 2 | Set shortcut… | — (records a shortcut) | hotkeys available | hotkeys |
| 3 | Clear shortcut | — | macro has a shortcut | clear |

Items: 3 top level, 3 in all, 0 submenus, 1 separator. Matches `tests/ui/context_menus.txt:348-352`.

### D2. Automation panel: script row (right-click and ⋯) (approved menu 24)

Opens on a right-click anywhere on a script header (`AutomationManager.qml:990-993`), on the ⋯ button (`:1056-1064`, iconRole panel-menu, tooltip “%1 options”) and with the Menu key on that button (`:1063`). Defined at `AutomationManager.qml:1601-1636` (`automationScriptMenu`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Reload | — | state not running, loading or disabled | refresh |
| 2 | Edit script | — | not a built-in script | edit |
| | `|` | | | |
| 3 | Unload | — | not running, not disabled, not built-in | unload |

Items: 3 top level, 3 in all, 0 submenus, 1 separator. Not in `context_menus.txt` (see D-F10).

### D3. Automation panel: recommended script (right-click and ⋯) (approved menu 29)

Opens on a right-click on an installed row (`AutomationManager.qml:1386-1393`), the row's ⋯ (`:1471-1481`, panel-menu, tooltip “%1 options”), and the Menu key on the list (`:553-556`). Rows not installed have no menu. Defined at `AutomationManager.qml:1520-1552` (`recommendedMenu`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Show in Scripts | — | entry has a loaded path | automation |
| 2 | Open project page | — | always | open-link |
| | `|` | | | |
| 3 | Uninstall… | — | state installed; key Del (`standardKey`); installed by hand: disabled, detail “Installed by hand” | delete |

Items: 3 top level, 3 in all, 0 submenus, 1 separator. Matches `context_menus.txt:353-357`.

### D4. Automation script dialog ⋯ (approved menu 25)

The ⋯ in the dialog's button row (`AutomationDialog.qml:246-268`, panel-menu, tooltip “Answers and presets”), shown when the dialog remembers answers. Defined at `AutomationDialog.qml:269-336`; tier submenus `:403-426`, placed by `placeTierMenus()` `:111-123`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Last used | — | has last-used values | dialog-memory |
| 2 | Presets ▸ | | | dialog-memory |
| 2.1 | {dyn} presets | — | hidden-by-project rows disabled; detail Project / Mine with a Project | project / mine (with a Project), none otherwise |
| | `|` (only when presets exist) | | | |
| 2.2 | Save as preset… | — | always | save-as |
| | `|` | | | |
| 2.3 | Delete preset ▸ {dyn} | — | any preset; rows ask “Delete the preset “%1”?” | delete |
| 2.4 | Copy to project ▸ {dyn} | — | Project only; any Mine preset | copy-to-tier |
| 2.5 | Copy to mine ▸ {dyn} | — | Project only; any project preset | copy-to-tier |
| 2.6 | Move to project ▸ {dyn} | — | Project only | move-to-tier |
| 2.7 | Move to mine ▸ {dyn} | — | Project only | move-to-tier |
| | `|` | | | |
| 3 | Reset to the script’s values | — | always | restore-default |

Items: 3 top level, 7 in all without a Project (15 with one), 2 submenus (6 with a Project), 3 separators.

### D5. Shapes dialog ⋯ (M12)

The ⋯ under the shape list (`ShapesEditionDialog.qml:281-288`, panel-menu, tooltip “More”). Defined at `ShapesEditionDialog.qml:521-551`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Copy to project / Copy to mine | — | visible with a Project; label by the selected shape's tier | copy-to-tier |
| 2 | Move to project / Move to mine | — | visible with a Project | move-to-tier |
| | `|` (with a Project) | | | |
| 3 | Restore default shapes… | — | asks | restore-default |
| | `|` | | | |
| 4 | Undo all changes | — | always | undo |

Items: 2 top level without a Project, 4 with one; 0 submenus; 1–2 separators.

### D6. Shapes dialog: right-click a shape (new, PJ5b #1057)

Right-click on a shape row, only with a Project (`ShapesEditionDialog.qml:199-206`). No Menu-key route. Defined at `:494-511` (`shapeTierMenu`).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Copy to project / Copy to mine | — | by the row's tier | copy-to-tier |
| 2 | Move to project / Move to mine | — | | move-to-tier |

Items: 2 top level, 2 in all, 0 submenus, 0 separators.

### D7. All tags dialog ⋯ (M13)

`AllTagsEditionDialog.qml:444-451` (button, panel-menu, tooltip “More”); menu `:730-760`. Same rows as D5 with “Restore default tags…”. Items: 2 top level without a Project, 4 with one; 0 submenus; 1–2 separators.

### D8. All tags dialog: right-click a tag (new, PJ5b #1057)

`AllTagsEditionDialog.qml:356-363` (only with a Project, not on section headers); menu `:703-720` (`tagTierMenu`). Same rows as D6. Items: 2, 0 submenus, 0 separators.

### D9. Shift times and Timing postprocessor: preset ⋯ (new, #598 via #674; tier rows PJ5c #1046)

The ⋯ beside the Preset combo box (`ShiftPresetBar.qml:132-138`, panel-menu, tooltip “Preset commands”). Used by `ShiftTimesPanel.qml:136` and `TimingPostprocessorPanel.qml:165`. Defined at `ShiftPresetBar.qml:139-192`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Save as preset… | — | always | save-as |
| 2 | Rename preset… | — | a preset is chosen, not read-only | edit |
| 3 | Delete preset… | — | same; asks | delete |
| | `|` (with a Project) | | | |
| 4 | Copy to project / Copy to mine | — | with a Project; a preset is chosen | copy-to-tier |
| 5 | Move to project / Move to mine | — | same | move-to-tier |
| | `|` (Timing postprocessor only) | | | |
| 6 | [x] Use in the Timing layout | — | Timing postprocessor only; a preset is chosen | none (check) |

Items: 3 top level (Shift times, no Project) up to 6; 0 submenus; 0–2 separators.

### D10. Agent panel: thread ⋯ (new, AI7b #864; compaction AI21 #1000)

The header's ⋯ (`AgentPanel.qml:192-199`, iconRole **menu-more**, tooltip “Thread menu”), shown for a top-level thread. No right-click on a thread row or the conversation; no Menu-key route. Defined at `AgentPanel.qml:681-723` (no objectName).

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Compact conversation | — | can compact, not compacting | compact |
| 2 | New thread from summary | — | can compact, not running | add |
| 3 | Rename thread… | — | always | edit |
| 4 | Export as Markdown… | — | always | export |
| | `|` | | | |
| 5 | Delete thread | — | asks “Delete this thread?” | delete |

Items: 5 top level, 5 in all, 0 submenus, 1 separator.

### D11. AI ▸ (shared submenu; new, AI8 #965/#978)

`InlineAiMenu.qml:6-91`. Instances: Line editor text menu (`Main.qml:4870`), Line editor AI button (`:7130`), Grid key menu (`:7326`), Grid row menu (`:7524`). Hidden (`shown: false`) when AI is off. Listed in `context_menus.txt:40-48` and `:83-91`; the Grid/Line editor slices own its place in those menus.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Rewrite… | — | chat model ready; detail “%n line(s)” in the Grid | edit |
| 2 | Shorten to %1 CPS | — | chat model ready | compact |
| 3 | Fix spelling and grammar | — | reviewer ready | dictionary |
| 4 | Translate to %1 | — | translator ready | language |
| 5 | Translate to… | — | translator ready | language |
| 6 | {dyn} custom AI actions | — | per action's role; some ask each time | ai-assistant |
| | `|` | | | |
| 7 | Repeat last AI action | GLOBAL_REPEAT_AI_ACTION (bound; detail = last action's label when unbound) | a last action exists | repeat-run |
| 8 | Edit actions… / Add a model in Settings › AI… | — | label by whether any model is ready | settings |

Items: 8 top level, 8 in all, 0 submenus, 1 separator.

### D12. Plugin-contributed rows

**Where plugins can add menu rows** (`docs/plugins.md:215`, `:239`; ADR 0022 line 68 says the API cannot add Grid menu entries):

1. **Plugin panel ⋯**: a panel root's `menu = {{id, text, checked, disabled}}`, at most 30 rows (`src/plugins/surface.cpp:183`, `:285-296`). They lead the panel ⋯ (`Main.qml:1482-1492` `pluginPanelItems`, placed by `panelOwnItemsInstantiator` `:1586-1592`), followed by the panel separator (`:1657-1661`), only while the plugin runs. A row with `checked` is a check item. No icon field (rows get `iconRole ""`); no plugin mark beyond being in the plugin's own panel. The panel ⋯ also gets **About %1…** (about, `Main.qml:1758-1765`) before Close. (PL4 #722.)
2. **Automation menu bar** (not a context menu): one submenu per running plugin, titled with the plugin's name, holding its commands (`src/app/plugin_runtime.cpp:661-668` `titles = {name, title}`; `Main.qml:4030-4043`). They follow Plugins… with no separator; the macros come after `macrosSeparator`. Submenus and rows have no icon and show no shortcut. (PL2 #683.)
3. **Plugin dialogs** (`hikari.dialogs.show`) and QML panels can draw their own controls; there is no API for a ShellMenu or for rows in Grid, Line editor, video, tab or other right-click menus.

Plugin panel ⋯ items: {dyn} 1 (up to 30 rows) plus About %1…; counted in the panels slice's menu.

---

## Other menus (dropdowns)

- **Agent panel** (all AI7b #864, `AgentPanel.qml`):
  - `@` / Attach (`:725-741`, from the `@` key `:614-615` and the Attach button `:624-630`): {dyn} open Documents (open-subtitles).
  - `/` slash menu (`:743-774`, when the composer holds “/”): Compact conversation (compact; detail /compact), New thread (add; /new), Change mode (ai-assistant; /mode). 3 items.
  - Model ⌄ (`:776-798`): {dyn} models as check items, text “model   provider” | Settings › AI… (settings).
  - Mode ⌄ (`:800-842`, `agentModeMenu`): Manual, Auto, Full access (check items with a description) | Default for new threads… (settings).
- **Template editor** (`TemplateEditor.qml`): Apply ⌄ (`:872-891`): Apply to every karaoke line (karaoke), Apply to the selected lines (select-lines). New (`:893-948`): 9 template/code kinds (template-editor / run-script; effect in the detail column) | From a preset ▸ ({dyn}; karaOK presets get notice-info and are disabled with a reason outside karaOK mode) | note label.
- **Settings**: More resets (`SettingsDialog.qml:278-299`, chevron-down): Reset this page, Reset all settings… (both restore-default). Settings › AI › Add provider (`SettingsAiPage.qml:1377-1390`): {dyn} provider presets with their icons. Settings › AI › MCP Copy setup (`SettingsAiPage.qml:2268-2291`, AI3b #820): Claude Code command (run-script), Codex config.toml (settings), JSON with the address and token (network; reason line “Holds your token; keep it private.”).
- **Save tier split button** (`TierSaveMenu.qml`, PJ5b #1057 / PJ5d #1068): Save to project (project), Save to mine (mine), check items. Used in preset popovers, Save layout and the Spellchecker.
- **Automation menu bar macros** (`MacroMenuEntries.qml`, used at `Main.qml:4030`, `:4048`): {dyn} macros and plugin commands, “/” in a name opens a submenu.

---

## Changed since CX2 (1d122c203)

| Menu | Change | PR |
|---|---|---|
| D1 macro | Run: media-play → run-script, shows the macro's shortcut; separator before Set shortcut… (approved 23.1) | CX6 #721 |
| D2 script | Right-click on the script header and Menu key on ⋯ open it (approved 24.1). Edit script and Unload disabled for built-in scripts. | CX6 #721; TH5b #939 |
| D3 recommended | **New menu** (approved 29) | FA6 #739 |
| D4 dialog ⋯ | Presets ▸ icon save-as → dialog-memory; Delete preset ▸ behind a separator; Reset icon refresh → restore-default (approved 25.1). Preset rows gain tier icon and Project/Mine detail; **added** Copy to project ▸, Copy to mine ▸, Move to project ▸, Move to mine ▸ after Delete preset ▸ (with a Project). | CX6 #721; PJ5c #1046 |
| D5/D7 Shapes/All tags ⋯ | **Added** Copy to …, Move to … and a separator at the top (with a Project). Restore/Undo all unchanged. | PJ5b #1057 |
| D6/D8 row right-click | **New menus** | PJ5b #1057 |
| D9 preset ⋯ | **New menu** (Save as preset…, Rename preset…, Delete preset…, Use in the Timing layout); tier rows added later | #674 (#598); PJ5c #1046 |
| D10 thread ⋯ | **New menu**; Compact conversation and New thread from summary added | AI7b #864; AI21 #1000 |
| D11 AI ▸ | **New submenu** in Grid and Line editor menus | AI8 #965, #978 |
| D12 plugin rows | **New**: plugin panel ⋯ rows + About %1…; Automation menu plugin submenus | PL4 #722; PL2 #683 |
| Dropdowns | **New**: Agent attach, slash, model, mode (AI7b #864); Settings AI Add provider (AI20 #728), MCP Copy setup (AI3b #820); TierSaveMenu (PJ5b/PJ5d). Template New: effect id moved from a tab in the label to the detail column (CX3 #638), karaOK effects and karaOK preset rows (TH5b #939, TH5d #981, TH5d3 #1003). Settings Reset all settings… icon settings → restore-default (IC2 #551). | |

Nothing removed or renamed in D1–D3 since the CX2 design.

---

## Findings

**D-F1. inconsistent-name (icon): the thread ⋯ uses the » overflow icon.** `AgentPanel.qml:194-195` gives the header's menu button `menu-more` and the tooltip “Thread menu”. CX6 kept » (menu-more) for overflow only; every other ⋯ in the app uses `panel-menu`, and text-style.md names the ⋯ tooltip “More options”. Recommend `iconRole: "panel-menu"`, tooltip “More options”. Visible change: yes.

**D-F2. inconsistent-name: ⋯ tooltips have five names.** Script row “%1 options” (`AutomationManager.qml:1061`), recommended row “%1 options” (`:1476`), script dialog “Answers and presets” (`AutomationDialog.qml:250`), Shapes and All tags “More” (`ShapesEditionDialog.qml:285`, `AllTagsEditionDialog.qml:448`), preset bar “Preset commands” (`ShiftPresetBar.qml:136`), Agent “Thread menu” (D-F1). text-style.md “One name per thing”: “A menu of more items | More options (tooltip of ⋯) | Not: %1 options”. Recommend tooltip “More options” on all; keep the row's name in the accessible description where several rows each have a ⋯ (script and recommended lists). Visible change: yes.

**D-F3. destructive-next-to-common: preset ⋯ puts Delete preset… between Rename and the tier rows.** `ShiftPresetBar.qml:142-191`: Save as preset…, Rename preset…, Delete preset… share a group, then Copy/Move, then the check item. CX2 rule: destructive last, after a separator. Recommend: Save as preset…, Rename preset… | Copy to …, Move to … | [x] Use in the Timing layout | Delete preset…. Visible change: yes.

**D-F4. destructive-next-to-common: script dialog Presets ▸ puts Delete preset ▸ first in the tier group.** `AutomationDialog.qml:120` inserts Copy to project ▸, Copy to mine ▸, Move to project ▸, Move to mine ▸ right after Delete preset ▸ (`:310-329`), so with a Project the destructive submenu leads a group of five. Approved 25 has Delete preset alone and last behind a separator. Recommend: {dyn} | Save as preset… | Copy to project ▸, Copy to mine ▸, Move to project ▸, Move to mine ▸ | Delete preset ▸ (insert the tier menus before the separator above Delete preset). Visible change: yes.

**D-F5. missing-expected / keys: Automation menu macros and plugin commands show no shortcut.** `MacroMenuEntries.qml:19-25` makes rows with no `command` or `shortcutText`; the panel's macro menu shows the same macro's key (`AutomationManager.qml:1647`), and plugin commands have global shortcuts (`docs/plugins.md:215`). Recommend `shortcutText` from `automationHotkeys`/the plugin command's binding for each row. Visible change: yes. (Menu bar, outside the context-menu scope; reported because the component is in this slice.)

**D-F6. no-icon: plugin panel ⋯ rows have no icon and plugins can't give one.** `Main.qml:1487-1491` builds rows without `iconRole`; the surface schema allows only `id, text, checked, disabled` for menu rows (`src/plugins/surface.cpp:288-290`), while plugin buttons already take an icon-set role (`surface.cpp:37-38`, `T::Icon` `:235-236`). Non-check plugin rows sit at the top level of the panel ⋯, not under a submenu with an icon, so the “named by their data” exemption doesn't cover them; `everyMenuItemHasAnIcon` reads QML only and can't see them. Recommend an optional `icon` (T::Icon) on menu rows, passed as `iconRole`, and a line in `docs/ux/menu-icon-gaps.md` exempting rows that give none. Visible change: yes (for plugins that set it).

**D-F7. text-style ellipsis: “About %1…” in a plugin panel ⋯.** `Main.qml:1761`. It opens Plugins at that plugin (a window you look at); text-style.md lists About among names without “…”, and Help › About has none (`tests/ui/menu_bar.txt:239`). Recommend “About %1”. Visible change: yes.

**D-F8. text-style ellipsis: Settings rows end in “…”.** Agent model menu “Settings › AI…” (`AgentPanel.qml:795`), mode menu “Default for new threads…” (`:839`), AI ▸ “Add a model in Settings › AI…” (`InlineAiMenu.qml:88`). All open Settings, which text-style.md names without an ellipsis. Recommend “Settings › AI”, “Default for new threads”, “Add a model in Settings › AI” (the last is in `context_menus.txt:48`, `:91`; regenerate). Uncertain for “Edit actions…” (same line), which opens the same page; I'd drop its “…” too. Visible change: yes.

**D-F9. text-style ellipsis: rows that ask have no “…”.** Agent “Delete thread” asks “Delete this thread?” (`AgentPanel.qml:711-722`), while Uninstall…, Delete preset…, Restore default shapes… and Reset all settings… carry it. AI ▸ custom actions with `ask` set open the instruction box (`InlineAiBox.qml:47-56`) but show the bare name (`InlineAiMenu.qml:65`), unlike Rewrite… and Translate to…. Recommend “Delete thread…”, and `qsTr("%1…").arg(name)` for custom actions with `ask`. Visible change: yes.

**D-F10. missing-expected (ledger): three right-click menus are not in `tests/ui/context_menus.txt`.** The script row right-click (`AutomationManager.qml:990-993` → `automationScriptMenu`), the shape row (`ShapesEditionDialog.qml:199-206`) and the tag row (`AllTagsEditionDialog.qml:356-363`) open menus that `contextMenusAreTheListedOnes` (`tests/ui/shell_tests.cpp:6612-6631`) doesn't list. CX3 made the listing a ledger of every right-click menu. Recommend adding “Automation script” (`automationScriptMenu`), plus “Shapes row” and “All tags row” if the test can open those dialogs with a Project. Visible change: no.

**D-F11. missing-expected: no Menu-key route for the shape and tag row menus.** The lists take Tab focus (`ShapesEditionDialog.qml:160`), but neither file has `MenuKey.onRequested`. CX3: a focusable row with a right-click menu opens it with the Menu key and Shift+F10. Recommend `MenuKey.onRequested: if (dialog.tiered) tierMenu.popup(currentItem…)` on both lists. The same rows are in the ⋯, so this is a small gap. Visible change: no (keyboard only).

**D-F12. missing-expected: Agent panel rows have no right-click menu.** A thread row (`AgentPanel.qml:304-342`) can only be renamed, exported or deleted after you open it and use ⋯ (D10). An answer is a read-only `TextEdit` (`:949-953`), which gets no text menu, so Copy works only with the keyboard (the same gap CX1 found in the Log). Recommend: right-click on a thread row opens D10 for that thread (Menu key too); answers get a text menu with Copy and Select all. Visible change: yes.

**D-F13. inconsistent-name (icon): the macro row's inline Run button uses media-play.** `AutomationManager.qml:1180`. Approved 23 moved Run to run-script and kept media-play for playback, but the button next to the same macro still shows the play icon. Recommend `iconRole: "run-script"`. Visible change: yes.

**D-F14. inconsistent-name (icon): MCP Copy setup reuses icons for other meanings.** `SettingsAiPage.qml:2273` Claude Code command = run-script (“Run the last loaded script”), `:2279` Codex config.toml = settings. One icon, one meaning (menu-icon-gaps.md). Recommend `edit-copy` for all three rows (the button's own icon), or new icons if the formats need telling apart. Visible change: yes. Dropdown, low priority.

**D-F15. toggle-hides-state (choice style): exclusive choices drawn as check items.** Agent model menu (`AgentPanel.qml:780-784`) and mode menu (`:808-813`), and `TierSaveMenu.qml:33-50`, list one-of-n choices with `checkable: true`. CX5 draws such choices as radio rows (`MenuRadioItem.qml`), as in Audio track ▸ and the bar modes. Recommend `MenuRadioItem`. Visible change: yes. The model rows also join model and provider with three spaces (`:782`); Settings › AI joins them with “ · ” (`SettingsAiPage.qml:1415`). Recommend the provider in `detailText`, or “ · ”.

**D-F16. Plausible, unverified: hidden tier rows in the Shapes and All tags ⋯ may leave gaps.** `ShapesEditionDialog.qml:524-538` and `AllTagsEditionDialog.qml:733-747` hide Copy/Move and the separator with `visible: dialog.tiered` alone. Every other hidden menu row in the app also sets `height: visible ? implicitHeight : 0` (`ShiftPresetBar.qml:162-185`, `Main.qml:1645-1660`, and `ShellMenuItem.qml:19-20` for submenus), which suggests a hidden row keeps its height in a Qt Menu. If so, a document without a Project shows two empty rows and a gap above Restore default…. Recommend adding the same `height` binding. Visible change: yes, if the gap is real; check on screen.

**D-F17. flat-list (watch): open-ended rows.** AI ▸ appends every custom AI action between Translate to… and the separator (`InlineAiMenu.qml:57-71`), with no cap. A plugin panel's ⋯ may lead with up to 30 plugin rows before the panel's own (`surface.cpp:287`). Recommend a separator before custom actions (or an “Actions ▸” submenu past a few), and a lower cap or a separator after plugin rows. Visible change: yes. Low priority: no user has many yet.

**D-F18. inconsistent-name (uncertain): “conversation” and “thread” in one menu.** D10 has Compact conversation and New thread from summary next to Rename thread… and Delete thread (`AgentPanel.qml:684-711`). CONTEXT.md defines neither term. If “thread” is the name, use “Compact thread”, and the slash menu and Grid's compact role follow it. Needs an owner call. Visible change: yes.

Clean: D1 and D3 match approved menus 23 and 29 and `context_menus.txt`. D2 matches 24. D5/D7 kept their approved rows (no change except the tier group). The Template editor dropdowns and Settings resets meet the “left as they are” list; Settings' “Reset page” button vs “Reset this page” row is the approved CX2 state. No hard-coded keys in menu labels; every non-check static row has an icon.

Totals: 12 menus (D1–D12; D12 is plugin rows inside the panel ⋯), 11 dropdown groups listed briefly, 18 findings (D-F16 plausible, D-F18 needs an owner call).
