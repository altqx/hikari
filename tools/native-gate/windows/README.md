# D1/D3 native gate harness (Windows)

The Windows half of the native gate in [docs/qt/docking.md](../../../docs/qt/docking.md)
("Required native gate and review") for cards D1 (#152) and D3 (#207, the dock chrome after MuseScore 4): NVDA,
Windows window management and drag-and-drop with real pointer input. HikariSub runs on the
desktop of the Winix VM (`winix-dev`, Windows 11, `vm: { displays: 2 }` in
`winix.yaml`: two QXL monitors at 1024x768, extended, and a USB HID tablet),
from the VM's `out/build/windows-x64-release` build and the Qt SDK it links.
Step names and evidence follow the [Linux harness](../README.md). It needs
winix 0.2.0.

| What | How |
| --- | --- |
| Keys | `winix ui keys` (SendInput with scan codes; arrows, Home, End and the other navigation keys carry the extended-key flag, so NVDA takes them as such); text through `ui text` |
| Pointer | `winix ui pointer` (the VM's USB HID tablet through host-side QMP: pressed at the path's first point, released at its last, `--hold` and `--release` for the drag held while the drop indicators are captured), `ui calibrate` (where the guest cursor lands). Windows maps the tablet to the primary monitor only, so the `pointer` step calibrates both mappings (`virtual`, `primary`) and uses the one that hits; windows go to the second monitor with Win+Shift+Right. A double-click is two `ui pointer` clicks, inside the 500 ms double-click time only because a call takes about 0.3 s (winix has no double-click at a point); it is tried up to three times, then `ui doubleclick` on the UIA title bar (SendInput, not the tablet), and the verdict says which did it. A drag or a resize presses with `--hold`, moves with `--button none` and releases with `--release`. Before a menu is opened from the keyboard the cursor is parked on the taskbar's clock: a Qt Quick menu that opens under the cursor takes its current item from the hover, and Down and Right then do something else |
| Displays | `winix ui monitors`, `ui screenshot --monitor N`; the changes through `display.ps1` (tasks `display-layout`, `gate-win-display`), as winix sets nothing: both monitors extended (`SDC_TOPOLOGY_EXTEND` with a supplied path), 1024x768 side by side; detaching the second (`SDC_TOPOLOGY_INTERNAL`); the second monitor's scale through `DisplayConfigSetDeviceInfo` (the per-monitor relative scale Settings uses). Windows allows 150 % only from about 1920x1200, so the mixed-DPI check runs the second monitor at 1920x1200 and puts it back |
| Focus | a window: `winix ui focus --pid --window hwnd:N`; a control (the Line text field): `ui setfocus --pid --name` (UI Automation SetFocus) |
| UI Automation | one `winix ui batch` per view: `windows --pid` (every top-level and owned window), `state` (the focused element and its ancestors, the foreground window, the cursor) and `tree --pid --window N` for each window. `uia.ps1` (task `gate-win-uia`) only presses controls through UIA patterns (Invoke, Toggle, SelectionItem; `ui click` falls back to a mouse click, which would not show that a screen reader can press them), focuses a control by name and type (`ui setfocus` matches by name only, and "Grid" is a pane and a tab), and reads `GetDpiForWindow` and each window's styles. winix reports no IsOffscreen: an element without area counts as off screen |
| The app | `winix ui launch --exe --arg --env PATH=<Qt SDK>;… --env QT_FORCE_STDERR_LOGGING=1 --cwd --no-console`; Qt's log is the launch log (`C:\Winix\launch\<pid>.log`, fetched with `winix pull`). `winix ui kill` ends it |
| Profile | `gate-win-fresh` sets `%LOCALAPPDATA%\HikariSub\HikariSub` (hikari.ini, layout.json) and `%APPDATA%\HikariSub\HikariSub` aside under `out/native-gate-win/old-profiles/`; nothing is deleted. `ui launch --fresh-profile` does not reach them: Qt finds those folders through the shell's known folders, not the APPDATA variables. When the run ends, `gate-win-restore` puts back the profile the first step set aside |
| Task parameters | `winix run TASK --env K=V` |

Nothing changes the VM definition: no device is added or removed.

## Running

```sh
export WINIX_OWNER=d1win                             # the lease owner every mutating call names
winix lease acquire --owner "$WINIX_OWNER" --ttl 3h
winix sync .                                         # the scripts reach the VM workspace
python3 tools/native-gate/windows/gate_windows.py    # every step except dpi
python3 tools/native-gate/windows/gate_windows.py pointer nvda
python3 tools/native-gate/windows/gate_windows.py --dpi dpi
winix lease release
```

The VM must be up with an unlocked, idle desktop (no winix job running) and
the `build` task run, plus the `test` task once (the `video` step needs its
`cfr.mkv` media fixture). Close any viewer of the VM console while the
`pointer` step runs: its own pointer moves the same cursor. `nvda-setup` runs
the first time the `nvda` step needs it.

Evidence goes to `out/native-gate-evidence/windows/`: one PNG and one TXT
(`winix ui windows --pid` and the UI Automation view as JSON) per observation,
`steps.log`, and `results.json` with a verdict per gate item: `observed`,
`failed` or `not-observable` with the reason. A failed item is never
recorded as a pass.

## Steps (`gate_windows.py [--dpi] [STEP...]`)

| Step | Gate item |
| --- | --- |
| `default` | default arrangement (one window, Video left, Audio over the Line editor, Grid below) |
| `kbd` | View > Panels > Line editor > Float and Dock from the keyboard; the draft and the editing target across both |
| `f6` | focus after Float, F6 from and into a floating panel, Ctrl+Shift+H from it |
| `move` | View > Move panel… (keyboard placement window): Audio left of the Grid |
| `pointer` | tablet calibration (where the guest cursor lands); the header's "⋯" button and its menu's Undock; double-click on a title bar and on a tab (float, then dock); drag a floating panel onto the Grid with the drop highlight captured mid-drag; then, with the main window on the monitor's left (`gate-win-fullscreen` `left`), a floating panel's window: borderless tool window (`WS_CAPTION`, `WS_THICKFRAME` off, `WS_EX_TOOLWINDOW`; no system title bar in UI Automation), the drawn shadow (the background shows through its outer ring, `ptr-8-floating-shadow.png`), moved by its header over bare desktop, resized from the shadow's left and top edges (`startSystemResize`) |
| `video` | Video panel with `cfr.mkv` docked, floating, redocked: frames keep presenting while stepping |
| `persist` | float Audio, close the main window (`ui focus`, Alt+F4), restart: Audio comes back floating |
| `fullscreen` | the main window borderless over the whole monitor (Windows has no fullscreen request for another application's window and HikariSub has no fullscreen command) with a floating panel; F6 through both |
| `outputs` | floating Audio moved onto the second monitor at 150 % (primary 100 %): on it, `GetDpiForWindow` 144 (per-monitor aware, not bitmap-scaled), 1.5x its width, that monitor's own pixels (`ui screenshot --monitor 1`); that monitor detached: the panel back on the primary, View > Panels > Audio > Show focuses it; finally both monitors at 1024x768, 100 %, extended (`outputs-restored`). Not observable if only one monitor reaches the desktop |
| `nvda` | NVDA's speech while F6 moves through the panels and a panel is floated from the View menu (`nvda`, which also requires the silence synthesizer), that the Float worked with NVDA running (`nvda-float-from-menu`), and the Grid tab's "⋯" button and menu spoken (`nvda-header-menu`) |
| `menutext` | Alt+V from the Line text field |
| `tests` | `hikari_ui_shell_tests` (D1 and D3 functions, the menu arrows), `hikari_ui_line_grid_a11y_tests`, `hikari_ui_docking_qualification_tests`, `hikari_ui_workspace_layout_tests` with `QT_QPA_PLATFORM=windows` (task `gate-win-tests`) |
| `a11y` | what UI Automation exposes of the docking controls: a lone panel's header as a title bar named after it, tabs (the Grid's one tab included) in a tab list "Panels", the "⋯" button as "<panel> options", no leftover Float/Close buttons and no title repeated as text; "⋯" and its menu's Undock and Dock pressed through the Invoke pattern; the Grid's table under the Grid panel |
| `header` | the "⋯" menu from the keyboard: F6 to the Grid, Shift+Tab to its tab, Right to "⋯", Space (Move panel…, Undock, Close); Shift+F10 on the tab; a lone panel's "⋯" (Return), Undock and, in the floating window, Dock |
| `dpi` | only with `--dpi`: the one monitor's scale 100 -> 150 % live (`SPI_SETLOGICALDPIOVERRIDE`, no sign-out) with a floating panel, typing there, then back to 100 %. Not observable if the guest does not change scale without signing out |

Submenus: the first Float from View > Panels opens the submenus with Right
and records `submenu-right-arrow`; should Right not open them, it records the
failure and the run continues with Return.

## NVDA

`nvda-setup` downloads NVDA 2026.2 from download.nvaccess.org, checks the
SHA-256 NV Access publishes for it (`f3f8d299…bca824`), and creates a portable
copy under `out/native-gate-win/nvda/portable-2026.2`
(`--create-portable-silent`); nothing is installed. Its configuration
(`nvda/config/nvda.ini`) selects the "No speech" synthesizer (`silence`) and
turns off sounds, the welcome dialog and update checks. The step starts it
with `--minimal --log-level=12` (io: every `Speaking [...]` is logged) and a
log file per run; `nvda-output` quits it (`nvda.exe -q`), prints the speech
lines and keeps the whole log as the job artifact (`nvda.log` in the
evidence).

## Not observable here

- With one monitor (`vm.displays` 1), mixed DPI and monitor removal: the
  `outputs` step says so.
- Windows' own Settings > Display flow is not driven; `display.ps1` and the
  `dpi` step use the calls Settings uses.
