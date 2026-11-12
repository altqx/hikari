---
name: typesetting
description: Typesetting signs in ASS subtitles. Use it to place, size, rotate, colour or clip text over on-screen signs and titles with override tags such as \pos, \an, \frz, \fscx, \clip and \c, to match a sign's font and colours, and to check the result against the video frame.
---

# Typesetting

A typeset sign is a Line whose text sits over writing in the video, in the sign's place, angle, size and colours, so the translation reads as if it were part of the picture.

## Steps

1. **Find the sign.** Read the Line, then look at its frames: `video_frame` with `line`, `subtitles: false` and `full: true` shows the Line's first frame whole, without subtitles. `search_tools` with the group `typeset` offers the frame tools and the solvers.
2. **Measure, don't guess.** Take colours with `video_colours` at points inside the sign's letters, its outline and its background. Take the sign's corners from the frame image: an image point maps to a video point as the `video_frame` description says, and a video point to a script point by `PlayResX` over the video's width.
3. **Let the solvers write the tags.** You propose where the sign is; the solvers place the tags exactly. Each returns the Line's new text and changes nothing:
   - `typeset_perspective` takes the sign's four corners (top left, top right, bottom right, bottom left) and writes `\pos`, `\org`, the rotations, `\fax` and the scales that put the Line on them. Use it for any sign that is turned or seen at an angle.
   - `typeset_clip` writes a rectangle or vector clip; `typeset_drawing` puts a shape (`\p1`) at a point.
   - `typeset_shift` moves positions, `\move` points, clips and `\org` together, for a sign that moved.
   - `typeset_hydra` sets, adds to, multiplies or runs a gradient of one tag across Lines, such as `\blur`, `\bord`, `\1c` or `\fax`.
   Write the other tags (font, colours, borders) yourself, in the first override block.
4. **Check before you apply.** `typeset_check` renders the Line alone with the proposed text on its frame and compares where its ink lands with the sign's corners or box, both in script coordinates. When it fails, measure again or fix the tags, and check again.
5. **Apply.** `typeset_apply` takes every Line of the sign with its text and target, checks each again and applies them as one undo step only when all pass; the change then waits in the user's review. Use `document_apply` only for Lines that have no place on the picture.
6. **Follow the sign over time.** A sign that moves, or that the camera pans across, needs `\move` or one Line per keyframe span. `video_keyframes` gives the cuts; a sign stays inside one shot.

## Rules

- Keep the Line's Style. Change it only when the user asks; put the differences in tags.
- Keep tags the user wrote unless the change is about them.
- `\pos`, `\move`, `\org` and `\clip` coordinates are in the script's resolution (`PlayResX` × `PlayResY` in `info_list`). Video points, which `video_colours` and the frame images use, scale by `PlayResX` over the video's width (`video_info`).
- Use `\an5` for a sign you centre on a point, and `\an7` when you align its top-left corner.
- Set `\bord0\shad0` unless the sign has an outline or a shadow, and colour them with `\3c` and `\4c` when it does.
- Don't work out `\frx`, `\fry`, `\frz`, `\fax` or `\org` by hand: give `typeset_perspective` the corners. Write the font, size and colour tags before you solve, since the solver measures the Line as it is.
- One sign is one Line, unless parts of it need different tags, such as two colours or a layer behind the text.
- Name each edit for the user, such as "Typeset the shop sign".
