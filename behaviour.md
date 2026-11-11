# AU21 #855: behaviour record

I read these sources and didn't run them. Clones and vendored copies as of 2026-11-11:
- **ILL:** `ILL.ChangeAlign.moon` 1.1.2 and its `Line.changeAlign` (vendored at f3e581c04d74, `modules/ILL/ILL/Ass/Line.moon:906`).
- **Chrolo:** `chrolo.ChangeAlignment.lua` 1.0.2.
- **PhosCity:** `phos.AlignAndDistribute.moon` 0.0.2.
- **petzku:** `petzku.PosToMargin.moon` 2.0.1 and `petzku.Phantom.moon` 1.1.2.
- **unanimated:** `ua.Cycles.lua` 2.0, `ua.Relocator.lua` (Teleportation only) and `ua.MultiCopy.lua`.
- **Akatsumekusa:** `aka.Cycles.moon` 1.1.4.

Jibrill's Position shifter v2 isn't public. Its gap is audit O38: the shift is drag-only, and there's no numeric shift.

## Change alignment

**ILL.ChangeAlign.** You pick one of the nine values in a 3×3 grid. For each selected Line:
- It gets the Line's width and height: the text's layout box, or for a shape, the path's bounding box.
- It shifts `\pos` by the offset between the old and new anchor, where the offset is 0, half or all of the width and the height. If the Line has `\move`, both of its points shift instead. A Line with no `\pos` gets one from its alignment and margins.
- The offset is rounded to **whole pixels**.
- If the Line is rotated (Style angle, `\frx`, `\fry` or `\frz`), `\org` is inserted at its current value, which is the old `\pos` when there was none. This keeps the rotation centre.
- Clips aren't touched, because they are absolute.

**chrolo.ChangeAlignment.** This computes the text's four bounding corners and puts `\pos` (or `\move`'s start) at the new anchor. `\move`'s end is the new start plus the old delta. It also has a "Cycle alignment" macro (`\an` + 1, wrapping round) that keeps position. It doesn't handle `\org`.

## Align and distribute (phos.AlignAndDistribute)

This needs video open. It reads each Line's bounding box (AssfPlus `getBoundingBox`).

- **Align.** Relative to one of: the first Line, the last Line, a custom Line, the video frame, the Line's own rect clip (which is then removed), PlayRes or LayoutRes. You tick Vertical (centre, top or bottom edge) and/or Horizontal (centre, left or right edge). Each Line moves by the difference.
- **Distribute.** This needs three or more Lines. The first and last Lines, **in selection (Grid) order**, stay where they are. Each Line moves so the chosen edge or centre is equally spaced.
- **What moves.** Both modes add the same dx and dy to `\pos`, `\org`, `\move`, and rect and vector `\clip`/`\iclip`. A Line without `\pos` gets one from its effective tags.

## Margin position (petzku.PosToMargin)

- **Position to Margin.** This only runs on Lines with `\pos`.
  - Vertical: for `\an7–9` the vertical margin becomes round(y); for `\an1–3` it becomes round(PlayResY − y); `\an4–6` is left alone.
  - Horizontal: the left and right margins keep the current total free width (the "buffer"), shifted to the position. A margin that comes out as 0 becomes −1 so the Style's margin isn't used, and the other margin gets the pixel back. A margin equal to the Style's is written as 0.
  - Then `\pos` is removed, along with any `{}` that leaves.
- **Margin to Position.** It computes x and y from the margins (the Line's margin, or the Style's when it is 0) and the alignment, and prepends `{\pos(%d,%d)}` (**truncated** to an integer), merging it into the start block.

## Phantom (petzku.Phantom)

This works on the **active Line only**. In each `\N`-separated row, it uses `{}…{}` markers, or the edit box's text selection in the cursor variant:
- **Align start:** `beg{}mid{}end` → `HIDE mid SHOW beg mid HIDE end`
- **Align end:** `beg{}mid{}end` → `HIDE beg SHOW mid end HIDE mid`

Here HIDE is `{\alpha&HFF&}` and SHOW is `{\alpha}`. Invisible copies pad the row so that `mid` lines up with the same text in another Line. The start block is kept.

## Numeric shift (ua.Relocator, Teleportation)

- **Fields.** X and Y are separate fields, and negative values are allowed. The same shift applies to each selected Line.
- **What moves.** A checkbox for each kind: `\pos`, both points of `\move`, `\org`, and rect and vector `\clip`/`\iclip` (a vector clip's scale is respected). Drawings (off by default) are rounded to whole pixels.
- **Lines without `\pos`.** On by default, such a Line gets one from its alignment, margins and PlayRes, unless it has `\move`.
- **Output.** Every number in a parenthesised tag is rounded to 2 decimal places, with trailing zeros dropped.

## Cycles

**ua.Cycles.** This has six commands (blur, bord, shad, alpha, an, fsp) with fixed lists in the script.
- It reads the tag from the start block; `\t` bodies are masked. A value on the list goes to the next one, wrapping round. A value not on the list goes to the next larger one, or wraps to the first.
- If the tag is missing, it is added with the list's first value.
- A Line that is commented out, or that ends in `{switch}`, cycles backwards. The Switch macro toggles `{switch}`.
- Note that `\an` is cycled without keeping the position.

**aka.Cycles.** The lists are editable in a config, with an "unanimated" and an "Akatsumekusa" preset. They cover 23 numeric tags, including x/y bord and shad, fs, be, frx/fry/frz, fax/fay, fscx/fscy, and alpha plus 1a–4a. It reads the effective tag rather than the start block only. Reverse comes from an actor flag "switch".

## Copy and Paste with tags (ua.MultiCopy)

**Copy.** This only reads the start block (`^{\\[^}]-}`); inline tags are never copied. The modes:
- `tags`: the whole start block.
- `any tag`: a comma list typed in the text box, taking the first occurrence of each tag. `\t` bodies are excluded.
- `clip`: the first `\clip` or `\iclip`.
- `pos x`, `pos y`, `colour(s)` and `alpha`.

The output is one line per selected Line, empty when nothing matched.

**Paste.**
- **Paste tags** replaces the start block wholesale. If the Line has no start block, the copied block is prepended.
- **Paste extra › any tag** inserts the tags before the block's `}` (or before its first `\t`). A de-duplication pass then keeps the last copy of each tag, so the pasted value wins.

**Counts.** When the counts differ, a dialog offers "loop", "only N" or "all".

**Clipboard.** It goes through a text box, with an optional system clipboard copy.
