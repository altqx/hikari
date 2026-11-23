# Simple mode (Watching)

The full-screen mode for watching and QA ([#1207](https://github.com/altqx/hikari/issues/1207)). [Full-screen typesetting](full-screen-typesetting.md) (#1208) is the other full-screen mode. Simple mode is drawn in [Quiet](visual-language.md#video-transport-and-full-screen), and its Discord mode is Watching ([Discord presence settings](discord-presence.md#modes)).

**Where the decisions come from.**

- The card's scope is Q566–Q569, Q575, Q576 and Q579 on tracking [#1209](https://github.com/altqx/hikari/issues/1209).
- The owner chose this design from the mode gate board on 2026-11-17 ("yes lgtm", Q698–Q704; see [Decisions](#decisions)).

**In the code** (#1207):

- `VideoFullscreenController::mode` is Simple or Typesetting. Until #1208 lands, Typesetting shows the visual tool rail, the inspector bar and the Visual tools button in the strip; #1208 replaces that layout.
- The settings are `video.fullscreenBarPosition` (0 below, 1 over), `video.fullscreenBar` (0 Auto-hide, 1 Always, 2 Never), `video.fullscreenHidePointer`, `video.fullscreenOverlays` and `video.fullscreenOpensIn` (0 Simple, 1 Typesetting). The Full screen section is the first on Settings › Video, so Full-screen settings… lands on it.
- Row 1's start time is the shown frame's time.
- The zoom readout, the editable time and the keyframe mark come with #1206. Watching's Discord mode and art come with #1245.
- A frame the source can't decode is `VideoSession::failedFrame`, cleared by the next request.

## Entering and leaving

- **Full screen** keeps its View menu row and its hotkey. It opens in the mode chosen under Settings › Video › Full screen › Opens in, Simple by default (Q701). **Full screen typesetting** still opens #1208's mode directly.
- **Typeset** in the strip switches to full-screen typesetting in place, and #1208's **Simple** switches back. The frame keeps its position.
- Esc, when nothing else takes it, leaves full screen. It works in every state below.
- The command palette (Ctrl+Shift+P, [#1236](command-palette.md)) opens over the picture, 24 from the top.

## The picture

| Bar position | Show bar | The picture |
| --- | --- | --- |
| Below the picture (letterbox), the default | Auto-hide, the default | Scaled to fit the area above the strip. The strip's room stays reserved, so the frame never changes size when the strip hides |
| Below the picture | Always | The same, with the strip always shown |
| Over the picture | Auto-hide or Always | Scaled to fit the screen; the strip lies over its bottom on a gradient from transparent to black at 82 % |
| either | Never | Scaled to fit the whole screen; no strip |

New installs get Below the picture with Auto-hide. A beta.4 user's existing Always or Never choice is kept.

The render-cost and time overlays stay at the top right, as today. The title row with the file name goes.

## The strip

Always on black, in every theme. Text and icons are `#EEF0F2`; secondary text is the same at 62 %. In high contrast its top edge is a 1-unit white line. It is 112 high below the picture.

1. **Row 1:** the start time, the seek bar across the full width, and the length, in the 12-unit monospace role. When keyframes are loaded, they are faint 1-unit ticks on the seek bar (Q703).
2. **Row 2,** three groups:
   - **leading:** the active Line's number in muted monospace and its text, read-only and elided. With no Line at the current time: "No line at this time" in muted (Q699);
   - **centred:** previous frame, play/pause, next frame, play line, stop, then the time in the 20-unit monospace role;
   - **trailing:** the zoom readout ([video-controls.md](video-controls.md#zoom-readout)), **Typeset**, volume and its slider, and Leave full screen.

Buttons are 34 × 34 with 20-unit glyphs, unboxed. Hover is white at 12 %. Typeset is a text button with the `tool-position` glyph; its tooltip is "Typeset: switch to full-screen typesetting · the frame keeps its place". Previous and next file are not in the strip (#1206). The visual tools, their inspector and the "Visual tools" button are in #1208's mode only.

### Showing and hiding

Q698. With Show bar at Auto-hide, while playing:

- the strip hides 2 seconds after the pointer stops moving (today's `kFullscreenBarIdleMs` is 1.5 s);
- pointer movement or any key except Space shows it;
- the pointer hides with the strip and comes back with it (Settings › Video › Full screen › Hide the pointer with the bar, on).

While paused, the strip stays shown.

### The strip's menu

Q700. Right-clicking the strip, or Shift+F10 on it, opens:

- **Bar position:** Below the picture (letterbox), Over the picture (radio items);
- **Show bar:** Auto-hide, Always, Never (radio items);
- **Full-screen settings…**, which opens Settings at Video › Full screen.

The menu is drawn like the strip: `#1B1D21`, `#EEF0F2` text, a 1-unit white line at 18 %, radius 6. It replaces today's "Show when the pointer moves" button.

## Settings

Q701. Settings › Video gets a **Full screen** section, in the [Settings](settings.md) layout:

| Row | Control | Setting | Default |
| --- | --- | --- | --- |
| Bar position | Below the picture (letterbox) · Over the picture | new, named by the code card | Below the picture |
| Show bar | Auto-hide · Always · Never | `video.fullscreenBar` (today's) | Auto-hide |
| Hide the pointer with the bar | check box | new | on |
| Show the render time and position overlays | check box | new | on |
| Opens in | Simple · Typesetting | new | Simple |

Descriptions under the rows: "Below keeps the whole picture in view; the bar sits in the black strip under it." and "Auto-hide shows it when the pointer moves or a key is pressed, and hides it 2 seconds later while playing. Below the picture, the strip stays reserved."

## States

Q702.

| State | What shows |
| --- | --- |
| No Line at this time | The leading group says "No line at this time" |
| Seeking or waiting for a frame | The last frame at 55 % brightness and a 34-unit spinner on it with "Seeking…"; the strip keeps working |
| A frame that won't decode | The picture goes; on black, a warning glyph and "Can't decode frame 5146 of sprite-fright.webm. Playback stopped." with "Seek to another time, or press Esc to leave full screen." under it. The time dims to 50 % |
| No video open | Full screen needs a video: its menu row is disabled, and the command palette shows it as "Needs a video" |

Full screen is never left because of an error.

## Keyboard and accessibility

- Space plays and pauses without showing the strip. The other transport keys work as in the Video panel.
- F6 moves to the strip and through its three groups; the seek bar takes the arrow keys as in [video-controls.md](video-controls.md).
- The leading group is a read-only label named "Line 46: *text*", or "No line at this time".
- Each button's accessible name is its tooltip.

## Discord

While Simple mode is open, presence shows **Watching** (`watching`). Its art is the `media-play` glyph in navy on #C9E7A4, drawn with the code card (Q697, Q704).

## Decisions

The owner said "yes lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6939502207)):

| Q | Decision |
| --- | --- |
| Q698 | The strip hides 2 s after the pointer stops while playing, shows on pointer movement or any key but Space, and the pointer hides with it |
| Q699 | With no Line at the current time, the leading group says "No line at this time" |
| Q700 | Right-clicking the strip offers Bar position, Show bar and Full-screen settings… |
| Q701 | Settings › Video › Full screen holds Bar position, Show bar, Hide the pointer, the overlays and Opens in |
| Q702 | Seeking dims the last frame with a spinner; an undecodable frame shows one line on black with the next step; Esc always leaves |
| Q703 | Keyframes show as faint ticks on the seek bar when loaded |
| Q704 | Watching's Discord art is a navy play glyph on #C9E7A4 |
