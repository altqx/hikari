---
name: typesetting
description: Typesetting signs in ASS subtitles. Use it to place, size, rotate, colour or clip text over on-screen signs and titles with override tags such as \pos, \an, \frz, \fscx, \clip and \c, to match a sign's font and colours, and to check the result against the video frame.
---

# Typesetting

A typeset sign is a Line whose text sits over writing in the video, in the sign's place, angle, size and colours, so the translation reads as if it were part of the picture.

## Steps

1. **Find the sign.** Read the Line, then look at its frames: `video_frame` with `line`, `subtitles: false` and `full: true` shows the Line's first frame whole, without subtitles. `search_tools` with the group `typeset` offers the frame tools.
2. **Measure, don't guess.** Take colours with `video_colours` at points inside the sign's letters, its outline and its background. Take the sign's position and size from the frame image: an image point maps to a video point as the `video_frame` description says.
3. **Write the tags.** Put one override block at the start of the text: alignment and position first, then rotation, scale, font, colours and borders. The [tag reference](references/tags.md) lists them.
4. **Check.** Draw the frame again with the Line (`video_frame` with `line`), and with `video_ink_bounds` compare where the ink lands with where the sign is. Fix and check again until they match.
5. **Follow the sign over time.** A sign that moves, or that the camera pans across, needs `\move` or one Line per keyframe span. `video_keyframes` gives the cuts; a sign stays inside one shot.

## Rules

- Keep the Line's Style. Change it only when the user asks; put the differences in tags.
- Keep tags the user wrote unless the change is about them.
- `\pos`, `\move`, `\org` and `\clip` coordinates are in the script's resolution (`PlayResX` × `PlayResY` in `info_list`). Video points, which `video_colours` and the frame images use, scale by `PlayResX` over the video's width (`video_info`).
- Use `\an5` for a sign you centre on a point, and `\an7` when you align its top-left corner.
- Set `\bord0\shad0` unless the sign has an outline or a shadow, and colour them with `\3c` and `\4c` when it does.
- A rotated sign needs `\frz` for its angle in the picture plane; perspective needs `\frx` and `\fry` with `\org` at the vanishing point, so check each change on the frame.
- One sign is one Line, unless parts of it need different tags, such as two colours or a layer behind the text.
- Name each edit for the user, such as "Typeset the shop sign".
