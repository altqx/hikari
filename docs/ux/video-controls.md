# Video controls

The transport row under the video, and the commands around it ([#1206](https://github.com/altqx/hikari/issues/1206)). It is drawn in [Quiet](visual-language.md#video-transport-and-full-screen), and full screen's strip carries the same row.

The owner chose the row from a board on 2026-11-17 ("yes to all lgtm", Q611–Q617 on tracking [#1209](https://github.com/altqx/hikari/issues/1209)). The rows the Aegisub parity audit added to #1206 (Q585) are part of it. Earlier decisions also apply:

- Previous/next file leave the row (Q569). They stay in the Video menu, the video's context menu and on `,` and `.` (`VIDEO_PREVIOUS_FILE`, `VIDEO_NEXT_FILE`).
- Nothing is added that the owner didn't pick (Q570).

The row, its commands and full screen's copy of it are built in `VideoTimeField.qml`, `VideoZoomReadout.qml` and the Video panel in `Main.qml`. Previous and next file have rows in the Video menu and the video's context menu.

## The row

One row, 34 high, under the seek bar. Left to right:

1. Previous frame · Play/Pause · Next frame, then a separator.
2. Play line · Stop.
3. **The time**: the 20-unit monospace timecode.
4. The frame, in `muted` monospace ("frame 5146 / 15116").
5. At the trailing end: **the zoom readout**, then volume and its slider.

The buttons are 24 × 24 tool buttons, icon only, with their names on hover. The row stays minimal (Q611): previous/next line, loop the line, playback speed and aspect ratio aren't added.

The log-like readout line above today's row goes ("00:01:02,750; 1506; 0; 0 ms, -3580 ms", `VideoController::times`). Its parts move:

| Today's part | Where it goes |
| --- | --- |
| Time, frame | The time and the frame readout |
| Frames since the active Line's start; ms from its start; ms from its end | The time's tooltip (Q612): "+0 frames from line 46's start · 0 ms from its start · −3580 ms from its end" |
| Its colour on a keyframe | The time turns `warning` and shows a keyframe mark after it (`keyframe-next`, 16 units, `warning`), so a keyframe isn't told by colour alone (Q613) |

## Jump to

The time is a button. A click on it, or **Jump to** (Ctrl+G), turns it into a field with its text selected, in place and at the same width. Under the pointer the time shows the `hover` fill and a 1-unit `control` outline, so it reads as editable.

The field accepts:

| Input | Example | Meaning |
| --- | --- | --- |
| A time | `0:03:40.50`, `3:40`, `3:40.5`, `220.5` | That time (`QcNotesController::parseTime`, plus plain seconds) |
| A frame number | `5200` | That frame |
| A step in seconds | `+5s`, `-1.5s`, `+1:00` | Forward or back from the shown frame's time |
| A step in frames | `+12f`, `-12f` | Forward or back by frames |

- **Enter** seeks to the frame and returns the field to the time.
- **Esc** puts the time back without seeking. Leaving the field without Enter does the same.
- **Input it can't read** keeps the field open with the `danger` border, and one line under the row says why, in `danger`, with the warning icon: "Not a time or frame: 3:99. Try 3:39 or a frame number." The video doesn't move (Q617).
- A target past the end clamps to the last frame. Jump to stops playback first, as Aegisub's does.

A bare number is a frame. A number with a decimal or a colon is a time.

## Zoom readout

At the row's trailing end, a quiet button shows the zoom: "Fit" or "200 %". It is always shown (Q614). It opens a menu:

- **Fit to panel** (the default, checked while on)
- 50 %, 100 %, 200 %
- 1:1 device pixels
- a separator
- Zoom in, Zoom out

Ctrl+wheel, a pinch and the commands update it. Reset zoom and pan (`GLOBAL_RESET_VIDEO_ZOOM`) returns to Fit. Under `ZoomModel::Legacy` the readout shows the crop zoom's percentage. Full-screen typesetting has the same readout in its top bar ([full-screen-typesetting.md](full-screen-typesetting.md)).

## Seek bar

The seek bar takes keyboard focus by Tab; today it is `Qt.NoFocus`. While focused it shows the 1-unit focus ring round its groove, and:

- ←/→ step a frame;
- PgUp/PgDn go to the previous or next keyframe;
- Home/End go to the video's start and end (Q616).

Its accessible name is "Seek bar" and its value is the time. There is no separate command to focus it (Q584 dropped Aegisub's).

## Next and previous subtitle boundary

Two commands with a Video menu row each and no button. From the shown frame they step through the active Line's start, then its end, then the next Line's start, in Aegisub's order. Previous goes the other way.

## Commands

The Aegisub keys come from Aegisub 3.5's `default_hotkey.json`. The ids map them for a future Aegisub hotkey import. HikariSub's command ids are given in the code card, never reusing a retired id.

| Command | Aegisub id | Aegisub default | HikariSub default | Note |
| --- | --- | --- | --- | --- |
| Jump to | `video/jump` | Ctrl+G | Ctrl+G | Free in every context |
| Zoom 50 % / 100 % / 200 % | `video/zoom/50`, `video/zoom/100`, `video/zoom/200` | none | none | Menu rows and readout entries |
| Zoom in / Zoom out | `video/zoom/in`, `video/zoom/out` | Ctrl+Numpad + / Ctrl+Numpad − | Ctrl+Numpad + / Ctrl+Numpad − | Free |
| Zoom to fit, 1:1 device pixels | — | — | none | Readout entries and menu rows |
| Next / previous subtitle boundary | `video/frame/next/boundary`, `video/frame/prev/boundary` | Ctrl+Right / Ctrl+Left | Ctrl+Shift+Right / Ctrl+Shift+Left (Q615) | Ctrl+Left/Right are Set start/end time from video (5045, 5046) |

## Full screen

Simple full screen's strip ([#1207](https://github.com/altqx/hikari/issues/1207), [visual-language.md](visual-language.md#video-transport-and-full-screen)) carries the same row at the strip's sizes: the same buttons, the editable time with Jump to, the keyframe mark and the zoom readout. Previous/next file leave it too.

## Accessibility and states

- Every button has its name as its accessible name, and its tooltip adds the binding.
- The time reads as "Video time, 0:03:34.42, editable". The field announces its reason when its input can't be read.
- Each state is drawn in all four themes on the #1206 board: rest, hover on the time, the keyframe mark, editing, bad input, the zoom menu, 200 % and the focused seek bar.
- The code card shows them again in its before and after screenshots.
