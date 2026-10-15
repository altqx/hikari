# D3 #207: dock and panel chrome on real Windows monitors (2026-10-15)

Build and host: [../setup/host.md](../setup/host.md). Monitors: DISPLAY1 LM30-E330C 2560×1080 @ 144 Hz, 100 %, primary;
DISPLAY2 AOC 22E1W 1920×1080 @ 60 Hz, set to **150 % (GetDpiForMonitor 144)** by the owner for this run, to the right (origin 2560,93)
([00-display-150.txt](00-display-150.txt)). Both on the GTX 1070 Ti.

Pointer input: the owner's mouse unless marked "SendInput" (my own synthetic mouse, used once to capture a held title drag).
Geometry comes from `GetWindowRect`/`GetDpiForWindow` read per-monitor DPI aware (physical pixels); captures are
`CopyFromScreen` in physical pixels. Recorders sampled window geometry every 0.2–0.4 s during each physical step.

| # | Item | Result | Evidence |
| --- | --- | --- | --- |
| 1 | Float a panel (double-click its header) | **observed**: Video floated, 706×582 at 96 DPI; tool window, borderless (`WS_POPUP`, no `WS_CAPTION`/`WS_THICKFRAME`; ex-style `0x80080` = `WS_EX_LAYERED`/`WS_EX_TOOLWINDOW`), owned by the main window | [01-floated-video-100pct.png](01-floated-video-100pct.png), [json](01-floated-video-100pct.json) |
| 2 | Free-part move onto the 150 % monitor (`startSystemMove`) | **observed** (owner): the panel followed the pointer onto the 22E1W | [02-free-move-on-150pct.json](02-free-move-on-150pct.json) |
| 3 | Keeps its size in logical pixels | **observed**: 706×582 → **1059×873** (×1.5) at 144 DPI. Audio later: 806×216 → 1209×324 and back, 816×199 ↔ 1224×299 | 02 json, [08-owner-retry-trace.txt](08-owner-retry-trace.txt), [11-before-removal-windows.json](11-before-removal-windows.json) |
| 4 | Crisp at 150 % (GetDpiForWindow = 144) | **observed**: `GetDpiForWindow` 144; text and icons drawn at the native scale, not bitmap-stretched | [02-free-move-on-150pct.png](02-free-move-on-150pct.png), [02z-header-150pct-crop.png](02z-header-150pct-crop.png) |
| 5 | Resize from the drawn shadow there | **observed** (owner): left edge (↔) and bottom-right corner (⤡) both resized it: x 3059→2771, 1059×873 → 1487×952 | [03-resized-on-150pct.png](03-resized-on-150pct.png), [json](03-resized-on-150pct.json) |
| 6 | Windows Snap during a free-part move (D3-windows-snap, decision 50) | **observed, unchanged from the VM**: free-part drag to the 22E1W's right edge and top edge, held ~2 s each: no Snap preview, no snap layouts; the panel stayed floating at its size (not zoomed). Snap is on for this user (`WindowArrangementActive=1`) | [04-after-snap-attempts.png](04-after-snap-attempts.png), [json](04-after-snap-attempts.json) |
| 7 | Title drag back and dock, with the drop highlight | **observed**. Owner's retry from the 150 % monitor: pressed on the title, the panel turned translucent, the accent highlight covered the top half of the Line editor group, and it docked on release. SendInput run on the 100 % monitor: translucent panel, highlight on the left half of the Grid, docked left of the Grid | [08-owner-retry-title-drag-highlight-from-150pct.png](08-owner-retry-title-drag-highlight-from-150pct.png), [09-owner-retry-docked.png](09-owner-retry-docked.png), [05-title-drag-held-over-grid.png](05-title-drag-held-over-grid.png), [06-title-drag-docked.png](06-title-drag-docked.png) |
| 7a | Owner's first attempt | not docked: the owner reported no highlight and no dock (only a double-click docked). The recording shows the panel following the pointer **opaque** with no highlight, which is the free-part (system move) path, so the drag most likely started right of the title. Nothing in the header tells the two parts apart besides the pointer position; noted as a usability observation, not a failure of the title drag | [07-owner-first-attempt-no-highlight.png](07-owner-first-attempt-no-highlight.png), [07-owner-first-attempt-trace.txt](07-owner-first-attempt-trace.txt) |
| 8 | Monitor removal with a floating panel on the second monitor (Win+P "PC screen only") | **failed (partly off-screen)**. Run 2, recorded: before, Audio at (3044,301) 1224×299 @144. Windows first moved it on-screen to (1245,200) 544×143; ~0.45 s later the app set it to **(2387,107) 816×199 @96**: the right size, but only its left 173 px is on the LM30 (screen ends at 2560); the rest is off-screen. The title stays reachable at the far right edge; the owner could not find it until told where to look. Run 1 (not recorded continuously): the panel came back at (393,377) as an 800×672 window with the panel drawn in only its top-left ~2/3, the rest of the window transparent; the owner docked it before I could test further | [12-removal-geometry-log.txt](12-removal-geometry-log.txt), [12-after-removal-right-edge.png](12-after-removal-right-edge.png), [12-after-removal-windows.json](12-after-removal-windows.json), [10-monitor-removed-panel.png](10-monitor-removed-panel.png), [10-monitor-removed-windows.json](10-monitor-removed-windows.json) |
| 9 | Monitor restored (Win+P "Extend") | **observed**: the panel stays at x=2387 and takes the 22E1W's scale (1224×299 @144), now straddling both monitors (173 px on the LM30). It does not return to its place before removal | [13-restore-geometry-log.txt](13-restore-geometry-log.txt) |
| 10 | Restart with the second monitor gone (layout points at it) | **observed**: saved layout had Audio floating at (2905,368) 816×199, `screenIndex` 1 ([13-layout-saved-with-panel-on-22E1W.json](13-layout-saved-with-panel-on-22E1W.json)). After restart the app logged `floating window "Audio" QRect(1744,368 816x199) reachable true screens QList("LM30-E330C 0,0 2560x1032")`: placed fully on the LM30 (right edge 2560), drawn whole, its "⋯" menu opens inside its frame (Move panel…, Dock, Close) | [14-restart-without-monitor-panel.png](14-restart-without-monitor-panel.png), [14-restart-without-monitor-windows.json](14-restart-without-monitor-windows.json), [14-restart-without-monitor-app.log](14-restart-without-monitor-app.log), [15-restart-panel-menu-usable.png](15-restart-panel-menu-usable.png) |

## Re-observed on real hardware in passing

Floating borderless (1), drawn shadow and the transparent margin around the frame (02, 03, 14), system move (2), resize (5),
menu position inside the floating frame (15), mixed DPI (3, 4). Not separately re-observed: focus and activation (F6 into and
out of a floating panel); the VM gate covers them.

## Still missing / for the card

- **Item 8 is a failure**: after a live monitor removal the floating panel ends up mostly off the remaining screen (run 2), or
  as an oversized window with a transparent area (run 1). The contract says removed monitors return floating groups to the main
  screen; the restart path (item 10) does this, the live path does not. Likely the scale-settle step (`dockchrome::kScaleSettleMs`)
  re-applies the logical size at a position computed for the old screen after Windows has already moved the window.
- Item 9: what restore should do is not specified; recorded as seen.
- D1 #152 and the sway/Hyprland gate (Linux) are still open; D3 cannot close from this session.
