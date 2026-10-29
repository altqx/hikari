# Legacy observations: S4 Document scripts (local capture, 2026-10-13)

Captured by [`document_scripts_capture.py`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/document_scripts_capture.py) (the plan's `document_scripts` section) in the `hikari-legacy-env` container (Ubuntu 24.04, Xvfb 1280x800, openbox), from the legacy Linux app built at `20d647c4` by [legacy-capture](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/.github/workflows/legacy-capture.yml) run 38309025775 (its `legacy-app` artifact: package sha256 `d42fcffa…1dbad`, its wx libraries on `LD_LIBRARY_PATH`):

```
Xvfb :99 -screen 0 1280x800x24 -nolisten tcp &
DISPLAY=:99 openbox &
LD_LIBRARY_PATH=<wx-prefix-lib>/lib DISPLAY=:99 \
python3 tools/legacy-capture/document_scripts_capture.py --plan tools/legacy-capture/plan.json \
    --package hikarisub-linux-x86_64.tar.gz --out s4.json
```

Without a window manager (a bare Xvfb) legacy does not open the document it is given and gets no keys, so the route needs one, as the other routes do.

Each case runs a fresh copy of the app with Autoload emptied, the scripts of [`automation/document-scripts`](../../legacy-cases/automation/document-scripts) written out in a folder outside the app (`{abs}`) and in the app's folder (`s4/`, relative to its working directory), and a document (two Lines, "Gate" and "Second") whose Script Info names them in "Automation Scripts". "Run the last loaded script" is bound to Ctrl+Shift+F12 in `Config/Hotkeys.txt` and pressed after a click on the Line editor's Effect field. Each script records its load, its validation and its macro, with the Document as it sees it (the first Line's text, the Line count, the Script Info keys). `s4.json` is the complete record; `screens/` keeps the boxes, the log window, the editor picker and the grid after the second failed validation.

## What legacy did

- **The hotkey rebuilds the Automation menu first.** The command is an Automation menu item, and `OnMenuSelected` brings an accelerator's menu up to date before running it (`HikariSubFrame.cpp:680-686` → `OnMenuOpened` → `Automation::BuildMenu`). BuildMenu reads "Automation Scripts" (`AddFromSubs`), loads the scripts (each top level runs once, in the listed order), reloads a changed one, and **validates every macro that has a validation function** to enable its item (`Automation.cpp:1375, 1434`). Then the command validates the last script's first macro again and runs it. So with `|{abs}/first.lua|missing.lua|s4/last.lua`: first load, last load, first validate, last validate, last validate, last run. "Second macro" (no validation function) is not called.
- **Run.** The last script's first macro runs; the title shows the change count (`1*s4.ass`). After `touch` (the file a minute newer) the next run reloads it once (in BuildMenu): last load, then its validations and run. An unchanged file is not reloaded. The separators `|`, `~` and `$`, leading spaces and empty tokens give the same scripts; `missing.lua` is skipped silently.
- **No scripts.** No "Automation Scripts", or only missing files: "Info" box, "This subtitle file does not have any scripts added".
- **Validation false.** The validation function sees the Document, edits the first Line ("edited while validating") and inserts a "Validated" Script Info key, and answers false. It runs twice per press (BuildMenu, then the command), and the edits stay in the file: the command's validation sees the menu build's edits, and the next press sees one more "Validated" key each time (7, 8, 9 keys). "Error" box, "Validation Lua script 'invalid.lua' failed". The grid shows the edited Line, but the title has no change count and the Line editor still shows "Gate".
- **Validation error.** Called twice as well; the "Log window" opens over the "Error" box with "Runtime error in Lua macro validation function:" and the traceback ("no way"). Return does not close the log window (its Close button does).
- **Load error.** A script that does not parse: "Error" box, "Error loading Lua script: broken.lua" and the Lua message, then "Select a script editor" (no editor set, `Automation::OnEdit`). Nothing is recorded (the script never ran).
- **Script Info as scripts see it.** "Title", "ScriptType", "Automation Scripts", then "YCbCr Matrix" (added on load, `SubsLoader.cpp:167-168`) and "PlayResX", "PlayResY" (written by `SubsGrid::GetASSRes` when the status bar reads the resolution, `SubsGridBase.cpp:1653-1675`), none of which is in the file.

## Compared with the rewrite

`AutomationDocumentScriptsTests::replaysTheLegacyCapture` (`hikari_ui_automation_document_scripts_tests`) runs every case through the application with real Lua helpers and the same scripts, and compares each press with legacy's: the scripts loaded (as a set: each loads in its own helper), then every validation and run in order with the Document it saw, the box (title and text; validation-error's text is the one `HikariSubFrame.cpp:924` gives a validation that answers false, since the log window covers the box in its screenshot and a raising validation answers false, `Automation.cpp:952-956`), the log entry, the editor picker, and the modified state. Equal, with these differences, each named:

- Legacy's menu-build validations are left out: the rewrite validates when it runs, and does not validate on the menu's build or on the hotkey (an open item in [S4](../../../../docs/qt/coverage.md)).
- "YCbCr Matrix" (C03-ycbcr-on-load) and "PlayResX"/"PlayResY" are not in the rewrite's Script Info. The PlayRes keys have no approved row: the rewrite reads the resolution without writing it, an open item in [Y4](../../../../docs/qt/coverage.md) awaiting a decision.
- The validation function's edits are dropped (S4-validation-edits): in validation-false the rewrite's validations see "Gate" and no "Validated" key.

These keys and edits are taken off legacy's records only (each of the three keys once; "Validated" and the edited text only in validation-false); the rewrite's records are compared whole, so a key it adds or drops on its own fails the comparison.

## The Windows capture

`s4-windows.json` is the same plan run on the Windows release `v0.0.1-rc.1` (`HikariSub-0.0.1-rc.1-windows-x64.zip`, sha256 `20a1b0fa…5e9a1e`, legacy `20d647c4`) in the Winix VM (Windows 11 build 26200) on 2026-10-14: winix `legacy-win-setup` job-15b77f0054d44fd181968eea9e89baf0 (tree at `9aa897065`, clean), then

```
python3 tools/legacy-capture/drive_windows.py --plan tools/legacy-capture/plan.json --out s4-windows.json --document-scripts
```

which runs each case through the `legacy-win-s4-*` tasks. All seven cases were captured, and six are the same as on Linux. In runs-last the scripts see one more Script Info key, "Active Line", in step 3 (the third press), as in four earlier Windows runs of the same plan (2026-10-14, 01:28 to 02:32 UTC). That is legacy's autosave: `SubsGrid::OnBackupTimer` saves 20 s after the first change since the last autosave (`SubsGridBase.cpp:1085-1088`) through `SaveFile`, whose `GetAssHeader` writes the active Line's row as "Active Line" into the open Document's Script Info (`SubsGridBase.cpp:1470`). The Windows driver waits 8 s after each hotkey and runs a winix task for each record read and for the touch, so the timer that step 0's run started fired before step 3; the Linux route's steps all came within the 20 s. The title stays `3*s4.ass`: an autosave does not mark the file saved.

`replaysTheLegacyCapture` replays both files. With the Windows one it autosaves (`Application::autosaveNow`) before the first step whose legacy records show the key, and the rewrite's scripts then see "Active Line" too (decision 49, 2026-10-15: legacy's write is ported, autosave included). Timed autosaves are off in the replay (`autosave.maxFiles` 0), so neither file depends on how long its steps take.

## Not observed here

The Automation menu itself (its items' enabled state after validation); a script editor that is set.
