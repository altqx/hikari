# Left and right side bars like VS Code: research

The owner's request: "add support for side bar left and right like vscode, where the right sidebar will be explorer, find and replace, shift time and more things that we can put in there, and everything can be customized, and the left side will be AI stuff by default; left and right can switch place."

Researched 2026-11-12. Sections 1 to 5 are facts. Recommendations appear only in section 6 ("Options"), which is marked.

## Sources and pins

- **HikariSub.** `origin/main` at `8d606a93a` (2026-11-12 21:05). The local `main` checkout (`a6d62e58e`) is 229 commits behind, so every repo `file:line` below refers to `origin/main`. Read it with `git show origin/main:<path>`. The Explorer is not on `origin/main` yet. Where it is cited, the source is the uncommitted PJ4 work in `~/.t3/worktrees/hikari/w8-pj4-f55933da70ab95ce` and is marked **(PJ4 WIP)**.
- **KDDockWidgets.** v2.5.1 at commit `674f28453219dddedafd4b8f33cf094e8e546be3`, the pin in [ports/kddockwidgets/portfile.cmake](../../ports/kddockwidgets/portfile.cmake) (`REF 674f2845…`) and `ports/kddockwidgets/vcpkg.json` (`"version": "2.5.1"`). It is built for QtQuick only (`-DKDDockWidgets_FRONTENDS=qtquick`, portfile.cmake). `KD/` below means `https://github.com/KDAB/KDDockWidgets/blob/674f28453219dddedafd4b8f33cf094e8e546be3/`.
- **VS Code.** Release `1.146.0`, the latest stable (2026-11-11). `VS/` below means `https://github.com/microsoft/vscode/blob/1.146.0/`. "Layout docs" means https://code.visualstudio.com/docs/configure/custom-layout.
- **MuseScore.** Tag `v4.7.7`, the latest 4.x tag. `MS/` below means `https://github.com/musescore/MuseScore/blob/v4.7.7/`. The repo's own [docs/research/musescore-docking.md](../research/musescore-docking.md) read v4.7.5, and the docking code it cites did not change in ways that matter here. The handbook now lives at https://handbook.musescore.org.

---

## 1. VS Code's model

### 1.1 Parts, view containers and views

- The workbench is made of parts: the Activity Bar, the side bar (Primary Side Bar), the panel, the auxiliary bar (Secondary Side Bar), the editor and the status bar. `VS/src/vs/workbench/services/layout/browser/layoutService.ts#L22-L31`
- A **view container** lives in one of three locations, `ViewContainerLocation { Sidebar, Panel, AuxiliaryBar }`. `VS/src/vs/workbench/common/views.ts#L39-L43`
- A **view** is a pane inside a container. A view's descriptor has `canToggleVisibility` and `canMoveView`. A container can have `hideIfEmpty` and `rejectAddedViews`. `VS/src/vs/workbench/common/views.ts#L127`, `#L138`, `#L300-L302`
- Extensions add containers under `contributes.viewsContainers`, with the keys `activitybar`, `panel` and `secondarySidebar`. These map to the three locations. `VS/src/vs/workbench/api/browser/viewsExtensionPoint.ts#L60-L81`, `#L316-L326`

### 1.2 Primary Side Bar, Secondary Side Bar, Activity Bar

- **Primary Side Bar.** It is on the left by default and shows views such as Explorer, Search and Source Control. The Activity Bar's icons switch between them. (Layout docs, "Primary Side Bar")
- **Secondary Side Bar.** It always sits opposite the Primary Side Bar, also after the primary has moved. (Layout docs, "Secondary Side Bar")
  - Its minimum width is 170. Its preferred width is the larger of the view's optimal width and 300. `VS/src/vs/workbench/browser/parts/auxiliarybar/auxiliaryBarPart.ts#L46-L79`
  - It switches containers with a horizontal composite bar, showing icons or labels (`workbench.secondarySideBar.showLabels`). `auxiliaryBarPart.ts#L213-L222`; `VS/src/vs/workbench/browser/workbench.contribution.ts#L637-L642`
- **Activity Bar position.** The setting `workbench.activityBar.location` takes exactly `default`, `top`, `bottom` or `hidden`, and defaults to `default`. `VS/src/vs/workbench/browser/workbench.contribution.ts#L674-L686`
  - `default` puts it "on the side of the Primary Side Bar and on top of the Secondary Side Bar".
  - `top` and `bottom` put it above or below both side bars.
  - `hidden` hides it in both. (Same lines, L680-L683.)
  - Older settings are migrated: `activityBar.visible: false` becomes `hidden`, and `'side'` becomes `default`. `workbench.contribution.ts#L1050-L1075`
  - The Activity Bar's context menu has an "Activity Bar Position" submenu. `VS/src/vs/workbench/browser/parts/activitybar/activitybarPart.ts#L644-L700`
  - `workbench.activityBar.autoHide` (default false) hides the bar while only one container is shown. `workbench.contribution.ts#L687-L692`

### 1.3 Moving views and side bars

- **Drag and drop.** Views and containers can be dragged between the Primary Side Bar, the Secondary Side Bar and the Panel. Dropping one on an Activity Bar item groups them. VS Code remembers the result. (Layout docs, "Drag and drop views and panels")
  - Each part accepts drops only while `canMoveViews()` is true. `VS/src/vs/workbench/browser/parts/paneCompositePart.ts#L278-L300`
  - `canMoveViews()` is false only in the Sessions (agents) window. `VS/src/vs/workbench/services/views/browser/viewDescriptorService.ts#L350-L352`
- **"View: Move View"** (`workbench.action.moveView`) picks a view, then runs **Move Focused View** on it. `VS/src/vs/workbench/browser/actions/layoutActions.ts#L795-L830`, `#L942-L973`
  - The destinations come first as "New Panel Entry", "New Side Bar Entry" and "New Secondary Side Bar Entry". Then come the existing containers, grouped by location. Containers with `rejectAddedViews` are left out. `layoutActions.ts#L981-L1063`
  - A "new entry" creates a generated container. A generated container that becomes empty is removed. `viewDescriptorService.ts#L372-L406`, `#L398`
  - `workbench.action.resetFocusedViewLocation` puts one view back where it started. `layoutActions.ts#L1100-L1138`
  - There are also bulk moves: "Move Panel Views To Secondary Side Bar" and "Move Secondary Side Bar Views To Panel". `VS/src/vs/workbench/browser/parts/panel/panelActions.ts#L376-L425`
- **Switching sides.** The setting `workbench.sideBar.location` takes `left` (the default) or `right`. Its description says "The secondary side bar will show on the opposite side of the workbench." `workbench.contribution.ts#L585-L591`
  - `workbench.action.toggleSidebarPosition` flips the setting. It is labelled "Move Primary Side Bar Right/Left". `layoutActions.ts#L143-L173`
  - The explicit commands are `moveSideBarRight` and `moveSideBarLeft`. `layoutActions.ts#L122-L139`
  - The same toggle is offered on container title menus as "Move Secondary Side Bar Left/Right". `layoutActions.ts#L188-L231`; `auxiliaryBarPart.ts#L261-L271`
  - `setSideBarPosition` moves the Activity Bar and the side bar to the new side and gives the auxiliary bar the opposite one, so the two bars **swap as a pair**. `VS/src/vs/workbench/browser/layout.ts#L653-L680`

### 1.4 Toggle and focus commands, default keys

| Command | Default key | Source |
|---|---|---|
| Toggle Primary Side Bar (`toggleSidebarVisibility`) | Ctrl/Cmd+B | `layoutActions.ts#L288-L310` |
| Toggle Secondary Side Bar (`toggleAuxiliaryBar`) | Ctrl/Cmd+Alt+B | `VS/src/vs/workbench/browser/parts/auxiliarybar/auxiliaryBarActions.ts#L28-L53` |
| Toggle Panel (`togglePanel`) | Ctrl/Cmd+J | `panelActions.ts#L32-L51` |
| Focus into Primary Side Bar (`focusSideBar`) | Ctrl/Cmd+0 | `VS/src/vs/workbench/browser/parts/sidebar/sidebarActions.ts#L39-L46` |
| Focus into Secondary Side Bar (`focusAuxiliaryBar`) | none; it shows the bar first if hidden | `auxiliaryBarActions.ts#L103-L128` |
| Hide Secondary Side Bar (`closeAuxiliaryBar`), Close Primary Side Bar (`closeSidebar`) | none | `auxiliaryBarActions.ts#L88-L101`; `sidebarActions.ts#L18-L25` |
| Previous / Next Secondary Side Bar View | none | `auxiliaryBarActions.ts#L188-L200` |

### 1.5 Maximise, auto-hide, close, empty state, defaults

- **Maximise.**
  - "Maximize Secondary Side Bar" (`maximizeAuxiliaryBar`) has no key. `auxiliaryBarActions.ts#L209-L230`
  - Its restore is bound to Ctrl/Cmd+W while the bar is maximised. `auxiliaryBarActions.ts#L232-L256`
  - A toggle button sits in the bar's title. `auxiliaryBarActions.ts#L260-L290`
  - Maximising saves the width and the visibility of the side bar, panel and editor, then hides all three. Restoring brings them back. `layout.ts#L2179-L2242`
  - Showing any of those parts ends the maximise. `layout.ts#L1904`, `#L1949`, `#L2092`
  - An editor that opens by itself, for example from agent edits, does not end it. `layout.ts#L360-L378`
  - The panel's maximise (`toggleMaximizedPanel`) works only when the panel is centre-aligned or at the left or right. `panelActions.ts#L278-L320`
- **Auto-hide.** VS Code has no auto-hide that shows a side bar as an overlay. The only auto-hide is the Activity Bar's `autoHide` above.
  - The experimental `workbench.secondarySideBar.forceMaximized` keeps the bar maximised while no editor is open. `workbench.contribution.ts#L630-L636`
- **Close.**
  - A single view can be hidden ("Hide '{0}'") only if it may be hidden and its container keeps another visible view. `viewDescriptorService.ts#L869-L886`
  - A whole container can be hidden from a composite bar, which unpins it. `VS/src/vs/workbench/browser/parts/compositeBarActions.ts#L653`
  - Hiding the Secondary Side Bar moves the focus to the panel or the editor. Showing it again reopens the last active container. `layout.ts#L2290-L2327`
- **Empty state.**
  - Every side bar and the panel show **"Drag a view here to display."** when they hold nothing. `paneCompositePart.ts#L255-L263`
  - The part also gets the class `empty` and a cleared title. `paneCompositePart.ts#L707-L713`
  - When the auxiliary bar has no containers, VS Code records `auxiliaryBar.empty`, and the bar starts hidden next time. `layout.ts#L1749`, `#L3038-L3043`
- **Default contents.**
  - The Chat container is registered in the auxiliary bar with `isDefault: true`. `VS/src/vs/workbench/contrib/chat/browser/chatParticipant.contribution.ts#L42-L50`
  - The layout docs show Copilot Chat in the Secondary Side Bar.
  - `workbench.secondarySideBar.defaultVisibility` takes `hidden`, `visibleInWorkspace` (the default), `visible`, `maximizedInWorkspace` or `maximized`. It applies to workspaces opened for the first time. `workbench.contribution.ts#L616-L629`
  - New users get the bar shown unless the setting is `hidden` or AI is disabled (`ChatAIDisabledSettingId`). `layout.ts#L3029-L3065`

### 1.6 Persistence: per workspace or global

- **Saved per workspace** (`StorageScope.WORKSPACE`):
  - whether each bar is hidden (`sideBar.hidden`, `auxiliaryBar.hidden`, `panel.hidden`, `activityBar.hidden`);
  - `sideBar.position` and `panel.position`;
  - the maximise state;
  - the active container of each part.
  - `layout.ts#L2860-L2910` (WORKSPACE keys L2863-L2909); `layout.ts#L807`, `#L831`, `#L842`
- **Saved per profile**, which is global:
  - the bar sizes (`sideBar.size`, `auxiliaryBar.size`, `panel.size`, all 300 by default);
  - `panel.alignment`;
  - `auxiliaryBar.empty`.
  - `layout.ts#L2879-L2901`
- **View locations are global per profile.** Every view-to-container and container-to-location choice is one JSON key, `views.customizations`, at `StorageScope.PROFILE`. `viewDescriptorService.ts#L41`, `#L710-L723`, shape at `#L29-L33`
- The order and pinning of containers on a composite bar are per profile. Per-workspace container state is kept separately. `VS/src/vs/workbench/browser/parts/paneCompositeBar.ts#L697-L765`
- **Reset View Locations** (`workbench.action.resetViewLocations`) returns every view and container to its default and clears the saved locations. `layoutActions.ts#L776-L793`; `viewDescriptorService.ts#L408-L432`
  - The "Restore Defaults" button of Customize Layout resets the layout settings but **not** the view locations. `layoutActions.ts#L1607-L1631`
- **Customize Layout** (`workbench.action.customizeLayout`) is a quick pick. `layoutActions.ts#L1512-L1539`, `#L1362-L1403`
  - It has six sections: Visibility (Menu Bar, Activity Bar, both side bars, Panel, Status Bar), Primary Side Bar Position, Panel Alignment, Quick Input Position, Layout Density and Modes.

### 1.7 The bottom Panel

- `workbench.panel.defaultLocation` takes `left`, `bottom`, `top` or `right` (default `bottom`). It applies only to a new workspace. After that, `panel.position` is kept per workspace. `workbench.contribution.ts#L597-L603`; `layout.ts#L3067`, `#L2900`
- The commands are "Move Panel To Top/Left/Right/Bottom" and "Set Panel Alignment to Left/Right/Center/Justify". `panelActions.ts#L129-L180`
  - Centre alignment spans the editor area, Justify spans the window, and Left/Right run from that window edge. (Layout docs, "Panel alignment")
- Views move into the Panel the same way they move into the side bars ("New Panel Entry", drag and drop; §1.3).

---

## 2. HikariSub's docking today

### 2.1 How docks are made

- **One engine area per window.**
  - Each shell window has one `KDDW.DockingArea` (QML for KDDW's `MainWindowInstantiator`, `KD/src/qtquick/QmlTypes.cpp#L30`). Its `uniqueName` is `engineName("Classic")` and its `affinities` are `root.dockAffinities`. `src/ui/Main.qml:4192-4198`
  - It sets no `options`, so it has no central group or central widget: the Grid and the Video are ordinary docks like every other panel. There is no `options:` in `src/ui/Main.qml`; compare `MainWindowOption_HasCentralWidget` at `KD/src/KDDockWidgets.h#L78-L93`.
- **Window affinities.**
  - `dockAffinities` is `["w" + shellWindow]`, and `engineName(name)` adds the window's suffix. `src/ui/Main.qml:43`, `:48`
  - Every dock takes the same affinity, so a panel can dock only into its own window. `src/ui/workspace_layout.h:36-44` (`ShellWindowNames`); [ADR 0025](../adr/0025-multiple-windows.md) "Panels don't move between windows".
  - KDDW's rule is that a dock docks only into docks and main windows of the same affinity. `KD/src/core/DockWidget.h#L286-L312`; `KD/src/core/MainWindow.h#L132-L145`
- **PanelDock.** Every built-in panel is a `PanelDock` (21 in `Main.qml`).
  - `PanelDock` is a `KDDockWidgets::DockWidgetInstantiator`. Its `open()` and `show()` emit `openedWithoutPlace` instead of floating a panel that has no place. `src/ui/panel_dock.h:9-26`, `src/ui/panel_dock.cpp:38-58`
  - The shell answers with `openAtFirstPlace`. `src/ui/Main.qml:863-883`
- **The panel registry.**
  - `kPanelRegistry` is **a version number, not a list of ids**: it is `17`, and the comment reads "bumped per added panel (2 Timing … 12 QcNotes, 13 Review, 14 Agent, 15-17 node effects)". `src/ui/workspace_layout.h:68-69`
  - The ids are strings in `panelIds()`. `src/ui/workspace_layout.cpp:125-136`
  - A layout naming a higher registry is refused ("it names panels from a newer version"). `src/ui/workspace_layout.cpp:869-870`
- **Engine flags.**
  - HikariSub sets only `Flag_HideTitleBarWhenTabsVisible | Flag_AlwaysShowTabs`. `src/ui/docking.cpp:476`
  - The comment above it reads: "Not Flag_TitleBarNoFloatButton: KDDW reads it as part of Flag_AutoHideSupport and builds side bars QtQuick cannot." `src/ui/docking.cpp:376`

### 2.2 KDDW side bars and auto-hide are not used

- No HikariSub code calls `moveToSideBar`, `overlayOnSideBar` or `SideBarLocation`. A search of `src/ui` for these names finds only the comment at `docking.cpp:376`.
- The request mentions a recent fix around `panel_dock.cpp` and "non-sidebar docks". I found no commit on `origin/main` or its branches with "non-sidebar" in its message. The last change to `panel_dock.cpp` is DK2 `29fee9fe5` ("the engine's open() places a panel with no place too"), which brought in `PanelDock`. KDDW's own tests use the phrase "on side bar" (`KD/tests/qtwidgets/tst_qtwidgets.cpp#L183-L185`). Either way, HikariSub does not turn side bars on (§3.1).

### 2.3 Layouts, presets and persistence

- **The arrangement value.**
  - `WorkspaceArrangement` holds `preset`, the engine `payload`, the `layout` (the saved layout it came from) and `locked`. `src/ui/workspace_layout.h:22-34`
  - The envelope carries the schema, the panel-registry version, the engine version, the panel list and the payload. `src/ui/workspace_layout.cpp:347-365`; [docs/docking.md:37](../docking.md)
- **Validation.** It requires exactly one main window named `Classic` (`workspace_layout.cpp:883-887`) and refuses `mainWindows.size() > 1` (`:893`). Every floating window's parent must be `Classic` (`:955-962`).
- **Per Document tab.** Each tab keeps its own arrangement, and switching tabs restores it in place ([ADR 0020](../adr/0020-per-document-tab-layouts.md); [docs/docking.md:58](../docking.md)). In [CONTEXT.md:43-45](../../CONTEXT.md), "Workspace" means a tab's panel arrangement in its window.
- **Session.** Each tab's arrangement goes on a `Layout:` line. A line that fails validation gives the tab the Editing preset. [docs/docking.md:60](../docking.md)
  - Since ADR 0025 the Session also stores each window's geometry and tabs ([ADR 0025](../adr/0025-multiple-windows.md), "Restore and the Session").
- **`[Hikari]` section.** In ASS and SSA files it holds `Layout=` with the envelope in base64, without screen positions. [docs/docking.md:62](../docking.md)
- **Saved layouts.** They live in `layouts.json` and are global snapshots. View › Layout and the layout switcher list the presets, the saved layouts and three commands. [docs/docking.md:39](../docking.md)
- **The lock** belongs to the arrangement: command 5230 `GLOBAL_TOGGLE_LOCK_LAYOUT`. [docs/docking.md:54](../docking.md)
- **Presets.**
  - The presets are **Editing, Timing, Translation, Typesetting and Karaoke**. `src/ui/workspace_layout.cpp:497-502`
  - "Classic" is the main window's engine name, not a preset. `Main.qml:4196`
  - Node effects has a layout of its own that swaps in and out. `workspace_layout.h:117-120`; `Main.qml:292-305`
  - How each preset is built:
    - Editing: `defaultLayout`. Grid at the bottom, Video above it, Line editor to the right, Audio and Original text above the editor, Postprocessor right of Audio and closed, Reference under the Grid, Styles and Template as tabs with the editor. `Main.qml:273-290`
    - Typesetting: `Main.qml:317-344`.
    - Timing: closes the Video and opens the Postprocessor. `Main.qml:345-348`
    - Karaoke: `Main.qml:349-357`.
    - Translation has no builder of its own. The Original text follows Translation mode. `Main.qml:361-372`
- **Where a panel opens when it has no place.** This is the DK2 owner decision ([docs/docking.md:23](../docking.md); `Main.qml:863-883`, `:444-458`).
  - Log and Spellchecker go along the bottom.
  - Styles and Template become tabs with the Line editor.
  - Agent becomes a tab with Review if it is docked, otherwise it goes right of the Grid at a preferred 446 px. `Main.qml:437-442`
  - Plugin panels go where their manifest's `area` says.
  - **Every other panel (Shift times, Find, Automation, QC notes, Postprocessor…) goes in "the column right of the Grid".** The first one makes a split beside the Grid at no more than a third of its width. Later ones become tabs of the group against the Grid's right edge (`Docking.dockRightOf`).
  - The presets give these panels no place, and Reset layout closes them.
- **Move panel… and menus.**
  - The panel's **⋯ menu** (`panelOptionsMenu`, `Main.qml:1024`) has the panel's own items, then Move panel… (`:1130-1134`), Move tab left/right (mirrored in RTL through `leftStep`, `:1036`), Float/Dock and Close. [docs/docking.md:83](../docking.md)
  - **View › Panels** (`panelsMenu`, `Main.qml:3216`) has a submenu per panel with Show, Float/Dock and Close, plus Move panel… (`:3305`) and "Restore the previous layout".
  - **Move panel** (`placementWindow`, `Main.qml:8167`) is a keyboard placement dialog. `placePanel(dock, kind, target)` places a panel as a tab, left, above, right or below a target, or floating (`Main.qml:801-824`; "MUST KILL: kind is a magic int").
- **Plugin panel areas.**
  - A manifest panel's `area` is `left`, `right` or `bottom`, and defaults to `right`. `src/plugins/include/hikari/plugins/manifest.h:38-43`; `src/plugins/manifest.cpp:62-63`; [docs/plugins.md:32](../plugins.md), `:216`
  - In QML, `bottom` becomes a tab of a docked Log, or else goes along the bottom at 180 px. `left` goes along the window's left edge at 300 px (`Location_OnLeft` with no target). `right` goes into the column right of the Grid. `Main.qml:508-519`

### 2.4 Panels that exist now

`panelIds()` (`workspace_layout.cpp:127-134`) and the docks in `Main.qml`:

| Id | Title | Declared | Hotkey command (id, default key) |
|---|---|---|---|
| Video | Video | `Main.qml:4203` | |
| Audio | Audio | `:4495` | |
| Original | Original text | `:5984` | |
| Editor | Line editor | `:6023` | |
| Grid | Grid | `:6288` | |
| Reference | Reference | `:6739` | |
| Timing | **Shift times** | `:6767` | `GLOBAL_SHOW_SHIFT_TIMES` 5030, Ctrl-I (`hotkeys.cpp:184`, `:837`) |
| Postprocessor | Timing postprocessor | `:6798` | 5214 (`hotkeys.cpp:315`) |
| QcNotes | QC notes | `:6840` | 5241 (`hotkeys.cpp:338`) |
| Review | Review (AI) | `:6864` | `GLOBAL_SHOW_AI_REVIEW` 5251 (`hotkeys.cpp:352`) |
| Agent | Agent (AI) | `:6904` | `GLOBAL_SHOW_AI_AGENT` 5252 (`hotkeys.cpp:353`) |
| Search | **Find** (find and replace) | `:6941` | Find 5009 Ctrl-F, Find and replace 5010 Ctrl-H (`hotkeys.cpp:840-841`) |
| Automation | Automation | `:6967` | |
| Spellchecker | Spellchecker | `:7005` | 5014 |
| Styles | Styles | `:7034` | |
| Template | Template editor | `:7064` | |
| NodeGraph / NodeInspector / NodeSources | Graph / Node / Source lines (node effects mode only) | `:7092`, `:7120`, `:7142` | |
| Log | Log | `:7164` | |
| Explorer **(PJ4 WIP)** | Explorer | not on main | `GLOBAL_SHOW_EXPLORER` 5337 (PJ4 `hotkeys.cpp:381`) |

- Review and Agent are listed only while the AI features are on (`root.review.on`, `root.agent.on`). `Main.qml:471-474`
- **Explorer (PJ4 WIP).**
  - It raises `kPanelRegistry` to 18 and adds `"Explorer"` to `panelIds()` (PJ4 `workspace_layout.h:69`, `workspace_layout.cpp:134`).
  - `addExplorer` docks it **left of the Grid** at a fifth of the Grid's width (PJ4 `Main.qml:216-221`).
  - It opens by itself only in the Editing preset while a Project is open (`:206-215`).
  - [ADR 0030](../adr/0030-open-a-folder-as-a-project.md) says the same: "It docks left of the Grid in the Editing preset when a Project is open."
  - The owner's request puts the Explorer on the **right**, which differs from ADR 0030.

---

## 3. What KDDockWidgets 2.5.1 offers

### 3.1 Side bars and auto-hide: not available on QtQuick

- `SideBarLocation { None, North, East, West, South, Last }`: "Each main window supports 4 sidebars". `KD/src/KDDockWidgets.h#L281-L290`
- The book describes the concept: "A side bar is a place in the MainWindow where you can 'minimize' dock widgets … When you send a dock widget to the sidebar it will close but show a button in the sidebar, if you press it, it will show the dock widget as an overlay." `KD/docs/book/src/architecture_and_concepts.md#L88-L92`
  - This is **auto-hide**, the IDE "pin/unpin" model. It is not a fixed, permanently open column. `CloseReason::MovedToSideBar` ("User clicked the pin-button") and `OverlayCollapse`. `KD/src/KDDockWidgets.h#L395-L401`
- Side bars exist only with `Flag_AutoHideSupport` (`Flag_TitleBarShowAutoHide | Flag_TitleBarNoFloatButton`). `KD/src/Config.h#L115-L120`; created in `KD/src/core/MainWindow_p.h#L39-L46`
  - `Flag_AutoHideAsTabGroups` sends tabbed docks to the side bar together. `KD/src/Config.h#L133`
- **The QtQuick frontend does not implement them:**
  - `QtQuick::ViewFactory::createSideBar` returns `{}`. `KD/src/qtquick/ViewFactory.cpp#L123-L126`
  - `qtquick/views/SideBar.cpp` is not in the build's source list, which names every other QtQuick view (`KD/src/CMakeLists.txt#L137-L149`). Only its header is installed (`#L514`), and that header includes `QToolButton`, a QtWidgets class. `KD/src/qtquick/views/SideBar.h#L21-L53`
  - `QtQuick::MainWindow::centerWidgetMargins` logs "SideBar hasn't been implemented yet". `KD/src/qtquick/views/MainWindow.cpp#L165-L169`
  - The QML methods (`moveToSideBar`, `overlayOnSideBar`, `toggleOverlayOnSideBar`, `sideBarIsVisible`) are declared on `DockingArea` and `DockWidget` (`KD/src/qtquick/MainWindowInstantiator.h#L78-L84`; `KD/src/qtquick/DockWidgetInstantiator.h#L106`), but nothing draws them.
  - HikariSub's comment at `src/ui/docking.cpp:376` reaches the same conclusion.

### 3.2 Affinities, nesting, layout and sizes

- **Affinities** limit which docks may dock together and into which main windows. `KD/src/core/DockWidget.h#L286-L312`; `KD/src/core/MainWindow.h#L132-L145`
  - A side bar container could be given an affinity of its own, so that only chosen panels can drop into it. HikariSub today uses one affinity per window (§2.1).
- **Nested main windows are supported.**
  - A `MainWindow` (QML `DockingArea`) can sit inside a dock. `DockWidget::isFloating` and `Group::floatingWindow` handle "main window nesting" (`KD/src/core/DockWidget.cpp#L202-L214`; `KD/src/core/Group.cpp#L550-L560`).
  - `LayoutSaver` saves nested main windows with their host: "A main window nested in a dock widget is part of that dock widget's window, but is a separate entry in the layout". `KD/src/LayoutSaver.cpp#L906-L934`, `#L995`
  - HikariSub's validator refuses any layout with more than one main window (`workspace_layout.cpp:893`).
- **No "pinned" areas, apart from the auto-hide pin above.**
  - There is a `MainWindowOption_HasCentralWidget` (a central item that can't be detached) and `MainWindowOption_CentralWidgetGetsAllExtraSpace`. `KD/src/KDDockWidgets.h#L78-L93`
  - QML exposes a `persistentCentralItemFileName`. `KD/src/qtquick/MainWindowInstantiator.h#L45`
- **Edge docking.** `addDockWidget(dock, Location_OnLeft/OnRight, relativeTo = null, size, InitialVisibilityOption)` with no target docks along the main window's outer edge, across the whole layout. `KD/src/core/MainWindow.h#L77-L90`; `KD/src/qtquick/MainWindowInstantiator.h#L66-L71`
  - HikariSub already does this for `left` plugin panels and the Log (`Main.qml:411-413`, `:515-516`).
  - An edge group of tabbed panels can therefore be built with today's engine. It is not a reserved slot: users can drag panels next to it, into it, or split it, and Reset layout or a preset rebuilds the whole layout.
- **Sizes.** "Minimum sizes are supported but not maximum sizes" on QtQuick (`KD/docs/book/src/min_max_sizing.md`, "QtQuick" section). KDDW cannot hold a column at a fixed width or cap it on QtQuick.
- **Collapsing.** KDDW has no "collapse a group to its tab strip" operation in `src/core`. Closing every dock of a group removes the group, and its place is kept for when a dock reopens (`hasPreviousDockedLocation`, as `panel_dock.cpp:53` uses).
- **Right-to-left.** `src/core` and `src/qtquick` contain no `RightToLeft` or `layoutDirection` handling, so `Location_OnLeft` is always the physical left.

### 3.3 Answer to "built on KDDW's layout, or a HikariSub container?"

From the facts above:

- Two **collapsible edge areas, each a tabbed stack**, are possible on KDDW only as ordinary edge groups (§3.2). KDDW has no notion of a side bar that keeps its slot when it is empty or collapsed, and its auto-hide side bars don't exist on QtQuick (§3.1).
- To get a slot that persists when empty, an activity strip, and left/right swapping, HikariSub has three choices:
  1. track the two groups itself on top of KDDW;
  2. nest a `DockingArea` per side bar inside a dock, which the engine supports but HikariSub's validator refuses;
  3. place two QML containers outside the `DockingArea` and host panels in them without KDDW.

---

## 4. MuseScore 4

- **There is no activity bar and no vertical icon strip.**
  - The left "sidebar" is a KDDW dock area. Palettes, Layout (formerly Instruments), Properties, Selection filter and History stack in one frame as **horizontal text tabs**.
  - All of them have `groupName: "VERTICAL_PANELS"` and a width, minimum width and maximum width of 300. `MS/src/appshell/qml/MuseScore/AppShell/NotationPage/NotationPage.qml#L93-L107`, `#L250-L389`
  - Panels sit on the left by default (`DockPanelView` sets `Location::Left`, `MS/src/framework/dockwindow/qml/Muse/Dock/dockpanelview.cpp#L159`). History is on the right (`NotationPage.qml#L362-L389`). Selection filter and History start hidden.
  - v4.4.4 had Instruments instead of Layout, and no History. `https://github.com/musescore/MuseScore/blob/v4.4.4/src/appshell/qml/NotationPage/NotationPage.qml#L205-L292`
- **Tabs only within a group.** Two panels share a tab bar only when they have the same `groupName` and the same location. `dockpanelview.cpp#L272-L295`; `MS/src/framework/dockwindow/qml/Muse/Dock/dockpageview.cpp#L195-L205`
  - The tab bar is 35 px high and shows text, with a menu of Close and Undock/Dock. `MS/src/framework/dockwindow/qml/Muse/Dock/DockFrame.qml#L40-L42`, `#L119-L131`; `MS/src/framework/dockwindow/qml/Muse/Dock/DockPanelTab.qml#L58-L76`; `dockpanelview.cpp#L41-L73`
- **Switching.**
  - The View menu has a checkable item per panel. `MS/src/appshell/qml/MuseScore/AppShell/appmenumodel.cpp#L296-L327`; `MS/src/appshell/internal/applicationuiactions.cpp#L125-L160`
  - Opening a panel adds it as a tab of an open panel of its group. `dockpageview.cpp#L207-L242`
  - Function keys: F7 Layout, F8 Properties, F9 Palettes, F10 Mixer, F12 Timeline. `MS/src/app/configs/data/shortcuts.xml#L746-L767`, `#L925-L926`
- **Moving to the right.**
  - A vertical panel may drop at the left or right of the score, within 300 px of its edge. `NotationPage.qml#L104-L107`
  - It may also drop into a hidden 36 px **DockingHolder** on any window edge. A holder opens and highlights within 50 px while a drag passes, and closes after it. `MS/src/framework/dockwindow/qml/Muse/Dock/DockPage.qml#L61-L113`; `MS/src/framework/dockwindow/qml/Muse/Dock/dockingholderview.cpp#L27-L56`; `MS/src/framework/dockwindow/internal/dropcontroller.cpp#L46`, `#L281-L307`
  - The handbook says panels "can be … undocked and dragged to the right sidebar; an equivalent area on the other side of the window". https://handbook.musescore.org/navigation/the-user-interface
- **KDDW side bars are not used.**
  - MuseScore vendors KDDW 1.4.95 (`MS/src/framework/dockwindow/thirdparty/KDDockWidgets/CMakeLists.txt#L87-L92`). It sets only `Flag_HideTitleBarWhenTabsVisible | Flag_TitleBarNoFloatButton` (`MS/src/framework/dockwindow/dockmodule.cpp#L124-L128`) and creates the main window with `MainWindowOption_None` (`MS/src/framework/dockwindow/qml/Muse/Dock/dockwindow.cpp#L143-L144`).
  - No MuseScore code outside `thirdparty/` refers to a side bar or auto-hide.
- **Workspaces.**
  - A workspace is a zip file that stores per-page KDDW layout blobs under `ui_states`, plus UI settings, toolbar configs and palettes. `MS/src/framework/ui/internal/uiarrangement.cpp#L36-L51`, `#L109-L116`; `MS/src/framework/dockwindow/qml/Muse/Dock/dockwindow.cpp#L608-L671`; `MS/src/framework/workspace/internal/workspacefile.cpp#L60-L127`
  - Changes are saved automatically (handbook, https://handbook.musescore.org/customization/workspaces).
  - **Reset workspace** deletes the user copy of an edited built-in workspace after a warning. `MS/src/framework/workspace/internal/workspace.cpp#L101-L122`; `MS/src/framework/workspace/qml/Muse/Workspace/workspacelistmodel.cpp#L218-L230`
  - **View › Restore the default layout** resets every dock and clears every page's saved state. `dockwindow.cpp#L335-L359`

---

## 5. Gaps and risks (findings, not recommendations)

1. **The engine has no side bar concept HikariSub can use.** KDDW auto-hide side bars are not built on QtQuick (§3.1). An edge group is just a group: users can split it, dock beside it, or empty it, and nothing keeps its slot when it is empty (§3.2). VS Code's "Drag a view here to display." placeholder (§1.5) has no KDDW equivalent.
2. **Per-tab layouts work against "left and right can switch place".**
   - In VS Code the side is per workspace and the view locations are global (§1.6).
   - In HikariSub the whole arrangement belongs to each Document tab (ADR 0020), so a swap or a moved panel would apply to one tab only unless it is kept apart from the arrangement.
   - Presets, Reset this layout and saved layouts change only the current tab ([docs/docking.md:37](../docking.md)).
3. **Multiple windows (ADR 0025).** Each window has its own `DockingArea`, affinity and panels, and panels don't move between windows. Saved layouts and presets are app-wide; the focus and the floating groups are per window ([ADR 0025](../adr/0025-multiple-windows.md), table). Nothing says yet whether side bar state is app-wide, per window or per tab.
4. **Validation and persistence formats.**
   - The validator refuses more than one main window and floating parents other than `Classic` (`workspace_layout.cpp:883-893`, `:955-962`), which rules out nested `DockingArea`s as they stand.
   - A new panel id raises `kPanelRegistry`, and older builds then refuse the layout (`:869-870`).
   - The Session `Layout:` line, the `[Hikari]` section and `layouts.json` all store the same envelope ([docs/docking.md:18](../docking.md)). Side bar state kept outside the KDDW payload would need a field in the envelope, or a separate setting.
   - The owner's rule that API and protocol versions stay at 1 until the first release (memory note "Versions stay at 1") covers `kApiVersion` and the plugin, Lua and media protocols. `kSchema` (`workspace_layout.h:67`) and `kPanelRegistry` are separate numbers.
5. **Existing presets.**
   - Editing, Timing, Translation, Typesetting, Karaoke and the node effects layout are built by hand around the Grid, Video and Line editor (`Main.qml:273-357`).
   - The DK2 rule places Shift times, Find, Automation and QC notes in a column **right of the Grid**, made on demand (§2.3). That column is already close to a right side bar, but it has no fixed slot, and Reset layout closes it.
   - The AI panels (Agent, Review) also open right of the Grid (`Main.qml:437-442`), and the request moves them to the left.
   - PJ4's Explorer docks **left** of the Grid, as ADR 0030 says; the request puts it on the right.
6. **Hotkeys.**
   - VS Code's **Ctrl+B is HikariSub's "Add bold"** (3012, editor scope; `hotkeys.cpp:901`). Ctrl+Alt+B has no default binding in `hotkeys.cpp`.
   - Ctrl-I is Shift times (`hotkeys.cpp:837`).
   - New commands need new ids. Audio ids under 1010 are mapped to id+10 (memory "Audio hotkey ids +10 trap"), and global ids are in the 5xxx range up to PJ4's 5337.
   - Hotkey count pins in tests move with each new command (memory "Hotkey count pins").
7. **Right-to-left.**
   - KDDW `Location_OnLeft/OnRight` are physical sides with no RTL mirroring (§3.2).
   - HikariSub mirrors only tab moves (`leftStep`, `Main.qml:1036`) and the focus mark's corner (visual-language.md:134).
   - VS Code names its bars "Primary" and "Secondary" rather than left and right (§1.2). The request says "left" and "right".
8. **The K2 focus triangle.**
   - The focused panel has no ring. It shows an 8 px accent triangle in its header's top trailing corner (`PanelFocusMark.qml`), on the group the engine reports as focused, and the triangle moves with F6, Tab and clicks. [docs/ux/visual-language.md:126](../ux/visual-language.md), `:134`
   - An activity strip or a container outside KDDW would need its own focus mark and its own place in the F6/Shift+F6 order ([docs/docking.md:29](../docking.md), `:85`).
9. **Accessibility.**
   - The headers are a tab list named "Panels" (`src/ui/DockTabBar.qml:33`), with a "<panel> options" button and named actions for every placement ([docs/docking.md:29](../docking.md), `:85`).
   - An icon-only activity strip would need an accessible name and a tooltip per icon ([docs/ux/visual-language.md:257](../ux/visual-language.md); memory "UI professional polish": icon-only tool strips with hover names).
   - VS Code's Move View is keyboard-driven (§1.3). HikariSub's Move panel… window is the keyboard equivalent (`Main.qml:8167`), but it offers no "side bar" destination.
10. **Window minimum.** The window's minimum follows the docked layout's minimum ([docs/docking.md:68](../docking.md)). Two side bars with 300 px minimums, like MuseScore's, raise that minimum on small screens (1280 × 800 at 150 % is the reference in visual-language.md:283).
11. **AI off.** Review and Agent exist only while AI is on (`Main.qml:471-474`). A left side bar that is AI by default would be empty with AI off. VS Code hides its Secondary Side Bar for new users when AI is disabled (§1.5).

---

## 6. Options (marked: implementation shapes, not decisions)

### Option A: two KDDW edge groups that HikariSub tracks

The left and right side bars are ordinary KDDW groups docked with `Location_OnLeft/OnRight` and no target. HikariSub records which panels belong to each side. "Swap sides" re-docks both groups. A thin activity strip of panel icons is QML outside the `DockingArea`.

- **Pros:**
  - No engine change.
  - Persistence stays one KDDW payload in the existing envelope.
  - Panels keep floating, tabbing and Move panel….
- **Cons:**
  - Nothing stops a user from splitting the group or docking beside it, so "the side bar" is a convention, not a structure.
  - An empty side bar leaves no slot, so it has no "Drag a view here" placeholder.
  - Every preset builder (`Main.qml:273-357`) and the DK2 right-of-Grid rule need rewriting.
  - The physical sides need hand mirroring for RTL.

### Option B: a nested `DockingArea` per side bar

Each side bar is a dock that holds its own `DockingArea` with an affinity of its own, so only side bar panels drop into it. KDDW saves nested main windows with their host (§3.2).

- **Pros:**
  - It is a real container with its own tab stacks and splits.
  - Affinities enforce what can go in.
  - Collapsing is closing the host dock.
- **Cons:**
  - The validator's "one main window, parents are Classic" rules (`workspace_layout.cpp:883-893`, `:955-962`) and ADR 0025's per-window naming must change.
  - A panel with a side bar affinity can't also dock in the main area, unless it has both affinities, and then the side bar enforces nothing.
  - Nesting is little used (no QtQuick example in `KD/examples/qtquick`), so it needs the native gate again under ADR 0017.

### Option C: HikariSub's own QML side bars outside KDDW

Two QML containers frame the `DockingArea` (left and right), each with an activity strip, a tab stack and a collapse control. Panels assigned to a side bar are QML items reparented into them instead of KDDW docks. Swapping mirrors the frame.

- **Pros:**
  - Exactly VS Code's model: fixed slots, an empty placeholder, Ctrl+Alt+B-style toggles and maximise.
  - RTL mirroring comes from Qt Quick's layout mirroring.
  - Side bar state can be app-wide or per window, independent of the per-tab arrangement.
- **Cons:**
  - Two docking systems: a panel moving between a side bar and the main area changes owner (KDDW dock ↔ QML item), with focus, the F6 order, the K2 mark, the ⋯ menu, floating and the accessibility names to rebuild.
  - Move panel… needs side bar destinations.
  - Plugin `area` meanings change.

---

## Open questions for the owner

1. Should a side bar be a **fixed slot** that stays while empty ("Drag a panel here"), or is a docked group on each edge enough?
2. Is the side and set of panels in each side bar **app-wide, per window, or per Document tab** like the rest of the arrangement (ADR 0020)? VS Code keeps the side per workspace and view locations global.
3. Should the side bars be **collapsible to an activity strip of icons**, as in VS Code, or **tabs only**, as in MuseScore 4?
4. The Explorer: **right side bar** (the request) or **left of the Grid** (ADR 0030, PJ4)? Which does PJ4 ship?
5. With **AI off**, what does the left side bar hold, or does it hide?
6. Should "left and right" **mirror in a right-to-left UI** (VS Code's Primary/Secondary naming), or stay physical?
7. Keys: VS Code's Ctrl+B is HikariSub's Add bold. Should side bar toggles get **Ctrl+Alt+B** and new keys, or have no default keys?
8. Do the **presets** (Editing, Timing, Translation, Typesetting, Karaoke) each define their own side bar contents, or do the side bars stay the same across presets?
9. Can **any panel** (Grid, Video, Line editor) go into a side bar, or only tool panels (Find, Shift times, Postprocessor, QC notes, Styles, Explorer, Agent, Review, plugins)?
10. Does a plugin's `area: left/right` mean the **side bar** on that side?
