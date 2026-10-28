# Notices: the inventory (NT1, [#302](https://github.com/altqx/hikari/issues/302))

Every popup whose only button was OK, and every message that popped legacy's Log window up, as qt stood at
ce0c6740e, with the kind each takes since NT1 (docs/qt/ux/visual-language.md, "Notices and the Log"). The
owner approved the kinds and titles with the [mockup](https://github.com/altqx/hikari/issues/302#issuecomment-6329132646)
on 2026-10-22 ([approval](https://github.com/altqx/hikari/issues/302#issuecomment-6329169301)); the departures are
NT1-log-panel, NT1-log-notices, NT1-ok-notices, NT1-read-only-save and NT1-select-lines-result in
docs/qt/compatibility-decisions.md. "List" says whether the popup was on PK1's allow-list
(`tests/ui/in_window_popups.txt`); all 16 that were are off it. Paths under `src/`; line numbers are qt's at
ce0c6740e. The PU cards follow the same rule for the popups they move.

## A. Popups whose only button is OK (18 objects, 29 messages)

| # | Where (id) | Today's title | Text | Sev | List | New kind | New title |
|---|---|---|---|---|---|---|---|
| A1 | ui/Main.qml spellingNotice | Spellchecker | No dictionary files were found… / Failed to initialize spell checker. | warning / error | yes | toast | Spellchecker |
| A2 | same | Spellchecker | Error. Word "%1" was not added. | error | yes | toast | Add word to dictionary |
| A3 | ui/SpellCheckerDialog.qml message | Warning | No spelling errors were found | info | yes | toast | Spellchecker |
| A4 | same | Warning | The edited line cannot be committed. (and Replace refusals) | error | yes | message box; a toast since PU6 (the Spellchecker a panel) | Replace / Replace all |
| A5 | ui/Main.qml cleanStylesMessage | Status of deleted styles | Styles used / Styles deleted lists | info | yes | toast + Details | Delete unused styles |
| A6 | ui/StyleManager.qml message (Clear) | Status of deleted styles | same | info | yes | toast + Details | Delete unused styles |
| A7 | same (createStyle, commit) | none (stale) | The document has no styles to edit. / Style named "%1" already exists. | error | yes | message box | New style / Save style |
| A8 | ui/Main.qml playerNotice (Windows) | Warning | The file is not a valid video file… | warning | yes | toast + Details | Open video |
| A9 | ui/VisualToolDialogs.qml noticeBox | Warning | Double "m" was blocked because of Vsfilter bug | warning | yes | toast | the tool's name |
| A10 | ui/AutomationNotices.qml notice | Info | This subtitle file does not have any scripts added | info | yes | toast | Run the last loaded script |
| A11 | same | Error | Error loading Lua script: %1 | error | yes | message box + Details | Run the last loaded script |
| A12 | same | Error | The script's helper ended; reload the script… | error | yes | message box | Run the last loaded script |
| A13 | same | Automation error | Cannot start editor. | error | yes | message box | Edit script |
| A14 | same (+ Log pop-up L26) | Error | Validation Lua script '%1' failed | error | yes | message box + Details | Run the last loaded script |
| A15 | ui/UpdateMessage.qml updateMessage | Check for updates | You already have the latest version | info | no | toast | Check for updates |
| A16 | same | Check for updates | Cannot check for updates | warning | no | toast + Details | Check for updates |
| A17 | same | Update | download / checksum failures | error | no | toast + Details | Download and install |
| A18 | same (next start) | Update | the last install's result | error | no | toast + Details | Install update |
| A19 | ui/Main.qml readOnlyWarning | Warning | Chosen file is read only… | warning | yes | message box: Save as… / Cancel | Save |
| A20 | ui/SessionWindows.qml readOnlyNotice | Warning | same | warning | yes | message box: Save as… / Cancel | Save session |
| A21 | ui/MatroskaSubtitles.qml noTracksMessage | Load subtitles from an MKV/OGM file | The file does not contain any subtitle tracks. | info | yes | toast | same |
| A22 | ui/AllTagsEditionDialog.qml messageBox | Error | Add/Remove/Save refusals | error | yes | inline error under the field (PU5-inline-errors) | Edit tags |
| A23 | ui/ShapesEditionDialog.qml messageBox | Error | Add/Remove/Save refusals | error | yes | inline error under the field (PU5-inline-errors) | Edit shapes |
| A24 | ui/FontCatalogWindow.qml info | Info | Enter a name for the new catalog | warning | yes | message box | Manage font catalogs (PU3: gone; New and Rename say an empty name inline in their popovers) |
| A25 | ui/FontCollectorDialog.qml message | none | Start refusals | warning / error | yes | message box | Font collector |
| A26 | ui/SettingsDialog.qml warningDialog | Warning | The selected %1 for conversion does not exist… | warning | yes | toast | Settings |
| A27 | ui/SelectLinesDialog.qml result | Select (Close + OK) | %1 lines selected. … | info | yes | toast | Select lines |
| A28 | ui/SearchTool.qml question, kind 0 | Confirmation / Find and Replace | Could not find… / Replaced %1 times. | info | yes (shared) | toast | Find / Replace all |
| A29 | same, kind 0 | none | Search path is invalid | error | yes (shared) | message box | Find in files |

16 objects leave the allow-list; SearchTool:question stays on it for its Yes/No questions (PU2). The update
notices were already windows of their own (DG1); UpdateMessage.qml is gone. A11 and A14 put the script's
error text under Details.

PU6 ([#308](https://github.com/altqx/hikari/issues/308)) moved the questions of its popups the same way: the
Font collector's "The zip file already exists, delete it?" (Yes / No, titled "Confirmation") is a message box
titled "Font collector" with Replace / Cancel (PU6-font-collector-replace), and "Incompatible resolution"
(Change / Do not change / Disable warning) a message box titled "Open video" with its choices as buttons and
"Don't ask again" (PU6-resolution-question). A3 and A4 come from the Spellchecker panel (PU6-spellchecker-panel).

PU7 ([#309](https://github.com/altqx/hikari/issues/309)): Settings > Hotkeys' Import legacy hotkeys… tells
its result in a toast titled "Import legacy hotkeys": info with how many bindings were added, kept and
skipped (the skipped ones, with their keys, under Details), an error when the file cannot be read. The AUR
notice (UpdateAurNotice.qml) is gone: an AUR install's release opens the update window (PU7-aur-command).

## B. Messages that pop the Log window up (49 call-site groups)

Each is a toast with the severity below, titled with the command, and a Log entry; the text is unchanged. ★
marks the data-loss or stopped-open errors, which the owner kept as toasts (error toasts stay until ×).

| # | Call site (src/app unless shown) | Text | Sev | Title |
|---|---|---|---|---|
| L1 ★ | application.cpp:2166 | %1 could not be saved. | error | Save |
| L2–L7 ★ | application.cpp:2232, 2374, 2378, 3160; application_tab_menu.cpp:503, 551 | Could not open / reload %1; nothing was changed. | error | Open subtitles / Reload subtitles / Paste translation |
| L8 ★ | application_tabs.cpp:642 | Session file is corrupt | error | Load session from file |
| L9–L11 | application_tabs.cpp:801, 810, 816 | Retry failures | warning | Restore session |
| L12 | application.cpp:2998/3000 | Auto save folder is empty / Cannot open auto save folder | info | Open auto save |
| L13 ★ | application.cpp:3052 | Failed to load autosave | error | Open auto save |
| L14 ★ | matroska_controller.cpp:29 | Indexing error occurred: %1 | error | Load subtitles from an MKV/OGM file |
| L15–L16 | ui/Main.qml:1536, 6377; application_tab_menu.cpp:459, 565, 317; application.cpp:952; application_tabs.cpp:865 | Invalid keyframes format | error / warning | Open keyframes |
| L17 ★ | application_video.cpp:90–99 | Indexing error / Cannot create VideoSource / … | error | Open video |
| L18 | application.cpp:675 | Cannot change YCbCr matrix | warning | Video |
| L19 | ui/Main.qml:5971 | Invalid FPS value. | error | Open dummy video |
| L20 | application.cpp:3272 | Video was not loaded using FFMS2 | warning | Shift times |
| L21 | application.cpp:4464, 4483 | In line %1, value '%2' cannot be scaled… | warning | Resample subtitles (one toast per run, the lines in Details) |
| L22 | application.cpp:643 (visual tools) | Video must be set… / Bad range / Cannot measure text… | warning | the tool's name |
| L23 ★ | application/audio_box.cpp (open, index, cache) | Cannot open audio %s / … | error | Open audio |
| L24 | application/audio_box.cpp:323 | Delay failed, it's longer than audio duration time | warning | Open audio |
| L25 | backends/audio_box_player.cpp:123, 288, 596 | Failed creating audio playback device. / Audio player failed. | error | Audio |
| L26 | automation_shell.cpp:792 | validation runtime error | error | Details of A14's message box |
| L27 | hotkeys_controller.cpp:315 | Shortcut "%1" is invalid | warning | Hotkeys |
| L28 | hotkeys_controller.cpp:227 | The shortcut %1 of "View all" was removed… | info | Hotkeys |
| L29–L37 | application.cpp:254, 349, 366, 368, 3378, 3381 | find/replace and select errors | error / warning | Find and replace / Select lines |
| L38–L45 | application.cpp:3594–3749 | Misspell replacer rule errors | error / warning | Fix minor errors |
| L46 | application_language.cpp:48 | Cannot find translation, language change failed | warning | Language |
| L47 | font_catalogs_controller.cpp:334 | For adding fonts to catalog, click "Manage" first… | warning | Font catalogs |
| L48–L49 | font_catalogs_controller.cpp:387, 397 | Cannot load external font folder / file | warning | Font catalogs |

Found on the way: the Style manager's message box kept "Status of deleted styles" as its title for later
commit errors (A7); its errors are titled "New style" and "Save style" now. `font_catalogs_controller.cpp:393`
says legacy logged "Cannot add external font file" silently, but the hook at application.cpp:814 logs it as a
notice; NT1 keeps that (a warning toast titled "Font catalogs").
