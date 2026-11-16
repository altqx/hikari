# CX7 round 3 shot manifest

Every file is a light/dark pair (`<name>-{light,dark}.png`) unless the row says otherwise. Test functions are in `tests/ui/release_screenshots_tests.cpp` (branch w8-cx7-shots). They're gated by `HIKARI_SURFACE_SHOT_DIR`, plus `HIKARI_SHOT_VIDEO` for Sprite Fright or `HIKARI_SHOT_KARAOKE_*` for the Ray MV. Fresh temporary profiles, xvfb on akari-rvm.

Commons: **yes** = the picture shows lines from the Wikimedia Commons TimedText subtitles, so it needs the CC BY-SA 4.0 credit. **no** = our own lines or no lines. **-** = no Sprite Fright subtitles at all (Ray MV or no video).

## hikari-site#244 (pushed to evidence/w8-cx7 under `cx7/site-244/src/assets/`)

| File | screenshots.md row | Test function | Footage | Commons | Status |
| --- | --- | --- | --- | --- | --- |
| home/projects | #244 tile 1 (Explorer, episode group open) | homeProjects (crop 0,291–1600,960 → 1200×502) | Sprite Fright | yes (row 18 dialogue in grid and video) | taken |
| home/windows | #244 tile 2 (two windows, own videos) | homeWindows (two window grabs composed on a backdrop; waits for the audio to finish loading) | Sprite Fright | yes (dialogue window) | taken (retake 2026-11-16, unit hk-cx7-r1) |
| home/ai-review | #244 tile 3 (Agent thread + Review) | homeAiReview (1920×1080, crop 0,62–1920,865) | Sprite Fright | no (our own lines; stand-in AI) | taken (retake hk-cx7-a7, English catalog): line 3 kept, line 4 undone, line 5 still waiting in Review |
| home/node-effects | #244 tile 4 / 1b Plus "Node effects" tile | homeNodeEffectsOnRayMv (Falling letters, frame 165; crop 0,62–1600,731) | Ray MV (karaoke) | - (credit "Subtitles: Jibrill") | taken |
| home/motion-tracking | #244 tile 5 | homeMotionTracking | Sprite Fright | no (our own sign, no dialogue) | taken |
| home/checking | #244 tile 6 | homeChecking | Sprite Fright | no (our own lines) | taken |
| home/plugins | #244 tile 7 | homePlugins (live index serial 9 served from a read-only copy, HIKARI_SHOT_MARKETPLACE; QT_SCALE_FACTOR 1.3216, crop 0,0,1200,502); Macro chains selected | no video | - | taken (retake 2026-11-16, unit hk-cx7-r1) |
| get-started/typesetting (1600×960) | 1a get-started/typesetting, #244 item 8 | getStartedTour | Sprite Fright | no (our own sign; signs file only) | taken |
| og-image.jpg (1200×630, dark only) | 1b public/og-image.jpg, #244 item 9 | coordinator's crop + credit drawn in | Sprite Fright | yes (credit drawn in) | taken |

## Guide pages and other homepage tiles (in out/cx7/shots, not pushed)

| File | screenshots.md row | Test function | Footage | Commons | Status |
| --- | --- | --- | --- | --- | --- |
| home/hero | 1b src/assets/home/hero | getStartedTour (window) | Sprite Fright | yes | taken |
| home/typesetting (1200×502) | 1b src/assets/home/typesetting | homeTypesetting | Sprite Fright | no (our own sign) | taken |
| home/karaoke (799×225) | 1b src/assets/home/karaoke | karaokeKaraokeModeOnRayMv (audioPanel crop) | Ray MV (karaoke) | - | taken |
| get-started/window | 1a get-started/window | getStartedTour | Sprite Fright | yes | taken |
| basics/grid | 1a basics/grid | getStartedTour (grid, rows 8–21) | Sprite Fright | yes | taken |
| basics/video-panel | 2 (video panel) | getStartedTour (video-panel) | Sprite Fright | yes | taken |
| basics/audio-spectrum | 2 (audio spectrum) | getStartedTour (audio-spectrum) | Sprite Fright | yes | taken |
| basics/line-editor | 1a basics/line-editor | basicsLineEditor | Sprite Fright | yes | taken |
| basics/fullscreen | 1a basics/fullscreen | basicsVideoFullscreen | Sprite Fright | yes | taken |
| basics/hidden-lines | 2 (grid hidden lines) | basicsGridHiddenLines | Sprite Fright | yes | taken |
| checking/comparison-grid | 2 (subtitle comparison) | checkingSubtitleComparison | Sprite Fright | yes | taken |
| customize/themes.png (one image, four themes) | 1a customize/themes | getStartedTour (shootFourThemesTiled) | Sprite Fright | yes | taken |
| translation/translation-mode | 1a translation/translation-mode | translationTranslationMode | Sprite Fright | yes (English originals) + our Thai lines (thai-lines.md) | taken |
| customize/karaoke-layout | 1a customize/karaoke-layout | customizeKaraokeLayoutOnRayMv | Ray MV (karaoke) | - | taken |
| karaoke/karaoke-mode | 1a karaoke/karaoke-mode | karaokeKaraokeModeOnRayMv | Ray MV (karaoke) | - | taken |
| karaoke/lead-in | 2 (split and join, lead-in) | karaokeSplitAndJoinOnRayMv | Ray MV (karaoke) | - | taken |
| karaoke/onsets | 2 (onset snap) | karaokeOnsetSnapOnRayMv | Ray MV (karaoke) | - | taken |
| karaoke/ne-mode | 2 (node effects mode) | homeNodeEffectsOnRayMv | Ray MV (karaoke) | - | taken |
| karaoke/ne-preview (videoPanel crop) | 2 (node effects preview) | homeNodeEffectsOnRayMv | Ray MV (karaoke) | - | taken |
| readme/audio | 1c audio | karaokeKaraokeModeOnRayMv (spectrum on) | Ray MV (karaoke) | - | taken |
| karaoke/furigana-video | 2 (furigana) | karaokeFuriganaOnRayMv | Ray MV (karaoke) | - | taken (hk-cx7-k9b, frame 1824, built on main a9efa7682 with #1180): よ, ぞら and ほし sit above their kanji in light and dark |
| karaoke/templater-result | 1a/2 templater | karaokeTemplaterOnRayMv | Ray MV (karaoke) | - | taken (hk-cx7-k6): Colour sweep, 14 lines generated from our own line (harness fix: wait for canApply before apply) |
| karaoke/agent-karaoke | 2 | - | Ray MV (karaoke) | - | skipped: needs a real agent run; stand-in AI only (Q551) |
| customize/layouts clip | 1a customize/layouts-*.mp4 | - | Sprite Fright + Ray MV (karaoke) | yes | deferred: clip not made; only the Karaoke still exists |
| Menu-dependent rows | see out/cx7/audit.md, menubar/audit-1b.md | - | - | - | deferred: every menu changes |
| Batch A/G re-runs (out/cx7/raw/<binary>/) | 1a "recapture" rows (no video) | existing gated functions, re-run as they are | no video | - | taken, not yet mapped to screenshots.md names |

## Earlier-round files at the top of out/cx7/shots (not site files)

`editing-*`, `sample-editing-*`, `typesetting-*` (Sprite Fright, 1600×960), `home-*` and `home-narrow-*` (Home page, no video, four themes), `og-image-crop.jpg`, `og-image-light.jpg`: drafts from the mockup and og rounds. They're superseded by the rows above and aren't for the site.

## ai-review note

Kept and undone items leave the Review panel, which lists only waiting items. So the change has three lines: line 3 kept (new text in the grid), line 4 undone (original text in the grid and video), line 5 still waiting in Review. The stand-in is the fake sidecar with a new `--replies` file that maps a plain prompt to its scripted turn. The agent runs in Manual mode, because in Auto the judge stopped the edit at Allow. Units hk-cx7-a1…a6; a6 is the shot.

## English numerus catalog (2026-11-16)

Before 04a787e6c, release_screenshots_tests didn't install `i18n/qm/hikarisub_en.qm`, so `%n` strings showed as "(s)". The suite now loads it in initTestCase and keeps it for the whole run. I checked every picture from this harness (OCR of each light raw, then by eye: checking, projects, windows, node effects, karaoke, translation, guide shots). Only home/ai-review showed "(s)": "1 change(s) to review" in Review and in the status bar. It's the only retake (unit hk-cx7-a7), and it now reads "1 change to review". home/plugins (homePlugins, hk-cx7-r1) has no `%n` string. Other suites' shots (batch A/G) come from their own harnesses and aren't in this check.

## Failing existing shot functions

Batch G re-ran each existing gated `hikari_ui_shell_tests` shot function as its own process, as it is: xvfb on akari-rvm, fresh profile. Logs are on the runner under `out/logs/g/<function>.log`. Each one fails before its shots, so the rows it covers stay pending.

| Function | Location | Failing check |
| --- | --- | --- |
| automationPanelScreenshots | tests/ui/shell_tests.cpp:24563 | `QVERIFY(recorder->hasActiveFocus())` returned false |
| dockChromeScreenshots | tests/ui/shell_tests.cpp:32364 | `QCOMPARE(tabTitles(timing), {"Line editor", "Shift times"})`: list sizes differ, actual 1, expected 2 |
| focusScreenshots | tests/ui/shell_tests.cpp:32030 | `QCOMPARE(timing->property("count").toInt(), 2)`: actual 1 |
| ne2Screenshots | tests/ui/shell_tests.cpp:15967 | `QVERIFY(visualItem("node_palette") != nullptr)` returned false |
| noticeScreenshots | tests/ui/shell_tests.cpp:3468 | `QVERIFY2(found, "styleMessages")` returned false |
| pluginPanelScreenshots | tests/ui/shell_tests.cpp:20617 | `(body = item(at("pluginPanelBody_"))) && findItem(body, "pluginWidget_chart")` returned false |
| pu2Screenshots | tests/ui/shell_tests.cpp:16575 | `QVERIFY2(w, …)` returned false, twice: `selectLinesDialog` and `historyWindow` |
| pu7Screenshots | tests/ui/shell_tests.cpp:35237, then :35322 | `QVERIFY(window)` returned false, then `QMetaObject::invokeMethod(manage, "startRename")` returned false |
| surfaceScreenshots | tests/ui/shell_tests.cpp:31937 | `dialogItem("settingsDialog", "appearanceFollowSystem")->property("visualFocus").toBool()` returned false |
| visualInspectorBarScreenshots | tests/ui/shell_tests.cpp:31574 | `QVERIFY(scrub)` returned false |
| popupScreenshotsPu4 | segfault (rc 139) | SIGSEGV, SEGV_MAPERR reading address 0x8. Main thread: `#5 QObject::property(char const*) const` (libQt6Core) ← `#6 ShellTest::popupScreenshotsPu4()` ← QMetaMethodInvoker / QTest::qRun. So `property()` was called on a null object inside the function (no line number; the binary has no debug info). Last log lines before the crash: a FontPicker.qml:400 image provider warning. |

The automation_repeat_tests surface shot also fails after its menu shot (batch A, `out/logs/batch-a-automation_repeat_tests.log`); that one is a menu shot, so it's deferred.
