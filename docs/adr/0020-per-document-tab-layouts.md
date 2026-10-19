---
status: accepted
---

# Keep a panel arrangement per Document tab

On 2026-10-18 altqx decided, in the wave-6 review of their UX feedback ([#213](https://github.com/altqx/hikari/issues/213), card [WS1 #215](https://github.com/altqx/hikari/issues/215)), that each Document tab keeps its own panel arrangement. This reverses the shared-Workspace rule accepted on 2026-09-27 in [workspaces.md](../qt/ux/workspaces.md), where the arrangement belonged to the application and stayed put when the editing target changed. [ADR 0017](0017-owned-native-docking.md) is unchanged: KDDockWidgets stays the layout engine behind the WorkspaceLayout adapter.

- **Switching tabs swaps the arrangement with the content**, the way each tab already gets its own video, audio and position back. Docked, tabbed and floating panels and their sizes are the tab's. The main window keeps its geometry; the engine restores the tab's layout in place, and nothing moves when both tabs show the same arrangement.
- **A new or opened tab** starts with the Translation preset when its file has `TLMode: Yes`, otherwise with a copy of the current tab's arrangement. Opening a file into the current tab counts as opening it.
- **Presets, Reset layout, the View arrangements and the layout backup** change only the current tab.
- **The Session saves each tab's arrangement** on a `Layout:` line of its own (WS1-session-layout in the [compatibility decisions](../qt/compatibility-decisions.md)); legacy skips the line. An invalid arrangement, or one the engine restores only in part, gives that tab the Editing preset with a notice, and the rejected text is kept for diagnosis, as the saved layout file already is.
- **The saved layout file** beside the settings holds the arrangement shown last. A start without a Session gives it to the first tab.

The arrangement is a value apart from the engine (`WorkspaceArrangement`: the preset it started from and the engine's payload in Hikari's envelope), so the `[Hikari]` section of ASS files ([WS2 #216](https://github.com/altqx/hikari/issues/216)) and saved layouts ([WS3 #217](https://github.com/altqx/hikari/issues/217)) store the same thing. Documents, drafts, tool targets, the editing target and the protected reference stay application state: a tab's arrangement places panels, and the Reference tray still follows the comparison rather than the tab.

The decision needs the native gate again for tab switches with floating panels on KWin, mutter, sway, X11 and Windows. It does not decide what a layout saved in a file does, or how saved layouts are named and switched; WS2 and WS3 do.
