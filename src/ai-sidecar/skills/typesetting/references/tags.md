# ASS override tags for typesetting

Tags go in `{…}` blocks in a Line's text. A tag holds until the next block changes it; tags that act on the whole Line (marked *Line*) count once, from the first block.

| Tag | Does | Notes |
| --- | --- | --- |
| `\an1`–`\an9` | Alignment, as on a numeric keypad: 7 top-left, 5 centre, 3 bottom-right | *Line*. The point `\pos` sets is this corner or centre. |
| `\pos(x,y)` | Places the Line at a point | *Line*. Script coordinates. |
| `\move(x1,y1,x2,y2[,t1,t2])` | Moves from one point to another, between t1 and t2 ms after the Line starts | *Line*. Not with `\pos`. |
| `\org(x,y)` | The point rotations turn around | *Line*. Default: the `\pos` point. |
| `\frz<deg>` | Rotation in the picture plane, counter-clockwise | |
| `\frx<deg>`, `\fry<deg>` | Rotation about the horizontal and vertical axes, for perspective | Pair with `\org`. |
| `\fax<f>`, `\fay<f>` | Shear: 0.1 slants by a tenth | |
| `\fscx<%>`, `\fscy<%>` | Width and height scale, 100 being the Style's | |
| `\fs<size>` | Font size in script pixels | |
| `\fsp<px>` | Extra spacing between letters | |
| `\fn<name>` | Font name | The font must be installed or attached. |
| `\b1`, `\b0`, `\i1`, `\i0` | Bold and italic on or off | `\b700` sets a weight. |
| `\c&HBBGGRR&` or `\1c` | Fill colour | Blue, green, red in hex, the reverse of web colours. |
| `\3c&HBBGGRR&`, `\4c&HBBGGRR&` | Outline and shadow colours | |
| `\alpha&HAA&`, `\1a`–`\4a` | Transparency of all parts or one: 00 opaque, FF invisible | |
| `\bord<px>`, `\xbord`, `\ybord` | Outline width | |
| `\shad<px>`, `\xshad`, `\yshad` | Shadow distance | |
| `\blur<n>`, `\be<n>` | Softens the edges | `\blur` is Gaussian; small values (0.5–1) blend a sign into the picture. |
| `\clip(x1,y1,x2,y2)`, `\iclip(…)` | Shows only the inside, or only the outside, of a rectangle | *Line*. |
| `\clip(m x y l x y …)` | A drawing as the clip, for a shape that isn't a rectangle | *Line*. |
| `\t([t1,t2,][accel,]tags)` | Animates the tags from t1 to t2 ms after the Line starts | |
| `\fad(in,out)` | Fades in and out over ms | *Line*. |
| `\q2` | No automatic line wrapping | *Line*. |
| `\p1` … `\p0` | The text between is a drawing (`m`, `l`, `b` commands) | |
| `\r`, `\r<Style>` | Back to the Line's Style, or to another Style | |

A line break inside a Line is `\N`.
