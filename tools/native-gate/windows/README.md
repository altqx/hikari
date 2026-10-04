# D1 native gate harness (Windows)

The Windows half of the native gate in [docs/qt/docking.md](../../../docs/qt/docking.md)
("Required native gate and review") for card D1 (#152): NVDA, Windows window
management and drag-and-drop with real pointer input. HikariSub runs on the
desktop of the Winix VM (`winix-dev`, Windows 11, two QXL monitors from
`tools/winix/monitors.sh` with the QXL-WDDM-DOD driver, a USB HID tablet), from the VM's `out/build/windows-x64-release`
build and the Qt SDK it links. Step names and evidence follow the
[Linux harness](../README.md).

| What | How |
| --- | --- |
| Keys | sequences with a navigation key (arrows, Home, End) through `gate.ps1 -Action keys` (task `gate-win-keys`): SendInput with scan codes and the extended-key flag, as a keyboard sends them; other chords through `winix ui keys`. `winix ui keys` sends arrows without the extended flag, i.e. as keypad keys: Qt still moves, but NVDA takes them as review-cursor commands (`numpad2`) and keeps them |
| Pointer | the VM's USB HID tablet through QMP `input-send-event` (`virsh -c qemu:///session qemu-monitor-command winix-dev`): absolute x/y and the left button, in small steps with short sleeps. The area the tablet's 0..32767 spans is measured (two raw positions, the guest cursor at each): Windows maps it to the primary monitor only. A drag carried on with relative motion (`rel`, the PS/2 mouse) reaches the second monitor, but the tablet's button-up report puts the cursor back on the primary, so windows go to the second monitor with Win+Shift+Right |
| Displays | `display.ps1` (tasks `display-layout`, `gate-win-display`): both monitors extended (the QXL secondary's path activated with a supplied SetDisplayConfig; `SDC_TOPOLOGY_EXTEND` alone fails), 1280x800 side by side; detaching the second (`SDC_TOPOLOGY_INTERNAL`); the second monitor's scale through `DisplayConfigSetDeviceInfo` (the per-monitor relative scale Settings uses). Windows allows 150 % there only from about 1920x1200, so the mixed-DPI check runs the second monitor at 1920x1200 and puts it back |
| Focusing a window | `winix ui click` on its title bar, as a user would |
| Screenshots | `winix ui screenshot` |
| UI Automation | `uia.ps1` (task `gate-win-uia`): the app's windows with the control-view walker, the focused element and its path, the foreground window, the cursor; it also invokes, focuses or closes named elements (the AT-SPI `do` of the Linux gate). Floating panels, the placement window and dialogs are owned by the main window, so UI Automation nests them under it (`winix ui windows/tree/find` do not reach them). A Subtree cache request fails once such a window exists, and Grid rows report the main window's HWND, which makes UI Automation list the window again under the row: the walk stops there |
| The app | `launch.ps1` through `winix ui launch` (a winix task ends every process it started); PATH gains the Qt SDK, Qt's log goes to `out/native-gate-win/app-*.err.log`. `hikarisub.exe` is a console program, so it starts through `cmd /c` without a console window (one in the default terminal can take the foreground) |
| Profile | `gate-win-fresh` sets `%LOCALAPPDATA%\HikariSub\HikariSub` (hikari.ini, layout.json) and `%APPDATA%\HikariSub\HikariSub` aside under `out/native-gate-win/old-profiles/`; nothing is deleted |

Nothing changes the VM definition: no device is added or removed.

## Running

```sh
winix sync .                                         # the scripts reach the VM workspace
python3 tools/native-gate/windows/gate_windows.py    # every step except dpi
python3 tools/native-gate/windows/gate_windows.py pointer nvda
python3 tools/native-gate/windows/gate_windows.py --dpi dpi
```

The VM must be up with an unlocked, idle desktop (no winix job running) and
the `build` task run, plus the `test` task once (the `video` step needs its
`cfr.mkv` media fixture). Close any viewer of the VM console while the
`pointer` step runs: its own pointer moves the same cursor. `nvda-setup` runs
the first time the `nvda` step needs it.

Evidence goes to `out/native-gate-evidence/windows/`: one PNG and one TXT
(`winix ui windows` and the UI Automation view as JSON) per observation,
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
| `pointer` | tablet calibration (where the guest cursor lands); the title-bar Float button, double-click on title bars, drag a floating panel onto the Grid with the drop indicators captured mid-drag |
| `video` | Video panel with `cfr.mkv` docked, floating, redocked: frames keep presenting while stepping |
| `persist` | float Audio, close the main window (title-bar click, Alt+F4), restart: Audio comes back floating |
| `fullscreen` | the main window borderless over the whole monitor (Windows has no fullscreen request for another application's window and HikariSub has no fullscreen command) with a floating panel; F6 through both |
| `outputs` | floating Audio moved onto the second monitor at 150 % (primary 100 %): on it, `GetDpiForWindow` 144 (per-monitor aware, not bitmap-scaled), 1.5x its width, the head's own framebuffer (`virsh screenshot --screen 1`); that monitor detached: the panel back on the primary, View > Panels > Audio > Show focuses it; finally both monitors at 1280x800, 100 %, extended (`outputs-restored`). Not observable if only one monitor reaches the desktop |
| `nvda` | NVDA's speech while F6 moves through the panels and a panel is floated from the View menu (`nvda`), and that the Float worked with NVDA running (`nvda-float-from-menu`) |
| `menutext` | Alt+V from the Line text field |
| `tests` | `hikari_ui_shell_tests` (D1 functions), `hikari_ui_docking_qualification_tests`, `hikari_ui_workspace_layout_tests` with `QT_QPA_PLATFORM=windows` (task `gate-win-tests`) |
| `a11y` | what UI Automation exposes of the docking controls: named title-bar buttons and tab items (each with Float and Close), the Grid's table under the Grid panel; invoking the buttons floats and docks |
| `dpi` | only with `--dpi`: the one monitor's scale 100 -> 150 % live (`SPI_SETLOGICALDPIOVERRIDE`, no sign-out) with a floating panel, typing there, then back to 100 %. Not observable if the guest does not change scale without signing out. Its screenshots also include the host's view of the framebuffer (`virsh screenshot`), because the winix worker that takes the others is not DPI aware |

Submenus: Right does not open View > Panels (or a panel's submenu) on
Windows, whether or not the extended-key flag is set; Return does. The first
Float records `submenu-right-arrow` and the run continues with Return.

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

- With the single VGA head (before `tools/winix/monitors.sh two`), mixed DPI
  and monitor removal: the `outputs` step says so.
- Windows' own Settings > Display flow is not driven; `display.ps1` and the
  `dpi` step use the calls Settings uses.
