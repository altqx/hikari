# QC notes in Quiet

How the QC notes panel looks in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). What it does (storage, line matching, keys, import and export) is [qc-notes.md](qc-notes.md)'s; this document says how it looks and where its messages go. While it has focus, or for 5 minutes after a note is added, Discord shows Reviewing ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Batch 4 lgtm", Q746–Q750; see [Decisions](#decisions)). The app doesn't change until the code card lands.

## The panel

In the right side bar, 380 wide.

- **Header.** "QC notes", then at the trailing end **Previous QC note** and **Next QC note** as icon buttons named on hover (`move-up`, `move-down`) with "5 of 7 open" in `muted` (Q747), **Add note** (small, `add` glyph) and ⋯. Next and Previous do what the commands do.
- **Filters.** The section chips, 22 high, radius 11, on `raised`: All, then each section with its count in `muted`; the chosen one has the `select` fill and a 1-unit `accent` edge. **Hide done** is the last chip, at the trailing end.

## The rows

Q746. One row per note, in a 4-unit inset, radius 4:

1. the Done box (14 units; `accent` fill when done);
2. the time in `muted` monospace, 70 wide;
3. the section in `muted` small text, then the note's text;
4. at the trailing end, the Grid line number in `muted` monospace, or "No line".

Done notes are at 55 % and their text is struck through. The current note has the `select` fill.

## In the Grid

Q748. Lines with notes show the `qc-notes` glyph, 12 units, in `accent` right after the number; the current note's line at full strength, the others at 60 %. The # column widens for it as [qc-notes.md](qc-notes.md#in-the-grid) says.

## Adding

Q749. The editor opens at the top of the list as a card on `raised`, radius 6: the time field, **Video time** (quiet), the section picker; the text field with the `focus` edge; then "Ctrl+Enter saves · Esc cancels" in `muted` with **Cancel** (quiet) and **Add** (primary). Editing in place uses the same card.

## Messages

Q750.

| State | Shown |
| --- | --- |
| No notes | The `qc-notes` glyph, "No QC notes in this file", in `muted` "Add one at the video's time, or import a Note Browser or mpvQC file. You can also drop one here.", **Add note** (primary), **Import notes file…** (quiet), **Import from the clipboard** (quiet; disabled with nothing to import) |
| Not ASS or SSA | The `qc-notes` glyph, "Only ASS and SSA files can keep QC notes." and "Save this file as ASS to add notes." with **Save as ASS…**; Add note disabled |
| After an import | One line under the header with the `import` glyph: "Imported 12 notes · 2 were already there · 1 has no line", and a close button |

"No more notes" from Next and Previous stays a notice when the panel is closed.

## Discord

Presence shows **Reviewing** (`reviewing`). Its art is the `qc-notes` glyph on #FFC6E0 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Batch 4 lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6943248299)):

| Q | Decision |
| --- | --- |
| Q746 | One row per note: done box, time, section before the text, the Grid line at the end; done notes dimmed and struck through |
| Q747 | Previous and Next QC note as icon buttons in the header with "N of M open" |
| Q748 | The qc-notes glyph in accent after the line number in the Grid |
| Q749 | The editor is a raised card at the top of the list |
| Q750 | Import results show under the header; no notes and a non-ASS file are the panel's states |
