# Native docking and workspace persistence proposal

For [Choose native docking and workspace persistence architecture](https://github.com/altqx/hikari/issues/46). Movable/floating panels, shared layout, tool targets and the familiar Editing preset are already [accepted](../ux/workspaces.md). This proposal chooses their implementation boundary; it does not reopen constrained-versus-floating UX or claim a native docking pass.

## Engine and dependency

Recommend **KDDockWidgets 2.4.1, QtQuick frontend only**, behind a Hikari-owned WorkspaceLayout service. Resolve the release to commit `c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8`; pin the source digest, recipe and options in the accepted vcpkg/overlay graph before qualification. Use the [documented public entry points](https://docs.kdab.com/kddockwidgets-manual/latest/private_api.html): QtQuick DockWidget/MainWindow views plus Config, LayoutSaver and KDDockWidgets headers. Other Core headers are private; isolate and qualify any unavoidable private dependency inside the adapter. No Muse framework import or direct library access from document services.

The [upstream release](https://github.com/KDAB/KDDockWidgets/releases/tag/v2.4.1) supplies the candidate version. Its [QtQuick guide](https://github.com/KDAB/KDDockWidgets/blob/c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8/README-QtQuick.md) documents the QtQuick-only option and cross-window effects caveat. Documented [engine features](https://github.com/KDAB/KDDockWidgets) cover nested splits, tab groups and floating groups. These match the accepted requirement more directly than implementing those mechanics over SplitView. A QWidget shell would alter the chosen Qt Quick shell boundary. A custom docking engine remains a fallback only after a concrete incompatibility, not an automatic scope expansion.

Use the dependency's [GPLv3 option](https://docs.kdab.com/kddockwidgets-manual/latest/licensing.html) with the repository's GPLv3 distribution; retain source, patches, applicable notices and license material in the release manifest. No commercial license purchase is assumed. Full payload/dependency compliance still belongs to the distribution gate.

## Hikari-owned identities and state

| Record | Proposed responsibility |
| --- | --- |
| WorkspaceLayout | One shared versioned arrangement: dock tree/library payload, open panels, selected panel tabs, split ratios, floating groups and placement hints. It contains no subtitle content, undo history or media decoder. |
| PanelId | Stable untranslated role/instance identity, such as core Video/Audio/Editor/Grid or tool Styles/Search/Timing/History. Recreating a QML view never creates a second authoritative document. |
| Panel controller | Application-owned focus bookmark and tool binding, independent of dock view/window lifetime. Follow/pin and protected-reference checks use ordinary application contracts. |
| WindowId / affinity | One main editing window plus floating panel groups in the same workspace affinity. Floating groups cannot create another editing target. Fullscreen video is a separate presentation surface, not a dock group. |

The initial shell has one main window; multiple floating groups are allowed. No new independent multi-main-window document model is implied. Core panels and persistent tools may split, tab, float, close and reopen; closing a panel hides its presentation, never closes its Document or discards its draft. Closing a floating group hides its member panels and returns focus to the most recent visible region. View → Panels can restore each panel at its last valid placement, otherwise its default location.

Controllers and application state survive view reconstruction. Moving Video between QQuickWindows explicitly releases/recreates window-bound scene-graph resources with generation checks; CPU media context survives. Never share one QSG node/texture handle blindly between windows. Rearrangement cannot commit a text draft, retarget a pinned tool, mutate a protected reference or switch the transport owner.

## Keyboard and focus contract

Every panel exposes named actions for Show, Focus, Hide, Float, Dock, Tab with…, Move before/after and Resize. A keyboard placement dialog selects a destination panel/group and side; it must express every supported drag placement. Split resizing exposes numeric/step controls. F6/Shift+F6 traverse visible regions, including floating groups, while named focus commands provide direct access. Panel tabs expose names, selection and close/float actions to assistive technology.

Capture a logical focus bookmark before moving/hiding. Restore the same control or a documented panel entry point after recreation; if it no longer exists, focus the next visible region. Focus never determines the editing Document. Modal task dialogs stay associated with their application task and logical owner even when a panel moves. Native cross-window activation, keyboard placement and screen-reader speech are validation requirements, not library guarantees.

## Save, restore and recovery

Use Hikari's schema envelope around the library's JSON layout: schema version, application/panel-registry version, exact engine version, preset/custom identity, engine payload and placement hints in device-independent units. Store tool bindings/session references separately so layout failure cannot corrupt them. Built-in Editing/Timing/Translation/Typesetting presets are immutable templates; automatic saving updates the current custom arrangement, and Reset layout returns to the chosen template without changing content or preferences.

Persist completed layout operations atomically, not every drag pixel: use `serializeLayout()` plus Hikari's atomic writer, not upstream `saveToFile()`, which writes directly to an output stream. Retain the previous valid layout. At startup validate size/schema/IDs before invoking the engine, construct known panels or its lazy factory, restore, then reconcile new/missing panels. The [LayoutSaver interface](https://github.com/KDAB/KDDockWidgets/blob/c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8/src/LayoutSaver.h) exposes serialization, restore success and restored docks; Hikari owns compatibility validation and recovery. Unknown/newer data is preserved for diagnosis, not overwritten on a failed restore. A corrupt/incompatible layout falls back to Editing with an inspectable recovery notice and a restore-backup option. Suspend autosaving throughout restore and fallback: a false return can follow mutation of live dock views, so rebuild a known layout from surviving controllers after partial failure rather than treating the result as transactional rollback. These requirements follow the [pinned implementation](https://github.com/KDAB/KDDockWidgets/blob/c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8/src/LayoutSaver.cpp#L368).

On Windows/X11, intersect saved placement with available screens, constrain minimum sizes and keep title bars reachable; removed monitors return floating groups to the main screen. Use screen identity and geometry as hints, not durable hardware identity. On Wayland, retain grouping and size where supported but let the compositor place windows: [upstream documents separate move/dock title bars and unavailable absolute position restore](https://docs.kdab.com/kddockwidgets-manual/latest/qpa-wayland.html). Do not promise coordinate-identical restoration across platforms.

## Required native gate and review

A focused Qt/C++ experiment must qualify the pinned engine with Qt 6.11.2: default arrangement, split/tab/float/redock, keyboard-only equivalents, NVDA/Orca, mixed DPI, monitor removal, GNOME/KWin Wayland, fullscreen coexistence, Video resource recreation, retained drafts/targets, unknown panels and corrupt/older/newer layouts. Record actual Qt/backend/engine revisions and observed failures. The HTML study and desktop documentation cannot pass these checks. The official SDK/toolchain prerequisites identified by the build audit also apply here.

**Requested decision:** adopt KDDockWidgets behind this owned adapter and persistence/focus contract, with the native gate required before treating docking implementation as qualified. Engine upgrades require a layout migration/restore matrix; if qualification reveals an incompatible requirement, reopen this choice explicitly.
