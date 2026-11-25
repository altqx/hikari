# Full-screen typesetting

The second full-screen mode, built for typesetting ([#1208](https://github.com/altqx/hikari/issues/1208)). Simple mode ([#1207](https://github.com/altqx/hikari/issues/1207)) is for watching and QA. Typesetting mode has the whole toolset, laid out so that it is easy to use and understand. It is drawn in [Quiet](visual-language.md).

**Where the decisions come from.**

- The card's scope is Q571 and Q575–Q580 on tracking [#1209](https://github.com/altqx/hikari/issues/1209).
- The Aegisub parity audit added two rows (Q585): the freehand vector clip and the style step mode.
- The owner chose this design from a board on 2026-11-17 ("yes to all lgtm", Q618–Q627).

The app doesn't change until the code card lands after the beta.4 tag. [visual-tools.md](../visual-tools.md) stays the contract for what each tool does. This document only says how the tools are presented.

## Entering and leaving

- **Full screen typesetting** has its own View menu row and its own hotkey (Q575).
- In Simple mode's strip, **Typeset** switches to this mode in place, and **Simple** switches back. The frame keeps its position.
- Esc, when nothing else takes it, leaves full screen, as in Simple mode.

## Layout

The frame is always letterboxed, whatever Simple mode's bar setting is. Panels sit beside it and never over it (Q577).

| Region | Size | Holds |
| --- | --- | --- |
| Top bar | 40 high, the full width, on `bg` | The Simple / Typesetting switch (a segmented group), the Document's name in `muted`, then at the trailing end the zoom readout ([video-controls.md](video-controls.md#zoom-readout)) and Leave full screen |
| Rail | 188 wide, at the left, on `panel` | The tools, named (see [The rail](#the-rail)) |
| Frame | The rest | The picture, scaled to fit, letterboxed on black |
| Inspector | 320 wide, at the right, on `panel` | The active tool (see [The inspector](#the-inspector)) |
| Line box | Under the frame, between the rail and the inspector | The active Line, editable (see [The line box](#the-line-box)) |
| Status row | 28 high, the full width, on `bg` | Previous frame · Play/Pause · Next frame, the time, the frame, then "Line 46 of 412" and the video's size at the trailing end |

This is Q618. Splits are 1-unit `line`.

At a width under 1400 the rail is 160 and the inspector 288. At 1366 × 768 the frame keeps about 890 units of width.

**Colours (Q619).** The panels follow the app's theme in all four themes. The frame area is always black.

The time in the status row is the same control as the transport's: a click or Jump to (Ctrl+G) edits it ([video-controls.md](video-controls.md#jump-to)). The zoom commands work here too.

## The rail

One row per tool, 28 high: the tool's icon and its short name. The tools fall into groups with a 1-unit `line` between them:

1. Pointer
2. Position, Move, Scale, Rotate, Tilt X / Y, Perspective
3. Rectangle clip, Vector clip
4. Drawing, Gradient, Text on path
5. Shift positions, All tags

The active tool is `select` with its name in semibold and its icon's accent layer in `accent`. High contrast adds a 1-unit `accent` boundary. Up and Down move between tools and Enter or Space chooses one. Each row's tooltip is today's full name with its binding.

**Short names (Q620).** The rail uses the short names below. Menus, commands, the hotkey list and tooltips keep today's names.

**The windowed Video panel (Q621, Q580).** It uses the same rail component, icon only, with the names on hover. That's today's tool rail, so the video keeps its width. Full screen shows the names.

## The inspector

A column with the tool's name as its header and a **?** button at the trailing end. Under it, in order:

1. **The hint**: one line saying what dragging does, on a `select`-tinted strip with the information icon. In high contrast it has a `line` boundary instead of the tint.
2. **The fields**: the tool's options and values in plain words ("Start X", "Ends after"), each under its 12-unit `muted` label, units inside the field. These are VT1's options and values, laid out as a column. Labels scrub sideways as in VT1.
3. **On the frame**: each handle the tool draws, named, with a small swatch of its mark ("Start point · filled circle").
4. **Apply to** at the foot: This line / Picked lines (N). This is VT1's batch picker as a segmented group.

The windowed panel keeps VT1's one-line inspector bar. It is the same component, laid out as a row: the hint becomes the **?** button's tooltip, and the handle names show when the pointer is over a handle. When the row is short, the **?** gives way first, before any field folds (#1414): once groups fold it sits in More's header, and while they still fit without it the active tool's rail button carries the hint in its tooltip and accessible description.

**The guide (Q626).** **?** opens a popover beside the inspector:

- the tool's name and what it writes (for example `\move(x1,y1,x2,y2,t1,t2)`);
- three to five lines on how to drag;
- its keys;
- "More in the guide", which opens the tool's page in the documentation.

There are no tours and no walkthroughs (Q578).

**Tool dialogs (Q625).** Edit shapes, Edit tags and the other tool dialogs open in the inspector's place, with the dialog's name as the header and × back to the inspector. Their content and behaviour are PU5's ([visual-tools.md](../visual-tools.md#edit-shapes-and-edit-tags-pu5)); only where they open changes. The windowed panel still opens them as windows.

- In the inspector's width, a dialog's columns stack into one scrolling column: the list, then the fields, then the preview. Undo and Redo sit at its foot.
- With both open, the inspector shows the one opened last; × on it shows the other.
- Esc, like ×, closes the dialog. Leaving full screen with a dialog open moves it back into its window.

## The line box

Under the frame:

- **Top row, left to right:**
  - Previous line, the Line's number in a chip, Next line;
  - its times in `muted` monospace;
  - the style button;
  - at the trailing end, "Enter applies · Esc reverts" in `muted`, then Undo and Redo (Q627).
- **The text**: one field with the Line's text and its tags. Tags are in the Line editor's syntax colours, as in the Line editor.
  - Enter applies; Esc puts the text back.
  - The field wraps to two lines and then scrolls.
- **Stepping**: Previous and Next line, and the Line editor's bindings for them, move the active Line. The frame seeks to the new Line's start when the video follows the active Line, as it does in the window.

Undo and Redo are the Document's, the same as Ctrl+Z and Ctrl+Y.

## The style picker and step mode

The style button opens a popover over the line box. It has a filter field (type to filter by name, ↑/↓ to move) and the Document's styles, each with its font and size in `muted`. At the field's end is the **Step through lines** toggle (`style-apply`).

- **Step mode off:** Enter or a click applies the style and closes the popover, as a combo box does.
- **Step mode on:**
  - Enter applies the highlighted style to the Line and moves to the next one, keeping the popover open with the filter cleared.
  - Shift+Enter goes back a Line.
  - Esc closes the popover.
  - A line at the popover's foot says what Enter will do: 'Enter applies "Sign" to line 46 and goes to 47.'
  - The style goes to the active Line alone: with several Lines selected in the Grid, the selection narrows to the active Line first, as Aegisub's Styling Assistant does.
- **Order (Q624):** when several Lines are picked, the step goes through the picked Lines in Grid order. Otherwise it goes through every Line in Grid order.
- **The Styles panel** gets the same toggle with the same behaviour.

## Vector clip and Drawing: freehand and segments

**Freehand (Q622).** Vector clip and Drawing each gain a seventh mode under "Draw with", after Spline: **Freehand**.

- Hold the button and draw. On release, the stroke is fitted to Bézier curves (`b`) within the **Smoothing** tolerance, not a run of `l` points.
- **Smoothing** runs 0–100 with a default of 45. Low stays close to the stroke with more points; high gives fewer, rounder curves.
- Smoothing is remembered per tool.
- The point and curve counts under it update as it moves.
- Moving the slider refits the last stroke until anything else is edited. The stroke and its refits are one undo step.

**Segments (Q623).** Right-clicking a segment opens a menu: **Make curve** · **Make straight** · **Insert point here** · **Delete segment**.

- Make curve turns an `l` into a `b`, with its control points at a third and two thirds of the segment.
- Make straight turns a `b` into an `l`.
- Insert point here is the existing insert at the click.

**New parts.** These are in the code card:

- a `vector-freehand` icon in the K1 set;
- `VIDEO_VECTOR_CLIP_FREEHAND`, a Video command with no default key, next to `VIDEO_VECTOR_CLIP_DRAG` … `_DELETE` (2034–2039) and shared by both tools as those are;
- the four segment commands as menu rows only.

## Tool names and hints

The hints and handle names below were checked against each tool's code, modifier keys included, in [#1208](https://github.com/altqx/hikari/issues/1208)'s first part. A tool whose behaviour changes updates its row.

| Today (menus, commands) | Rail name | One-line hint | Handles named in the inspector |
| --- | --- | --- | --- |
| Position pointer | Pointer | Point at the picture to read a position. Ctrl-click puts the line there. | — |
| Text positioning | Position | Drag to place the line. Shift-drag locks to one axis. Right-click places it at the pointer. | Position · square |
| Text moving | Move | Drag the start or end point; right-drag sets the end. The line travels between them. | Start point · square, End point · circle, Path · arrow, Current position · cross |
| Text scaling | Scale | Drag to stretch across, right-drag to stretch up. Shift-drag scales both. | Width, Height, Both · arrow |
| Text Z rotation | Rotate | Drag round the origin to turn the text. Drag the origin to move it. | Origin · cross, Angle · line |
| Text X / Y rotation | Tilt X / Y | Drag sideways to turn round Y, right-drag up or down to turn round X. Drag the origin to move it. | Origin · cross, Tilt grid · outline |
| Perspective | Perspective | Drag the corners to lay the text on a surface. Drag inside to move it. | Corners · square, Plane corners · circle, Origin · cross |
| Rectangle clipping | Rectangle clip | Drag a box. Only what is inside shows. Drag its edges to resize, inside to move. | Edges and corners · outline |
| Vector clipping | Vector clip | Click to add points, drag them to reshape. Only what is inside shows. Freehand: hold and draw. | Point · square, Curve handle · circle |
| Vector drawing | Drawing | Click to add points, drag them to reshape. Or pick a shape and drag a box. Freehand: hold and draw. | Point · square, Curve handle · circle |
| Position shifter | Shift positions | Drag to move the positions, clips and origins of the picked lines together. Shift locks an axis. | Position and move start · square, Move end · circle, Origin · cross, Clip and drawing · outline |
| Hydra | All tags | Choose a tag, then drag sideways on the picture to change it. Shift for small steps. | Changed line · outline |
| Gradient | Gradient | Drag across the text to set the gradient’s direction. Drag the ends to adjust; Enter applies. | Start and end, Skew, P1, P2 and P3 · square |
| Text on path | Text on path | Click to add path points, drag them to shape the path. The text follows; Enter applies. | Point · square, Curve handle · circle |

The strings live in `HikariSub.VisualTools`, next to the tools' own.

## Commands

| Command | Default key | Note |
| --- | --- | --- |
| Full screen typesetting | Given in the code card, checked against every context | Q575; a View menu row |
| Typeset / Simple (switch modes in place) | none | Buttons in each mode's bar |
| Vector clip: Freehand | none | Shared by Vector clip and Drawing |
| Jump to, zoom levels, zoom in and out | As in [video-controls.md](video-controls.md#commands) | They work in both full-screen modes |

## Accessibility

- The rail is a list with the tools' full names as accessible names.
- The inspector's fields have their labels as accessible names. The hint is the inspector's accessible description.
- The handle names are announced when keyboard focus reaches a handle's field.
- The line box is a text field named "Line 46 text".
- F6 moves through the top bar, the rail, the frame, the inspector, the line box and the status row.
- Each region keeps the 1-unit focus ring.
