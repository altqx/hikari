---
status: accepted
amends: 0020, 0025, 0030
---

# Frame every window with a left and a right side bar

On 2026-11-12 altqx decided, in two rounds on card [DK3 #1040](https://github.com/altqx/hikari/issues/1040) (Q424–Q442, "yes to all"), that every shell window gets two VS Code-like side bars, one on each edge, outside the middle docking. The AI panels start in one and the tool panels in the other, and the two can swap. The research is in [side-bars.md](../research/side-bars.md). [ADR 0017](0017-owned-native-docking.md) is unchanged: KDDockWidgets stays the engine of the middle docking behind the WorkspaceLayout adapter.

## HikariSub's own side bars, not the engine's

The pinned KDDockWidgets has no side bar HikariSub can use: its auto-hide side bars are not built for QtQuick, and an edge group is an ordinary group that can be split, docked beside or emptied, so it keeps no slot when empty. The side bars are therefore QML containers that frame the `DockingArea` (Q433, option C). The middle keeps today's docking for the Grid, Video, Audio, the Line editor and every panel not in a side bar.

- **A side bar** is an icon strip along its top and, under it, the one panel it shows, with that panel's header (Q426, Q427, and the owner's follow-up on #1040: icons on top, as VS Code's top activity bar). The strip has the panels' icons only, with names on hover. Clicking an icon shows its panel; clicking the shown panel's icon hides the side bar. Two title bar toggles, View and the commands show it again with its panel and width.
- **What can go in** (Q428, Q439): tool panels, the AI panels and plugin panels, in any mix and in either side bar: the AI panels can share the tools side bar. The Grid, Video, Audio and the Line editor stay in the middle.
- **Moving.** Dragging an icon to the other strip moves its panel there. The ⋯ menu and Move panel… offer Move to the left side bar, Move to the right side bar and Move out of the side bar, each where it applies. A panel moved out takes its first place in the middle by the DK2 rules.
- **A panel changes container when it moves.** Its controller, focus bookmark and tool binding stay application state ([docking.md](../docking.md)); only its view moves between a `PanelDock` and a side bar. Focus, the F6 order, the K2 focus triangle, the ⋯ menu, floating and the accessible names follow it.
- **An empty side bar** keeps its empty strip and shows "Drag a panel here" (Q434). A side bar left without panels by AI off hides, and its toggle is disabled (Q436).
- **Right-to-left** mirrors the whole frame, so the AI side bar starts on the right (Q437).
- **Width** starts at about 320 px; dragging the inner edge resizes it, and dragging it below its minimum hides it (Q442).

## Side bars are per window, outside the per-tab arrangement

This amends [ADR 0020](0020-per-document-tab-layouts.md). A Document tab's arrangement is still the middle docking and its floating panels. The side bars belong to the window (Q430): which panels each holds, in what order, which one shows, its width and whether it is hidden. A tab switch never moves them. Presets, Reset this layout, saved layouts and a file's `[Hikari]` section set only the middle, and the presets no longer place the panels that now start in a side bar.

This amends [ADR 0025](0025-multiple-windows.md)'s table: the side bars join the per-window column, and the Session stores them with each window's geometry and Project. Each window shows the side bars on Home too (Q431).

**Always saved** (the owner's follow-up on #1040, amended after the lgtm: no new file). Every side bar change is saved at once in two places that already exist:

- **The window's Session entry** (`"sideBars"` in its `windows` object). The Session's version stays 1; a reader that skips the key opens the defaults.
- **The settings file**, as `workspace.sideBars`: the side bars changed last, in any window. A new window takes them, and so does a start without a Session or with a Session written before this record (or legacy's). Only a first start with neither gets the defaults below.

`layout.json` is not used for them: it holds one tab's arrangement in the envelope that saved layouts and the `[Hikari]` section share, and a layout that fails to restore is set aside whole, which would take the side bars with it. The settings file already keeps such window state (`workspace.dialogGeometry`), next to `appearance.aiSideBar` (`start` or `end`), the AI side bar's edge.

## Defaults and the Explorer

- **The AI side bar** (left unless swapped) starts with Agent and Review (Q425).
- **The tools side bar** (right unless swapped) starts with Explorer, Find and replace, Shift times, Timing postprocessor, Styles and QC notes.
- **Swap** (Q429): View › Swap side bars, and Settings › Appearance › AI side bar: Left / Right. Contents and widths move together.
- **Plugins** (Q440): a manifest panel with `area: left` or `right` first shows in that side bar; `bottom` stays in the middle.

This amends [ADR 0030](0030-open-a-folder-as-a-project.md): the Explorer starts in the tools side bar, not left of the Grid (Q435).

## Migration and commands

- **Migration** (Q441): on the first start of this version (no `workspace.sideBars` in the settings yet), panels that a saved arrangement docks in the column right of the Grid move into the side bars once, the AI panels left and the tools right, with a What's new line. Arrangements are not rewritten otherwise.
- **Commands** (Q438): 5339 Toggle left side bar (Ctrl+Alt+B), 5340 Toggle right side bar and 5341 Swap side bars, no default keys for the last two. Ctrl+B stays Add bold.

## Native gate

The new frame reruns the ADR 0017 native gate on sway, KWin and mutter, and on Windows when the VM is up: the side bars and their strips at mixed DPI, a panel moved between a side bar and the middle with the focus kept, floating from a side bar, RTL, screen-reader names and restore across several windows.

## What changes

- [docking.md](../docking.md): the per-tab section names the side bars as window state; the DK2 right-of-Grid rule applies to panels that are not in a side bar.
- ADR 0020: "Docked, tabbed and floating panels and their sizes are the tab's" excludes the side bars.
- ADR 0025: the side bars are per window.
- ADR 0030: "It docks left of the Grid in the Editing preset" reads "It starts in the tools side bar".
- `CONTEXT.md` gains **Side bar**.

The owner approved the mockup and every recommended answer to DK3-Q1–Q14 (Q451–Q464) on 2026-11-12 ([#1040](https://github.com/altqx/hikari/issues/1040#issuecomment-6826953073)). Among them: the AI side bar's edge is one app-wide setting that Swap side bars flips; clicking the shown panel's icon hides its side bar, and two title bar toggles bring it back; a 240 px minimum; icons that don't fit go under a "More panels" chevron; no drag between a side bar and the middle docking yet.
