# CX7 #810 gate 1b: menu bar, Subtitles, Video and Audio

Read-only, at `origin/main` 8dbf88f41. `src/` and `tests/ui/menu_bar.txt` in this worktree match it. The menus are defined in `src/ui/Main.qml:3324-3959` (Subtitles `:3324-3575`, Video `:3576-3876`, Audio `:3877-3959`), with their actions at `Main.qml:2439-2862`, plus `VideoStreamsMenu.qml`, `VideoChaptersMenu.qml` and `RecentFilesMenu.qml`. Default keys come from `kMainDefaults` (`src/application/hotkeys.cpp:843-931`), and shortcut names from `kNames` (`hotkeys.cpp:443-836`, mirrored in `src/ui/command_names.cpp`).

The notation follows CX1 (`docs/ux/context-menus.md:1-16`). `{key}` is the default binding shown in the key column, and "—" means unbound. `&x` is the mnemonic letter. Icon exemptions follow IC1 (`docs/ux/menu-icon-gaps.md:146-150`). "hasEditingTarget" is `root.shell.hasEditingTarget`, and "video" is `root.video.hasVideo`.

## Subtitles

`Main.qml:3324-3575`, `lazyContent`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Shift &times… | GLOBAL_SHOW_SHIFT_TIMES {Ctrl+I} | always; shows the Shift times panel; help "Shifting subtitle times" | shift-times |
| 2 | Timing &postprocessor… | GLOBAL_SHOW_TIMING_POSTPROCESSOR — | always; shows the panel | timing-postprocessor |
| 3 | Timing helpers ▸ | — | always (the rows gate themselves) | timing-assistant |
| 4 | &Blur and glow layers… | GLOBAL_BLUR_AND_GLOW_LAYERS — | hasEditingTarget; also in the Grid row and Line editor text menus | layers |
| 5 | Build layers with the &last values | GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT — | hasEditingTarget; also in the Grid row menu | layers-repeat |
| 6 | T&ypesetting graph… | GLOBAL_TYPESETTING_GRAPH — | hasEditingTarget and the layout isn't locked; also in the Grid row menu | node-editor |
| 7 | &QC notes… | GLOBAL_SHOW_QC_NOTES — | always; shows the QC notes panel | qc-notes |
| 8 | Style &manager | GLOBAL_OPEN_STYLE_MANAGER {Ctrl+M} | hasEditingTarget; help "Is used to manage ASS styles" | styles |
| 9 | Script properties | GLOBAL_OPEN_ASS_PROPERTIES — | hasEditingTarget | script-properties |
| 10 | Font collector | GLOBAL_OPEN_FONT_COLLECTOR — | hasEditingTarget and `assColumns`; help restates the label | font-collector |
| 11 | Resample subtitles | GLOBAL_OPEN_SUBS_RESAMPLE — | hasEditingTarget; opens ResampleWindow (Resample / Cancel); help restates the label | resample |
| 12 | Set FPS ▸ | — | hasEditingTarget; help "Changes the frame rate of the subtitle times" (approved 28) | fps |
| 13 | Check spelling | GLOBAL_OPEN_SPELLCHECKER — | hasEditingTarget; help restates the label | spellchecker |
| 14 | Check karaoke | GLOBAL_CHECK_KARAOKE — | hasEditingTarget; opens a window titled "Karaoke check" | karaoke-check |
| 15 | Kanji timer | GLOBAL_KANJI_TIMER — | hasEditingTarget | kanji-timer |
| | `|` | | `:3565` | |
| 16 | [x] Translation mode | GLOBAL_TRANSLATION_MODE — | hasEditingTarget and (on, or translator mode available); turning it off asks first; help "Translator mode displays and saves both foreign text and translation text" | translation-mode (kept on a check item) |

Items: 16 top level, 34 in all. 2 submenus, 1 top separator.

### Subtitles › Timing helpers ▸

`Main.qml:3343-3463`. Actions at `:2449-2544`; all run `runTimingHelper`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Start to previous keyframe | GLOBAL_SNAP_START_TO_PREVIOUS_KEYFRAME — | video and hasEditingTarget | snap-start-to-previous-keyframe |
| 2 | Start to next keyframe | GLOBAL_SNAP_START_TO_NEXT_KEYFRAME — | same | snap-start-to-next-keyframe |
| 3 | End to previous keyframe | GLOBAL_SNAP_END_TO_PREVIOUS_KEYFRAME — | same | snap-end-to-previous-keyframe |
| 4 | End to next keyframe | GLOBAL_SNAP_END_TO_NEXT_KEYFRAME — | same | snap-end-to-next-keyframe |
| | `|` | | | |
| 5 | Add lead-in | GLOBAL_ADD_LEAD_IN — | same | lead-in |
| 6 | Remove lead-in | GLOBAL_REMOVE_LEAD_IN — | same | lead-in-remove |
| 7 | Add lead-out | GLOBAL_ADD_LEAD_OUT — | same | lead-out |
| 8 | Remove lead-out | GLOBAL_REMOVE_LEAD_OUT — | same | lead-out-remove |
| | `|` | | | |
| 9 | Start one frame earlier | GLOBAL_START_FRAME_EARLIER — | same | time-offset-start |
| 10 | Start one frame later | GLOBAL_START_FRAME_LATER — | same | time-offset-start |
| 11 | End one frame earlier | GLOBAL_END_FRAME_EARLIER — | same | time-offset-end |
| 12 | End one frame later | GLOBAL_END_FRAME_LATER — | same | time-offset-end |
| | `|` | | | |
| 13 | Extend previous line to this start | GLOBAL_EXTEND_PREVIOUS_TO_START — | hasEditingTarget | extend-previous-to-start |
| 14 | Extend next line to this end | GLOBAL_EXTEND_NEXT_TO_END — | hasEditingTarget | extend-next-to-end |
| | `|` | | | |
| 15 | Timing assistant | GLOBAL_TIMING_ASSISTANT — | video and hasEditingTarget; shows Timing postprocessor in assistant mode (`:1250-1255`) | timing-assistant |
| 16 | Accept assistant suggestion | GLOBAL_TIMING_ASSISTANT_ACCEPT — | same | commit |

Items: 16, 4 separators.

### Subtitles › Set FPS ▸

`Main.qml:3522-3543`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Set new FPS… | GRID_SET_NEW_FPS — | parent's gate | fps |
| 2 | Set FPS from video | GRID_SET_FPS_FROM_VIDEO — | video | fps-from-video |

Items: 2.

## Video

`Main.qml:3576-3876`, `lazyContent`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Open &video… | GLOBAL_OPEN_VIDEO {Ctrl+Shift+O} | always; inline Action (`:3585`); also in File (approved 28) and the video menus | open-video |
| 2 | Recently opened videos ▸ | — | always (`RecentFilesMenu`) | recent-video |
| 3 | Open keyframes | GLOBAL_OPEN_KEYFRAMES — | video; opens a file picker; help restates the label | open-keyframes |
| 4 | Recently opened keyframes ▸ | — | video | recent-keyframes |
| 5 | Open timecodes | GLOBAL_OPEN_TIMECODES — | always; opens a file picker | open-timecodes |
| 6 | Recently opened timecodes ▸ | — | always | recent-timecodes |
| 7 | Save timecodes | GLOBAL_SAVE_TIMECODES — | video, or a timecodes file is open; opens a save picker | save-timecodes |
| 8 | Close timecodes | GLOBAL_CLOSE_TIMECODES — | a timecodes file is open | close-timecodes |
| 9 | Open dummy video | GLOBAL_OPEN_DUMMY_VIDEO — | always; opens DummyVideoDialog (Open / Cancel); no status help; id 5103 has no shortcut name | dummy-video |
| 10 | Centre times within frames | GLOBAL_CENTER_TIMES_IN_FRAMES — | video and hasEditingTarget | center-times |
| 11 | Align to video… | GLOBAL_ALIGN_TO_VIDEO — | same | align-to-video |
| 12 | Track colour… | GLOBAL_TRACK_COLOUR — | same | track-colour |
| 13 | Find fade… | GLOBAL_FIND_FADE — | same | find-fade |
| 14 | Insert start time from video | GLOBAL_SET_START_TIME {Ctrl+Left} | same; no status help | set-start-time |
| 15 | Insert end time from video | GLOBAL_SET_END_TIME {Ctrl+Right} | same; no status help | set-end-time |
| 16 | Shift by frames to video ▸ | — | video and hasEditingTarget | shift-times |
| 17 | Tracking ▸ | — | hasEditingTarget | motion-apply |
| 18 | Previous frame | GLOBAL_PREVIOUS_FRAME {Left} | video | frame-previous |
| 19 | Next frame | GLOBAL_NEXT_FRAME {Right} | video | frame-next |
| 20 | Go to start time of line | GLOBAL_SET_VIDEO_AT_START_TIME — | video | video-to-start-time |
| 21 | Go to end time of line | GLOBAL_SET_VIDEO_AT_END_TIME — | video | video-to-end-time |
| 22 | Play or pause | GLOBAL_PLAY_PAUSE {Alt+Space} | video; the icon flips with `playing` | media-play / media-pause |
| 23 | Go to previous keyframe | GLOBAL_GO_TO_PREVIOUS_KEYFRAME — | video | keyframe-previous |
| 24 | Go to next keyframe | GLOBAL_GO_TO_NEXT_KEYFRAME — | video | keyframe-next |
| | `|` | | `:3829`, the menu's only separator | |
| 25 | Unload video | — (runs VIDEO_DELETE_FILE through `hotkeyGesture` only) | `video.loaded`; no key column | close-video |
| 26 | Reload video | GLOBAL_RELOAD_VIDEO — | video, and not a dummy video | reload-video |
| 27 | Streams ▸ | — | video and `streamCount > 0` | video-streams |
| 28 | Chapters ▸ | — | video and `chapterCount > 0` | chapters |
| 29 | Video details… | GLOBAL_VIDEO_DETAILS — | video; opens a read-only window with a Close button | video-details |
| 30 | Zoom video | GLOBAL_VIDEO_ZOOM — | video; toggles `videoView.zoomMode` with no check mark | zoom |
| 31 | Reset zoom and pan | GLOBAL_RESET_VIDEO_ZOOM — | `videoView.zoomed` (approved 28) | zoom-reset |
| 32 | [x] Render cost | GLOBAL_VIDEO_RENDER_COST — | video (approved 28) | render-cost |

Items: 32 top level, 48 in all. 7 submenus, 1 top separator.

### Video › Recently opened videos / keyframes / timecodes ▸

`RecentFilesMenu.qml:6-33`, used at `Main.qml:3590, 3605, 3625`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | {dyn} recent file | — | tooltip `qsTr("Open") + " " + path` (`RecentFilesMenu.qml:19`) | — (IC1: named by data) |
| 2 | None | — | disabled; shown only when the list is empty | — (IC1) |

Items: 1 each ({dyn}).

### Video › Shift by frames to video ▸

`Main.qml:3693-3726`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Selected lines, start at video | GLOBAL_SHIFT_FRAMES_TO_VIDEO_START — | video and hasEditingTarget | shift-start-to-video |
| 2 | Selected lines, end at video | GLOBAL_SHIFT_FRAMES_TO_VIDEO_END — | same | shift-end-to-video |
| 3 | Following lines, start at video | GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_START — | same | shift-start-to-video |
| 4 | Following lines, end at video | GLOBAL_SHIFT_FOLLOWING_FRAMES_TO_VIDEO_END — | same | shift-end-to-video |

Items: 4.

### Video › Tracking ▸

`Main.qml:3727-3788`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Apply motion data… | GLOBAL_APPLY_MOTION_DATA — | video and hasEditingTarget; help switches to "Open a video first: tracking data is per frame." without video | motion-apply |
| 2 | Perspective tracking… | GLOBAL_PERSPECTIVE_TRACKING — | same | tool-perspective |
| 3 | Revert motion data | GLOBAL_REVERT_MOTION_DATA — | hasEditingTarget | motion-revert |
| | `|` | | | |
| 4 | Copy motion data from lines | GLOBAL_COPY_MOTION_DATA — | video and hasEditingTarget; same help switch | motion-copy |
| 5 | Copy Power-Pin track from lines | GLOBAL_COPY_POWER_PIN_TRACK — | same | perspective-plane |
| | `|` | | | |
| 6 | Extend tracked lines… | GLOBAL_EXTEND_TRACKED_LINES — | same | motion-extend |
| 7 | Extrapolate `\move` to the line | GLOBAL_EXTRAPOLATE_MOVE — | hasEditingTarget | move-extend |

Items: 7, 2 separators.

### Video › Streams ▸ and Chapters ▸

`VideoStreamsMenu.qml:6-31`, `VideoChaptersMenu.qml:6-29`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| S1 | {dyn} [x] audio track | — | checkable `ShellMenuItem`, not `MenuRadioItem`; closes the menu | — (data) |
| C1 | {dyn} [x] chapter "label  [time]" | — | `label + "  " + time` glued into the text; checked = current | — (data) |

Items: 1 each. Gate 1 already covers both (rule 23, B2 and B14), so they aren't repeated below.

## Audio

`Main.qml:3877-3959`, `lazyContent`.

| # | Item | Command | Enabled when / notes | Icon |
|---|---|---|---|---|
| 1 | Open audio | GLOBAL_OPEN_AUDIO — | always; opens a file picker; help "Opens audio file" | open-audio |
| 2 | Recently opened audio ▸ | — | always; a hand copy of `RecentFilesMenu`; help restates the title | recent-audio |
| 2.1 | {dyn} recent file | — | status help `qsTr("Open") + " " + path`; reveals an already open file first (`revealRecent`) | — (IC1) |
| 2.2 | None | — | disabled; shown only when the list is empty | — (IC1) |
| 3 | Open audio from video | GLOBAL_AUDIO_FROM_VIDEO — | video | audio-from-video |
| 4 | Close audio | GLOBAL_CLOSE_AUDIO — | `audio.hasAudio` | close-audio |
| 5 | Reload audio | GLOBAL_RELOAD_AUDIO — | `audio.reloadable` | reload-audio |
| | `|` | | `:3938` | |
| 6 | Set audio position to video time | GLOBAL_SET_AUDIO_FROM_VIDEO — | hasAudio and hasEditingTarget | audio-to-video-time |
| 7 | Set audio marker to video time | GLOBAL_SET_AUDIO_MARK_FROM_VIDEO — | same | audio-marker-to-video-time |
| 8 | Join with previous line using the audio selection | GLOBAL_JOIN_PREVIOUS_AUDIO_SELECTION — | `audio.ready` and hasEditingTarget | join-lines |

Items: 8 top level, 9 in all. 1 submenu, 1 separator.

**Totals:** 3 menus and 10 submenus; 56 top-level rows and 91 items in all (34 + 48 + 9).

## Changed since 1d122c203

The changes come from `git log -S` on each `menu_bar.txt` line, each mapped to the first-parent merge that brought it to main.

| Change | Row | Commit | PR |
|---|---|---|---|
| removed | Subtitles › Shift times / run timing postprocessor | 12d142ae8 | #619 (#611) |
| renamed, added | Subtitles › "Shift times..." → "Shift times…"; Timing postprocessor… added | bc48cbc63 | #674 (#598) |
| added | Video › Shift by frames to video ▸ (4); Audio › Join with previous line using the audio selection | 91dc3c80a | #625 (FA17 #574) |
| added | Video › Centre times within frames | 26f206a2c | #672 (FA16 #573) |
| added | Subtitles › Set FPS ▸ (from the Grid menu, approved 28) | 57027a885 | #680 (CX4 #580) |
| renamed | Video › "Turn off video zoom" → "Reset zoom and pan" | eb270f56a | #686 (FA12 #569) |
| renamed | Subtitles › "ASS file properties" → "Script properties" | a8f80546b | #709 (#596) |
| renamed | Subtitles › "Kanji timer…" → "Kanji timer" | b751fba35 | #713 (#596 a3) |
| renamed | Video › "Show / hide render cost" → [x] "Render cost" | 1daae5047 | #715 (CX5 #581) |
| removed | Subtitles › Conversion ▸ (Convert to ASS, SRT, MDVD, MPL2, TMP) | 1c76e04e3 | #762 (#611, pin train) |
| added | Video › Open timecodes, Recently opened timecodes ▸, Save timecodes, Close timecodes | a3e3af519 | #762 (FA21 #648) |
| added | Video › Align to video… | 9e89e724b | #762 (FA14 #571) |
| added | Video › Reload video; Audio › Reload audio | 6109dc837 | #762 (FA9 #566) |
| added | Video › Video details… | 41cabce2f | #762 (FA19 #576) |
| added | Subtitles › QC notes… | 75e1cd7a2 | #798 (#577, pin train 2) |
| added | Video › Tracking › Perspective tracking… | d38b1ce50 | #798 (FA13 #570) |
| added | Video › Tracking ▸ (the other 6 rows, 2 separators) | 72f2e0b56 | #868 (AU8 #842) |
| renamed | Video › "Play / Pause" → "Play or pause" | 345b9fc24 | #926 (#596 b) |
| added | Subtitles › Blur and glow layers…, Build layers with the last values | 914dea9e4 | #943 (AU18 #852) |
| added | Video › Track colour…, Find fade… | bc7f602e8 | #956 (AU11) |
| added | Subtitles › Timing helpers ▸ (16 rows, 4 separators) | 710e1e350 | #968 (AU19 #853) |
| added | Subtitles › Typesetting graph… | 34a2af941 | #1025 (AU9 #844) |

No row moved between these three menus. In all, Subtitles went from 11 to 34 items, Video from 22 to 48 and Audio from 7 to 9.

## Cross-reference with the right-click and ⋯ menus

| Command | Menu bar | Context menu (`tests/ui/context_menus.txt`) | Verdict |
|---|---|---|---|
| GLOBAL_BLUR_AND_GLOW_LAYERS | Subtitles › Blur and glow layers… | Grid `:72`, Line editor text `:34`, same label | duplicate, the same name; fine |
| GLOBAL_BLUR_AND_GLOW_LAYERS_REPEAT, GLOBAL_TYPESETTING_GRAPH | Subtitles | Grid `:73-74`, same labels | duplicate, the same name (gate 1 #1 moves them under Typesetting ▸ in the Grid) |
| GLOBAL_OPEN_VIDEO | Video › Open video… | Video `:200`, Full-screen `:235` | the same |
| GLOBAL_RESET_VIDEO_ZOOM, GLOBAL_VIDEO_RENDER_COST | Video | Video `:218, 224`, Full-screen `:253, 259` | the same, and [x] in both (approved 28) |
| VIDEO_DELETE_FILE | Video › Unload video, with no command | Video `:226`, Full-screen `:261`, with the command | **SV4** |
| Play | GLOBAL_PLAY_PAUSE "Play or pause" {Alt+Space} | VIDEO_PLAY_PAUSE "Play"/"Pause" {Space} (`:194`) | two commands with two scopes. The names follow text-style ("Play or pause" for the command, Play and Pause for the states); no change |
| Audio tracks, chapters | Streams ▸, Chapters ▸ with [x] rows | Audio track ▸, Chapters ▸ with (•) rows | gate 1 rule 23 (B2, B14) |
| VIDEO_FULL_SCREEN, VIDEO_ASPECT_RATIO, Export frame ▸, VIDEO_STOP | — | Video `:196, 223`, Full-screen | **SV2** |
| AUDIO_GOTO_START / _END icons | Video › Go to start/end time of line: video-to-start-time / -end-time | Audio » rows 19–20 use the same icons | **SV14** |

## Findings

### SV1. flat-list: Video has 32 top-level rows and a single separator

- **Tag:** flat-list, destructive-next-to-common.
- **Evidence:**
  - `Main.qml:3581-3874`: the only `MenuSeparator` is at `:3829`, and `menu_bar.txt:110-158` has one `Video > -`. That leaves 24 rows above it in one run, 48 items in all.
  - File commands (rows 1–9), line timing from video (10–17), playback (18–24) and view state (27–32) run together.
  - Play or pause (row 22) sits between Go to end time of line and Go to previous keyframe.
  - Open dummy video (9) sits 8 rows below Open video… (1), and Reload video (26) sits 25 rows below it.
- **Is it too long?** Yes as one run. At 32 rows (34 with SV2) it is still workable once grouped, because every group is one task. Aegisub splits the same commands over a Video menu and a Timing menu. A new top-level Timing menu would be the bigger change; recommend only if the owner wants it (uncertain value).
- **Recommend:** keep the commands, in 6 groups:
  1. Open video…, Recently opened videos ▸, Open dummy video…, Reload video, Unload video
  2. Open keyframes…, Recently opened keyframes ▸, Open timecodes…, Recently opened timecodes ▸, Save timecodes…, Close timecodes
  3. Play or pause, Previous frame, Next frame, Go to previous keyframe, Go to next keyframe, Go to start time of line, Go to end time of line
  4. Set start time from video, Set end time from video (SV10), Shift by frames to video ▸, Centre times within frames
  5. Align to video…, Track colour…, Find fade…, Tracking ▸
  6. [x] Full screen, Change aspect ratio… (SV2), Streams ▸ (Audio track ▸ by rule 23), Chapters ▸, [x] Zoom video (SV3), Reset zoom and pan, [x] Render cost, Video details (SV6)

  That makes 5 separators and 34 rows, with no new submenu. Each close row ends its group (SV5). Regenerate `menu_bar.txt` and the docs export.
- **Visible:** yes.

### SV2. missing-expected: the Video menu has no Full screen and no Change aspect ratio

- **Tag:** missing-expected, duplicate-of-context-menu (reverse: a row that exists only in the right-click menu).
- **Evidence:**
  - VIDEO_FULL_SCREEN and VIDEO_ASPECT_RATIO are only in the video menus (`context_menus.txt:196, 223, 231, 258`), and are handled in `Main.qml:10886, 10888`.
  - `menu_bar.txt` has neither, and neither does any other menu-bar menu.
  - Change aspect ratio is reachable only by a right-click on the picture or a shortcut. Full screen is reachable through the transport button too.
  - Export frame ▸ (6 rows) and Stop are also right-click only. They are lower priority, because they act on the picture under the pointer.
- **Recommend:** add `[x] Full screen` (VIDEO_FULL_SCREEN, checked = `videoFullscreen.active`, icon fullscreen) and `Change aspect ratio…` (VIDEO_ASPECT_RATIO, aspect-ratio; "…" per B11) to Video's view group (SV1 group 6). Leave Export frame ▸ and Stop in the video menus.
- **Visible:** yes.

### SV3. toggle-hides-state: Zoom video is a toggle drawn as a plain command

- **Tag:** toggle-hides-state.
- **Evidence:**
  - `Main.qml:2796-2800`: `videoView.toggleZoom()` flips `zoomMode` (`src/ui/video_view_controller.cpp:146-154`), which shows the zoom frame (`VideoZoomFrame.qml:8`).
  - The row is not checkable, so the menu doesn't say whether zoom mode is on. Render cost, two rows below it, is a check item.
- **Recommend:** `[x] Zoom video`, `checkable: true`, `checked: root.videoView.zoomMode` (the property exists, `video_view_controller.h:28`), keeping the zoom icon as Render cost keeps its own.
- **Visible:** yes.

### SV4. keys: the menu bar's Unload video has no command, so it shows no key and is missing from the listing

- **Tag:** name-differs-from-context-menu (key column), missing-expected.
- **Evidence:**
  - `Main.qml:3830-3836` has no `command:`, and only calls `hotkeyGesture("VIDEO_DELETE_FILE", 3)`.
  - `menu_bar.txt:150` reads `Video > Unload video` with no `[COMMAND]`.
  - The same row in the video menus carries `[VIDEO_DELETE_FILE]` (`context_menus.txt:226, 261`) and shows the bound key.
- **Recommend:** add `command: "VIDEO_DELETE_FILE"` to the menu-bar row, and regenerate `menu_bar.txt`.
- **Visible:** no by default, because VIDEO_DELETE_FILE has no default key. It shows once a user binds one.

### SV5. destructive-next-to-common: the close rows sit inside their groups, not at the end

- **Tag:** destructive-next-to-common.
- **Evidence:**
  - Video: Close timecodes (`:3640`) is followed by Open dummy video. Unload video (`:3830`) opens the last group, ahead of Reload video.
  - Audio: Close audio (`:3925`) comes before Reload audio (`:3932`).
- **Recommend:**
  - Video: see SV1. Unload video ends group 1, and Close timecodes ends group 2.
  - Audio: Open audio…, Recently opened audio ▸, Open audio from video, Reload audio, Close audio | Set audio position to video time, Set audio mark to video time (SV9), and the join row (SV19).
- **Visible:** yes.

### SV6. text-style "…": six rows break the ellipsis rule

- **Tag:** text-style.
- **Evidence:** `docs/ux/text-style.md:53-57` gives "…" to rows that open a picker or a dialog that asks for values, and none to a window you work in or look at. `docs/ux/text-inventory.md:42` already lists "Open audio…" and "Open keyframes…".
  - Missing "…":
    - Open audio (`Main.qml:2818`, file picker).
    - Open keyframes (`:2622`, file picker).
    - Open timecodes (`:2628`, file picker).
    - Save timecodes (`:2633`, save picker).
    - Open dummy video (`:2645`; DummyVideoDialog asks for values, with Open / Cancel at `DummyVideoDialog.qml:206-213`).
    - Resample subtitles (`:2582`; ResampleWindow asks for values, with Resample / Cancel at `ResampleWindow.qml:232-239`).
  - "…" that shouldn't be there:
    - Video details… (`:2793`): a read-only window with only Close (`VideoDetailsDialog.qml:122`).
    - QC notes… (`:2559`): shows a panel you work in, with no Apply. Shift times… and Timing postprocessor… keep theirs, because their panels have Apply.
  - **Uncertain:** Typesetting graph… (`:2553`) builds the graph at once and then shows Node effects. It is a borderline case, and gate 1 #1 kept the "…" in the Grid, so leave it unless the owner decides otherwise.
- **Recommend:** "Open audio…", "Open keyframes…", "Open timecodes…", "Save timecodes…", "Open dummy video…", "Resample subtitles…", "Video details", "QC notes". Give the shortcut names the same change (`hotkeys.cpp:733` "Video details…"), and use TR1.
- **Visible:** yes.

### SV7. inconsistent-name: shortcut names differ from the Subtitles rows

- **Tag:** inconsistent-name (one name per thing, `text-style.md:117-119`; CX2 1.3 precedent).
- **Evidence:** each pair is shortcut name (`hotkeys.cpp`) → menu row.
  - "Time shift window" (`:567`) → "Shift times…". text-style's table says "Shift times (the panel and command)", and it is a panel, not a window.
  - "Timing postprocessor panel" (`:713`) → "Timing postprocessor…".
  - "QC notes panel" (`:736`) → "QC notes…" ("QC notes" after SV6).
  - "Set new FPS" (`:551`) → "Set new FPS…".
  - Timing helpers: "Snap start to the previous keyframe" (`:782`) → "Start to previous keyframe", and the same for the other three snaps. "Move start one frame earlier" (`:790`) → "Start one frame earlier", and the same for the other three. "Extend the previous line to this start" (`:794`) → "Extend previous line to this start". "Accept the timing assistant’s suggestion" (`:797`) → "Accept assistant suggestion".
  - The Timing helpers rows also break text-style's verb-first command rule (`text-style.md:96`). The CX4 "drop the parent's word" rule doesn't apply, because the parent "Timing helpers" has no verb to drop.
- **Recommend:**
  - Timing helpers rows take the verb: "Snap start to the previous keyframe", "Snap start to the next keyframe", "Snap end to the previous keyframe", "Snap end to the next keyframe", "Move start one frame earlier", "Move start one frame later", "Move end one frame earlier", "Move end one frame later", "Extend the previous line to this start", "Extend the next line to this end", "Accept the timing assistant’s suggestion". The lead rows stay as they are; their shortcut names add "to/from the selected lines", which the menu context makes clear.
  - Shortcut names: "Shift times…", "Timing postprocessor…", "QC notes", "Set new FPS…".
- **Visible:** yes.

### SV8. inconsistent-name: "Unload video" next to "Close audio" and "Close timecodes"

- **Tag:** inconsistent-name.
- **Evidence:** `Main.qml:3833` "Unload video"; `:2834` "Close audio"; `:2639` "Close timecodes". All three take a file out of the editor and leave it on disk. text-style's Delete/Remove row (`text-style.md:145`) reserves the other verbs.
- **Recommend:** "Close video" in the menu bar, both video menus and the shortcut name (`hotkeys.cpp:644`). CX2 approved menu 10 drew "Unload video", so this needs the owner's pick. **Uncertain** whether the owner wants the legacy word kept.
- **Visible:** yes.

### SV9. inconsistent-name: "audio marker" in the menu bar, "the mark" in the audio overflow

- **Tag:** inconsistent-name, name-differs-from-context-menu.
- **Evidence:** Audio › "Set audio marker to video time" (`Main.qml:2852`, shortcut `hotkeys.cpp:633`). The audio » menu, Play around ▾ and Customise say "Play before the mark" / "Play after the mark", and gate-1 B1 picks "mark" for the shortcuts as well.
- **Recommend:** "Set audio mark to video time", in the menu and the shortcut name, in the same PR as B1.
- **Visible:** yes.

### SV10. legacy-name: "Insert start time from video" sets a time, and inserts nothing

- **Tag:** legacy-name, inconsistent-name.
- **Evidence:**
  - `Main.qml:2674, 2680` call `app.setTimeFromVideo(false/true)`, which sets the selected lines' start or end time. The command ids are GLOBAL_SET_START_TIME and GLOBAL_SET_END_TIME.
  - In the Grid, "Insert ▸" means a new line (`Main.qml:7440`). The Audio menu's sibling says "Set audio position to video time".
  - The wording is a literal carry-over of legacy's Polish "Wstaw czas…" (**uncertain**; I didn't check the legacy tree).
- **Recommend:** "Set start time from video" and "Set end time from video", in the menu and the shortcut names (`hotkeys.cpp:634-635`), and add status help ("Sets the selected lines’ start to the video frame’s time").
- **Visible:** yes.

### SV11. flat-list: Subtitles has 16 rows and one separator, with QC notes among the typesetting rows

- **Tag:** flat-list.
- **Evidence:** `Main.qml:3329-3573`: the only separator is above Translation mode (`:3565`). QC notes… (`:3487`) sits between Typesetting graph… and Style manager. The check tools (Check spelling, Check karaoke) sit 6 rows below it.
- **Recommend:** 5 groups, 16 rows:
  1. Shift times…, Timing postprocessor…, Timing helpers ▸
  2. Blur and glow layers…, Build layers with the last values, Typesetting graph…
  3. Style manager, Script properties, Font collector, Resample subtitles…, Set FPS ▸ (keeps approved 28's order)
  4. Check spelling, Check karaoke, Kanji timer, QC notes
  5. [x] Translation mode
- **Visible:** yes.

### SV12. inconsistent-name: Check karaoke opens a window called "Karaoke check"

- **Tag:** inconsistent-name.
- **Evidence:** `Main.qml:2594` "Check karaoke", but `KaraokeCheckDialog.qml:11` has the title "Karaoke check". text-style (`:100`) says a window opened by a command takes that command's name. Every other window in these menus already matches: Script properties, Font collector, Kanji timer, Resample subtitles, Video details, Align to video, Apply motion data, Perspective tracking, Extend tracked lines, Set new FPS.
- **Recommend:** window title "Check karaoke".
- **Visible:** yes.

### SV13. text-style: the status-bar help in these menus breaks the help rule, and some of it can never show

- **Tag:** text-style, inconsistent-name.
- **Evidence:** the rule is "one clause in the third person, starting with the verb, no full stop" (`text-style.md:103`).
  - Not a verb clause, or the label repeated:
    - "Shifting subtitle times" (`Main.qml:3331`)
    - "Is used to manage ASS styles" (`:3496`)
    - "Font collector" (`:3510`)
    - "Resample subtitles" (`:3517`)
    - "Check spelling" (`:3546`)
    - "Open keyframes" (`:3601`)
    - "Recently opened audio" (`:3891`)
  - Imperative instead of third person:
    - "Open a VFR timecodes v1, v2 or v4 file" (`:3621`)
    - "Save a VFR timecodes v2 file" (`:3636`)
    - "Close the currently open timecodes file" (`:3643`)
    - "Go to previous frame" / "Go to next frame" (`:3791, 3797`)
  - Missing articles: "Opens video file", "Opens audio file", "Closes audio" (`:3584, 3885, 3927`).
  - Wrong name: "Translator mode displays and saves both foreign text and translation text" (`:3569`). The mode is "Translation mode", and the other text is the "Original text".
  - Help that never shows: the Tracking rows' "Open a video first: tracking data is per frame." (`:3735, 3743, 3759, 3767, 3776`) is chosen only when there is no video, but those rows are disabled without video, and a disabled item gets no hover. Gate 1 #1 already raises "disabled rows hide their reason"; this is the menu-bar case of it.
- **Recommend:**
  - "Shows the Shift times panel", "Opens Style manager for the active line’s style", "Collects the fonts the document uses into a folder or archive", "Resamples the document to a new script resolution", "Opens the Spellchecker panel", "Opens a keyframes file", "Lists the audio files opened lately", "Opens a VFR timecodes v1, v2 or v4 file", "Saves the timecodes as a VFR v2 file", "Closes the open timecodes file", "Goes one frame back", "Goes one frame forward", "Opens a video file", "Opens an audio file", "Closes the audio".
  - "Shows and saves the original text and the translation of each line".
  - Drop the dead Tracking branch, or show the reason the way gate 1 #1 decides.
- **Visible:** yes (status bar).

### SV14. icons: one icon, two meanings

- **Tag:** no-icon (wrong icon); gate 1 rule 18.
- **Evidence:**
  - Timing helpers ▸ (`Main.qml:3346`) uses `timing-assistant`, the icon of its own Timing assistant row (`:3452`).
  - Shift by frames to video ▸ (`:3694`) uses `shift-times`, the icon of Shift times… (`:3332`).
  - Tracking ▸ (`:3728`) uses `motion-apply`, the icon of its own Apply motion data… (`:3736`).
  - Start one frame earlier and later share `time-offset-start` (`:3408, 3415`), and End one frame earlier and later share `time-offset-end` (`:3422, 3429`).
  - Video › Go to start/end time of line (`:3804, 3810`) and the audio » Go to selection start/end (`:6742, 6749`) share `video-to-start-time` / `video-to-end-time`. One moves the video and the other scrolls the audio view.
  - Set FPS ▸ = Set new FPS… (`fps`) is excluded, because approved 28 drew it that way.
- **Recommend:**
  - New roles `timing-helpers`, `shift-frames-to-video` and `tracking` for the three submenus.
  - Split the nudge pairs into `start-frame-earlier` / `start-frame-later` / `end-frame-earlier` / `end-frame-later`.
  - New `audio-to-selection-start` / `audio-to-selection-end` for the audio rows.
  - These go through the icon pipeline with gate 1's PR 5.
- **Visible:** yes.

### SV15. missing-expected: Open dummy video can't be found or bound in Settings › Shortcuts

- **Tag:** missing-expected.
- **Evidence:**
  - GLOBAL_OPEN_DUMMY_VIDEO is id 5103 in `kActions` (`hotkeys.cpp:213`), but there is no `{5103, …}` row in `kNames`. Only 5104 is retired (`:1124-1131`).
  - `HotkeyList::build` lists only named ids (`hotkeys.cpp:1482-1518`), and `docs_export.cpp:80-82` skips unnamed ids.
  - So the row's key column stays empty, and the command is absent from Settings › Shortcuts and the docs.
  - **Uncertain:** whether Shift+click mapping still binds it through `gestureTarget`; I didn't trace that. This is the Video counterpart of FE7.
- **Recommend:** add `{5103, "Open dummy video…"}` to `kNames` and `command_names.cpp`.
- **Visible:** yes (Settings › Shortcuts and the docs export).

### SV16. inconsistent-name: Tracking ▸ names the Power-Pin pair two ways, and Revert sits in the apply group

- **Tag:** inconsistent-name, destructive-next-to-common.
- **Evidence:**
  - "Apply motion data…" pairs with "Copy motion data from lines" (`Main.qml:2710, 2728`).
  - The Power-Pin pair is "Perspective tracking…" and "Copy Power-Pin track from lines" (`:2716, 2734`). Its window calls the data "Power-Pin data" (`PerspectiveTrackingWindow.qml:66`).
  - Revert motion data (`:3748`), which puts the source lines back, sits in the apply group.
- **Recommend:** Apply motion data…, Apply Power-Pin data… | Copy motion data from lines, Copy Power-Pin data from lines | Extend tracked lines…, Extrapolate `\move` to the line | Revert motion data. The window titles and shortcut names follow. **Uncertain:** the owner may prefer "Perspective tracking" as the feature name; if so, use it in all four places instead.
- **Visible:** yes.

### SV17. inconsistent-name: Previous frame and Next frame aren't verb-first, while their neighbours are

- **Tag:** inconsistent-name (text-style command form, `text-style.md:96`).
- **Evidence:** "Previous frame", "Next frame" (`Main.qml:2752, 2757`) sit next to "Go to previous keyframe", "Go to next keyframe" and "Go to start time of line". Their own help already says "Go to previous frame".
- **Recommend:** "Go to previous frame" and "Go to next frame", with the same shortcut names (`hotkeys.cpp:609, 617`). Low priority.
- **Visible:** yes.

### SV18. code: Recently opened audio is a hand copy of RecentFilesMenu, and both build their text from pieces

- **Tag:** inconsistent-name (the path shows in different places), text-style (`text-style.md:115`).
- **Evidence:**
  - `Main.qml:3890-3917` repeats `RecentFilesMenu.qml`, but puts the path in the status-bar help (`:3904`) where the other three put it in a tooltip (`RecentFilesMenu.qml:19-20`). It also adds `revealRecent`, which the other three lack.
  - Both build `qsTr("Open") + " " + path`. FE12 raises the same thing for File's list.
- **Recommend:** use `RecentFilesMenu` for audio, with an `onChosen` that calls `revealRecent` first, and build one string, `qsTr("Open %1").arg(path)`, in the component, in the same PR as FE12.
- **Visible:** yes, but minor (the tooltip moves from the status bar).

### SV19. inconsistent-name (uncertain): "Join with previous line using the audio selection" doesn't join

- **Tag:** inconsistent-name.
- **Evidence:**
  - Its help (`Main.qml:3953`) says it gives the active line the audio selection and ends the line before it where the selection starts. No lines merge.
  - The Grid's Join ▸ merges lines (`Main.qml:7536`), and the row shares Join's `join-lines` icon (`:3954` vs `:7535-7537`).
- **Recommend:** "Apply the audio selection and end the previous line there", with the existing `extend-previous-to-start` icon, which shows the same effect on the line before. **Uncertain:** the wording is long, and the owner may prefer something shorter.
- **Visible:** yes.

### SV20. missing-expected: mnemonics are on 7 of 16 Subtitles rows, 1 of 32 Video rows and no Audio row

- **Tag:** missing-expected (keyboard).
- **Evidence:**
  - Subtitles: &t, &p, &b, &l, &y, &q, &m (`Main.qml:2441-2564, 3476`); Timing helpers, Script properties, Font collector, Resample, Set FPS, Check spelling, Check karaoke, Kanji timer and Translation mode have none.
  - Video: only "Open &video…" (`:3586`).
  - Audio: none.
  - There are no duplicate letters in these menus, unlike FE10.
- **Recommend:**
  - Subtitles: Shift &times…, Timing &postprocessor…, Timing &helpers ▸, &Blur and glow layers…, Build layers with the &last values, T&ypesetting graph…, Style &manager, Sc&ript properties, &Font collector, R&esample subtitles…, Set FP&S ▸, Check spellin&g, Check &karaoke, Kanji t&imer, &QC notes, Tra&nslation mode.
  - Video and Audio: give letters to the first row of each group (SV1, SV5). For example: &Open video…, Open &keyframes…, &Play or pause, Set &start time from video, Align to &video…, &Full screen; and Audio: &Open audio…, Open audio from &video, &Reload audio, &Close audio.
  - **Uncertain:** how much keyboard menu use the owner expects on Linux, where underlines show only with Alt.
- **Visible:** yes (underlines).

## Checked and clean

- **Check items:** Translation mode and Render cost are check items. Streams and Chapters are check items too (gate 1 rule 23 makes them radios). No other toggle in these menus apart from SV3.
- **Icons:** every non-exempt row and submenu has one. The exempt rows are the {dyn} recents, "None", and the stream and chapter rows.
- **"…" on window names:** Style manager, Script properties, Font collector and Kanji timer correctly have none (text-style decision 5). Align to video…, Track colour…, Find fade…, Apply motion data…, Perspective tracking… and Extend tracked lines… open windows that ask for input, so they correctly keep theirs.
- **British English and sentence case:** "Centre times within frames" and "Track colour…" are British. No title-case or "The …" label. "Power-Pin", "FPS" and `\move` are proper names or tags.
- **Shift by frames to video ▸:** its rows drop the parent's verb, per the CX4 rule.
- **Approved 28 rows:** Set FPS ▸ (fps / fps-from-video), Reset zoom and pan and [x] Render cost are as drawn.
