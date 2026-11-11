# Icon round 4 (IC5)

[IC5](https://github.com/altqx/hikari/issues/860) draws, ahead of time, the icons that the wave-8 native-feature cards will need, and parks each one as `pending` for its card (owner Q207, 2026-11-09). Rules: [icons.md](icons.md). **New** is a role drawn for IC5. **Reused** is a role already in the set with the same meaning.

Left without an icon, as IC1 to IC3 decided:
- check items and option checks inside a dialog;
- entries named by their data;
- plain text verbs in a DialogButtonBox;
- dialog sections with a switch.

## Inventory

| Card | Command or surface | Role |
| --- | --- | --- |
| AU4 #839 | Grid and Line editor menus: Remove tags… | remove-tags (new) |
| AU4 #839 | "And clean up" and each Clean up action | none: checks in the dialog |
| AU5 #840 | clipper2 build for ILL | none: no UI |
| AU6 #845 | Drawing tool strip, Pathfinder: Unite, Subtract, Intersect, Exclude | shape-unite, shape-subtract, shape-intersect, shape-exclude (new). The Pathfinder button shows shape-unite |
| AU6 #845 | Drawing tool strip: Offset | shape-offset (new) |
| AU6 #845 | Clip to shape, Shape to clip | clip-to-shape, shape-to-clip (new) |
| AU6 #845 | Merge drawings | shape-merge (new) |
| AU7 #843 | Visual tool rail: Gradient | tool-gradient (new) |
| AU7 #843 | Gradient strip modes: rectangle, skewed, rotated, wedge (vector), along the clip's edge, by character, by Line | gradient-rect, gradient-skew, gradient-rotated, gradient-vector, gradient-clip-edge, gradient-by-char, gradient-by-line (new) |
| AU7 #843 | Merge strips | gradient-merge (new) |
| AU8 #842 | Video › Tracking: Apply motion data…, Revert motion data, Copy motion data from lines, Extend tracked lines…, Extrapolate \move to the line | motion-apply, motion-revert, motion-copy, motion-extend, move-extend (new) |
| AU8 #842 | Perspective tracking…, Copy Power-Pin track from lines | tool-perspective, perspective-plane (reused) |
| AU9 #844 | Node library: shake, wobble, bounce, typewriter, fade by letter, focus lines, grain, chromatic aberration, random move, karaoke to alpha | graph-shake, graph-wobble, graph-bounce, graph-typewriter, graph-fade-by-letter, graph-focus-lines, graph-grain, graph-chromatic-aberration, graph-randomove, graph-karaoke-to-alpha (new) |
| AU9 #844 | Run a graph on selected Lines | not drawn: the command isn't designed yet |
| AU10 #841 | Select lines: Overlapping lines | select-overlapping (new) |
| AU10 #841 | Grid › Split: at tag blocks, every N frames / N seconds | split-tag-blocks, split-every-n (new) |
| AU11 #846 | Track colour, Find fade | track-colour, find-fade (new) |
| AU14 #847 | Visual tool rail: Text on path | tool-text-on-path (new) |
| AU16 #850 | Reset popup: Reinstall N scripts | reinstall-scripts (new) |
| AU16 #850 | Reset popup: Open backup folder | show-in-folder (reused) |
| AU16 #850 | A failed reinstall's Try again | refresh (reused) |
| AU16 #850 | Per-script progress | run-busy, run-applied, notice-error (reused) |
| AU18 #852 | Grid and Subtitles menus: Blur and glow layers…, and its repeat command | layers, layers-repeat (new), as AU18's design asks |
| AU18 #852 | Dialog: Glow, Border and Shadow sections | none: sections with a switch. Add and Remove take add and remove (reused) |
| AU19 #853 | Snap start or end to the previous or next keyframe | snap-start-to-previous-keyframe, snap-start-to-next-keyframe, snap-end-to-previous-keyframe, snap-end-to-next-keyframe (new) |
| AU19 #853 | Remove lead-in, Remove lead-out | lead-in-remove, lead-out-remove (new) |
| AU19 #853 | Add lead-in, Add lead-out | lead-in, lead-out (reused) |
| AU19 #853 | Join, split at the video frame, snap to the neighbouring Line | join-lines, split-at-video, continuous-previous, continuous-next (reused). A join that changes times only would get its own role |
| AU19 #853 | Extend previous line to this start, Extend next line to this end (time-only joins) | extend-previous-to-start, extend-next-to-end (new, drawn in AU19 at the owner's gate, Q327) |
| AU19 #853 | Shift start or end by N frames | time-offset-start, time-offset-end (reused) |
| AU19 #853 | Timing postprocessor: Assistant mode | timing-assistant (new) |
| AU19 #853 | Assistant: Accept, Change, Skip | commit, edit, chevron-right (reused) |
| AU20 #854 | Line breaks: Insert balanced break, Move break left, Move break right, Remove breaks | line-break-insert, line-break-left, line-break-right, line-break-remove (new) |
| AU20 #854 | Autowrap (\q2) | none: a check item |
| AU20 #854 | Change case ▸ (lower, UPPER, Title, Sentence) | change-case, case-lower, case-upper, case-title, case-sentence (new) |
| AU20 #854 | All characters to: full-width, half-width | fullwidth, halfwidth (new) |
| AU20 #854 | Dialog swapper | dialogue-swap (new) |
| AU21 #855 | Change alignment, keep position | change-alignment (new) |
| AU21 #855 | Align left, centre, right, top, middle, bottom; distribute horizontally, vertically | align-left, align-center, align-right, align-top, align-middle, align-bottom, distribute-horizontal, distribute-vertical (new) |
| AU21 #855 | Margin position, both ways | pos-to-margins, margins-to-pos (new) |
| AU21 #855 | Phantom | phantom (new) |
| AU21 #855 | Cycle-value commands, and their list in Settings | cycle-values (new) |
| AU21 #855 | Position shifter: X and Y fields | shift-by-x, shift-by-y (new) |
| AU21 #855 | Copy and Paste columns with tags, the clip or the start block | columns-copy, columns-paste (reused): new choices in the existing dialogs |
| AU22 #856 | Grid: Interpolate across Lines | interpolate-lines (new) |
| AU22 #856 | Cascade, acceleration, tag filter | none: options in the dialog |
| AU23 #857 | File › Import: Import lyrics… | import-lyrics (new) |
| TH3 #831 | Settings › Audio: Split Thai into character clusters | none: a check item |
| TH4 #832 | Split lines into characters | split-characters (reused): the existing command splits by grapheme cluster |
| TH5 #833 | Automation manager: the built-in Karaoke Templater row | builtin-script (new) |
| TH5 #833 | karaOK presets, karaOK export choice | notice-info (reused, Q155) |
| TH5 #833 | Template editor mode, export format | none: choices named by their data |
| AI23b #800 | Composer globe, "Searched the web" rows | web-search (reused; drawn by IC3, pending its card) |
| AI23b #800 | An answer's Sources list | citations (new): the mockup used link, which means only the chain |
| AI23b #800 | "Read <site>" rows | web-page-read (new): the mockup used open-link, which means only "Open in the browser" |
| AI23b #800 | A citation's Open in browser | open-link (reused) |
| AI23b #800 | Used-up plan notice | notice-warning (reused) |
| AI23b #800 | Google's search suggestions | none: Google's own snippet, drawn as sent; never drawn by us |
