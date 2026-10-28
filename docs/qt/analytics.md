# Anonymous usage data

HikariSub sends anonymous usage data to PostHog Cloud EU (`https://eu.i.posthog.com`). This page lists every event and property it can send ([AN1 #404](https://github.com/altqx/hikari/issues/404)).

## The basics

**It is on by default.** That includes copies updated from an earlier version and settings imported from legacy, which never had this option.

**To turn it off**, use either:
- **Settings > Updates and privacy > Send anonymous usage data**
- **Getting started**, on its Updates page

Turning it off stops sending at once. It also deletes the events waiting to go and the install id. If you turn it on again, you start with a new id.

**`DO_NOT_TRACK`** set to anything other than `0` in the environment turns sending off for that run.

**Only release packages send.** The PostHog project key is compiled in by the release workflows (`HIKARI_POSTHOG_KEY`). A build without the key queues nothing and sends nothing; the switch still shows. That covers builds from source, forks, CI test builds and tests.

**What identifies you:**
- A random UUID made the first time something is sent (`privacy.usageDataId`). It is not tied to anything else.
- No person profiles are made (`$process_person_profile: false`).
- PostHog does no GeoIP lookup (`$geoip_disable: true`). The project discards client IP addresses.

**Never sent:** file names, paths, subtitle or script text, Style or font names, script names other than the bundled scripts', saved layout names, or anything else you typed. Every property is an id from a closed list, a bucket, a bool, a version or a count. The client checks each event against the table below and drops any that does not match. The tests walk a scripted session and fail on any free text: `analytics_session_tests`, using the check in `tests/support/analytics_endpoint.h`.

## How it is sent

- Events wait in memory and in `analytics-queue.json` in the cache folder, at most 1000 events or 1 MB. Past that, the oldest are dropped.
- They are sent to `POST /batch/`, at most 100 at a time:
  - 60 seconds after start, then every 5 minutes
  - once at quit, after the window has closed, waiting at most 2 seconds
- A send that fails waits for the next one. There is no retry loop, no waiting in the window, and no error message.

## Properties on every event

| Property | Value |
|---|---|
| `distinct_id` | The install's random UUID |
| `$process_person_profile` | `false` |
| `$geoip_disable` | `true` |
| `$session_id` | A random UUID for each run, never stored |
| `$lib` | `hikarisub` |
| `$lib_version` | The app version, e.g. `0.1.0-beta.1` |

## Events

### Usage basics

| Event | When | Properties |
|---|---|---|
| `app_started` | The main window's first frame | `version`: the app version<br>`channel`: `stable` or `prerelease`<br>`$os`: `windows`, `linux` or `other`<br>`$os_version`: the system's version (`QSysInfo::productVersion`), else `other`<br>`arch`: `x86_64`, `arm64` or `other`<br>`package`: `installer`, `portable`, `appimage`, `deb`, `rpm`, `aur` or `build_tree`<br>`qt_version`: e.g. `6.11.2`<br>`language`: `en`, `ko_KR`, `pl`, `ta` or `th_TH`<br>`theme`: `light`, `dark`, `high_contrast_white` or `high_contrast_black`<br>`theme_follows_system`: bool<br>`layout`: `editing`, `timing`, `translation`, `typesetting`, `karaoke`, `saved` or `custom`<br>`screens`: `1`, `2` or `3+`<br>`scale`: `1x`, `1.25x`, `1.5x`, `2x` or `3x+`<br>`startup_time`: a time bucket |
| `app_closed` | Quit | `session_length` (since start, or since sending was turned on): `<1m`, `1-5m`, `5-15m`, `15-60m`, `1-3h` or `3h+`<br>`render_frames_lt8ms`, `render_frames_8_16ms`, `render_frames_16_33ms`, `render_frames_33_66ms`, `render_frames_66_200ms`, `render_frames_200ms_plus`: how many subtitle frames took that long to draw this session, while sending was on |

Time buckets: `<250ms`, `250-500ms`, `0.5-1s`, `1-2s`, `2-5s`, `5-15s`, `15-60s`, `60s+`.

### Feature usage

| Event | When | Properties |
|---|---|---|
| `command_used` | A command runs (see the note below the table) | `command`: the command's hotkey symbol, e.g. `GLOBAL_SAVE_SUBS` or `AUDIO_PLAY`. Only symbols in the hotkey table are sent; an automation macro's hotkey never is.<br>`via`: `key`, or `pointer` (a menu, toolbar or panel button) |
| `panel_opened` | You show a panel (View menu, the panels' menus, Fix minor errors…) | `panel`: `video`, `audio`, `editor`, `grid`, `reference`, `timing`, `search`, `automation`, `original`, `log`, `styles`, `fix_minor_errors`, `template` or `spellchecker` |
| `dialog_opened` | A window on the dialog frame opens | `dialog`: one of `about`, `allTagsEdition`, `aspectRatio`, `autosaves`, `columns`, `conversion`, `crashReport`, `credits`, `dummyVideo`, `fontCatalogClashes`, `fontCatalogs`, `fontCollector`, `fontPicker`, `fontsFromSubtitles`, `gettingStarted`, `history`, `hotkeyCapture`, `kanjiTimer`, `karaokeCheck`, `listChooser`, `manageLayouts`, `movePanel`, `reportIssue`, `resample`, `saveLayout`, `scriptProperties`, `selectLines`, `setNewFps`, `settings`, `settingsImport`, `shapesEdition`, `spellAddedWords`, `styleClashes`, `styleManager`, `translationShift`, `unsavedChanges`, `updateAvailable`, `whatsNew`, or `automation_script` (any script's dialog) |
| `layout_changed` | You choose a layout | `layout`: `editing`, `timing`, `translation`, `typesetting`, `karaoke`, or `saved` (one of your saved layouts; its name is not sent) |
| `getting_started_finished` | Getting started's Finish | `pages_changed`: the pages whose settings Finish changed, from `language`, `import`, `look`, `layout`, `media`, `editing`, `updates`, `files`. Which values you chose is not sent. Getting started has no setting on its layout and files pages yet, so those two are not sent today. |
| `getting_started_skipped` | Getting started's Skip | (none) |

Where `command_used` is reported:
- `via: key`: every command a key runs. The shell reports it through `HotkeysController::ran`.
- `via: pointer`: every menu item and button that passes through `Main.hotkeyGesture`, the check for a Shift+click that maps a hotkey.

**Volume:**
- `command_used` goes at most once per command and `via` per session.
- `panel_opened` and `dialog_opened` go at most once per id per session.
- `layout_changed` goes each time, up to 20 per session.

### Errors and crashes

| Event | When | Properties |
|---|---|---|
| `$exception` | The start after a native crash or a freeze the program was closed in, and a freeze it came back from (CR1) | `$exception_list`: one entry: `type` and `value` (the signal, `SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE` or `SIGILL`; on Windows the exception's name, e.g. `EXCEPTION_ACCESS_VIOLATION`, else its code `0x…`; `APP_HANG` for a freeze), `mechanism` (`{type: signal, seh or watchdog, handled: false}`), `stacktrace` (`{type: raw, frames}`), up to 32 frames of `{platform: native, module, offset}`<br>`$exception_level`: `fatal`, `error` for a freeze<br>`crashed_version`: the version that crashed<br>`report_id`: a random UUID, the same as its `crash_report`'s<br>`freeze_seconds`: how long it froze (a freeze only) |
| `crash_report` | You press Send report on a crash or freeze report, or "Always send crash reports" sends it (CR1, [crash-reports.md](crash-reports.md)) | `report_id`: its `$exception`'s<br>`kind`: `crash` or `freeze`<br>`note`: what you typed, up to 4000 characters<br>`log`: the Log's last 200 lines, file names, fonts, Line text and folders replaced<br>`settings`: the settings summary (ids and values from a closed list, up to 64)<br>`version`, `channel`, `$os`, `$os_version`, `arch`, `package`, `qt_version`: as `app_started`'s |
| `automation_error` | A macro fails, its helper is lost or force-stopped, or a script does not load | `kind`: `failed`, `lost`, `stopped` or `load_failed`<br>`script`: a bundled script's file name, while its bytes are the bundled manifest's (in the program's Automation folder or copied to your folder; spaces become `_`), else `user script`. A script you add to the program folder is `user script`.<br>A macro whose validation declines to run is not an error and sends nothing. |

How a crash is recorded:
- A signal handler (Linux) or an unhandled-exception filter (Windows) writes a small record to `crash-record.txt` in the cache folder; the hang watchdog writes `hang-record.txt` (CR1, [crash-reports.md](crash-reports.md)).
- The handler is always installed (CR1). The record stays on this machine: with sending on, only the record goes as `$exception` at the next start. The rest of a report goes only when you press Send report, which works with sending off too: pressing it is consent for that one report, sent under a one-off random id.
- A frame's `module` is a file name only, and only for the program's own files (in its folder) or the system's (`/usr`, `/lib`, the Windows folder). Any other module is `other`.
- `offset` is the hex offset into that module.

### Performance

| Event | When | Properties |
|---|---|---|
| `video_opened` | A video open ends | `open_time`: a time bucket<br>`outcome`: `ready` or `failed`<br>`kind`: `file` or `dummy` |
| `audio_opened` | An audio open ends: all of it is loaded, or it fails (one closed or replaced on the way sends nothing) | `open_time`: a time bucket<br>`outcome`: `ready` or `failed`<br>`kind`: `file`, `from_video` or `dummy` |

Startup time is `app_started`'s `startup_time`. The frame-time buckets are in `app_closed`.

## For developers

**Code:**
- `src/app/analytics.cpp` holds the client and the table (`Analytics::conforms`).
- `src/app/crash_record.cpp` holds the crash record; `src/app/crash_reports.cpp` and `crash_report.cpp` the reports (CR1).

**Tests**, against a fake endpoint on the loopback:
- `analytics_tests`
- `analytics_session_tests`
- `crash_report_tests` (CR1)

**Adding an event:**
1. Add it to the table in `analytics.cpp` and to this page.
2. Extend `analytics_session_tests` if the session can make it.
