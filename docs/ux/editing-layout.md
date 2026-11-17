# Editing layout

The default layout, as a mode: what the window holds while editing, and how its panels look when they are empty, busy or failed ([#1205](https://github.com/altqx/hikari/issues/1205)). The arrangement is [workspaces.md](workspaces.md)'s and doesn't change. It is drawn in [Quiet](visual-language.md), and its Discord mode is Editing ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("yes lgtm", Q705–Q709; see [Decisions](#decisions)). The app doesn't change until the code cards land after the beta.4 tag.

## The window

| Region | Holds |
| --- | --- |
| Title row, 36 high | Menus, the layout switcher reading "Editing", the Document tabs, the side-bar toggles, the window buttons ([visual-language.md](visual-language.md#composition)) |
| Video, upper left, about 44 % | The tool rail, the frame, the seek bar and the one transport row of [video-controls.md](video-controls.md) |
| Audio, upper right, above the Line editor | The waveform or spectrum, the ruler and [the toolbar](#the-audio-toolbar) |
| Line editor, under Audio | Tag buttons, the text, the time, style and actor row, the layer, effect and margins row |
| Grid, the full width below | Its filter field in the header, then the column headings |
| Side bars, outside | As left; Find at first start, as today ([ADR 0032](../adr/0032-side-bars.md)) |
| Status bar | Labelled numbers from the leading edge |

At 1366 × 768 the Line editor's rows wrap in two at most.

## The Audio toolbar

Q709. One row, 34 high, of 24-unit tool buttons in groups 10 apart, the same in Editing and [Timing](timing-layout.md):

| Where | Group |
| --- | --- |
| Leading | Previous line, Play, Play line, Stop, Next line |
| | Play around the start, Play around the end: each a button with a separate 16-unit chevron for its menu |
| | Add lead-in, Add lead-out |
| | Commit, Commit automatically, Go to the next line after commit |
| Trailing | Waveform · Spectrum, one segmented group |
| | Snap to keyframes, Follow the playhead |
| | Zoom, the zoom readout in `muted` ("1.0×"), Volume, Audio settings |

Checked toggles take the `select` fill and the icon's accent layer, as every toggle does ([visual-language.md](visual-language.md#fixes-by-area), finding 9).

## Panel states

Each panel shows its own empty, busy and error states. The rest of the window stays usable.

### Empty

Q705. In the panel's area, on `field`, centred: the panel's file glyph at 24 units in `muted`, one muted line, then one or two buttons.

| Panel | Line | Buttons | Also |
| --- | --- | --- | --- |
| Video | No video | Open video… (secondary), Use a dummy video… (quiet) | "or drop a video file here" in 11.5-unit `muted`. The rail and the transport row are disabled |
| Audio | No audio | Open audio… (secondary), Use the video's audio (quiet, only when a video with audio is open) | The ruler is empty |

### A new file

Q708. A new file shows its one empty Line, selected, and under it, in `muted`: "A new file starts with one empty line. Type in the Line editor, or open a file." The side bar starts closed for a new window with no file; a window that had one keeps it as left.

### Busy

Q706. While a panel opens or reads a file:

- a 2-unit `accent` progress line runs under the panel's header, determinate when the work reports progress;
- one muted line says what is happening: "Opening sprite-fright.webm · Indexing keyframes · 42 %" in the frame, or "Reading audio from sprite-fright.webm · 63 %" in a 26-unit row under the Audio header with **Cancel** (quiet) at the trailing end;
- the waveform fills in as it is read;
- the status bar shows nothing extra.

### Errors

Q707. A failure that belongs to a panel shows in that panel, centred on `field`:

- the `notice-warning` glyph in `danger`, 24 units;
- one line naming the file and the reason, in `text`: "Can't open sprite-fright.mkv: its video codec (VVC) isn't supported.", "sprite-fright-trailer.webm has no audio track.";
- the next step as a secondary button (Open another video…, Open audio…) and **Details** (quiet), which opens the Log at that entry.

A toast shows only for a failure no panel can show.

## With the other specs

- **Command palette.** Ctrl+Shift+P opens it 8 under the title bar ([command-palette.md](command-palette.md)).
- **Settings.** File › Settings and the palette open the one Settings window ([settings.md](settings.md)).
- **Video row.** As [video-controls.md](video-controls.md) (#1206).

## Discord

Presence shows **Editing** (`editing`), unchanged from #599.

## Decisions

The owner said "yes lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6939502207)):

| Q | Decision |
| --- | --- |
| Q705 | Empty panels show one muted line and one or two buttons, plus "or drop a video file here" for Video |
| Q706 | Busy panels show a 2-unit accent progress line under the header and one muted line with Cancel; the rest of the window stays usable |
| Q707 | A failure shows as one line in the panel that failed, with the next step and Details; a toast only when no panel can show it |
| Q708 | A new file shows its one empty Line with a muted hint, and the side bar starts closed |
| Q709 | The Audio toolbar order: playback, play around start and end, lead-in and lead-out, commit; then Waveform/Spectrum, snapping and follow, zoom, volume, settings |
