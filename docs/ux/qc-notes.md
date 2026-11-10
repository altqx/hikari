# QC notes

A native version of arch1t3cht's Note Browser 1.3.6, whose notes live in the subtitle file. The owner approved the mockup and recommendations 1–9 on 2026-11-04 ([#577](https://github.com/altqx/hikari/issues/577#issuecomment-6619602171)), then the addendum, recommendations 10–18 ([comment](https://github.com/altqx/hikari/issues/577#issuecomment-6619925977)). The panel is `src/ui/QcNotesPanel.qml` with `QcNoteEditor.qml`, its controller `src/app/qc_notes_controller.h`, and the parsing, storage and line matching `src/application/qc_notes.h`.

## Where it opens

- Subtitles › QC notes… (5241, no default key) and View › Panels › QC notes. The dock id is `QcNotes` (panel registry 12). No built-in layout holds it; it first opens to the right of the Grid.
- Next QC note (5242), Previous QC note (5243) and Add QC note (5254) work from anywhere and have no default keys. Add QC note… is also in the Grid's context menu, at that line's start.

## Storage

- One `[Script Info]` key per note, in time order, rewritten as a block after the other properties: `Hikari QC <n>: h:mm:ss.cc,section,open|done,text`.
- Times are kept to the centisecond. A section can't contain a comma (it becomes a space) and is "General" when empty. A line break in the text is `\N`.
- Other properties and unchanged notes keep their bytes. A `Hikari QC <n>` key whose value isn't a note is left alone, and its number is skipped.
- Why Script Info: Aegisub drops an unknown `[Section]` and unknown Project Garbage keys, and expires extradata no line uses; it keeps unknown Script Info keys in order. libass ignores unknown Script Info keys. See the addendum for the source references.
- Each change is one undo step: Add QC note, Edit QC note, Delete QC note, Mark QC note done, Mark QC notes not done, Delete QC notes, Import QC notes. Effect and Text are never touched.
- Only ASS and SSA files can keep notes. With an SRT or another format open, the panel says "Only ASS and SSA files can keep QC notes. Save this file as ASS to add notes." Add, import and paste are disabled there, and Add QC note opens the panel to show that message ([#808](https://github.com/altqx/hikari/issues/808)).
- Shift times and the postprocessor don't move notes. Export as writes no notes to SRT or the other formats without Script Info; SSA files keep them.

## The list

- Each row shows Done, the time, the section (with All chosen), the text and the Grid line number, or "No line". Done notes are dimmed and struck through.
- Section chips filter the list: All, then each section with its count ("7 of 9" with Hide done). Hide done (`qcNotes.hideDone`) removes done notes.
- Clicking a note, Enter, or Next / Previous selects its line and moves the video to the note's time when the line is on screen then, otherwise to the line's start. Next and Previous follow the chosen section and Hide done, don't wrap, and say "No more notes" at the end (a notice when the panel is closed). Without a current note they start from the video time, or the active line's start.
- Keys: ↑ ↓ move, Enter goes to the note, Space marks it done, F2 edits, Del deletes, Esc goes back to the Grid.
- A screen reader reads a note as "3:24, TLC, line 57, …, not done".

## Which line a note marks

The line on screen at the note's time; with several, the one that starts latest. With none, the line that starts nearest, less than 15 seconds away, as in Note Browser. Comment lines are skipped. The match is worked out again after every change, so a note stays with its moment.

## In the Grid

Lines with notes carry a flag in the number column, which widens while any line has one. The current note's flag is filled. Hovering the number lists the notes ("0:03:24 TLC: …"), and the row's accessible text adds "2 QC notes". The flags follow the panel's section and Hide done.

## Adding and editing

- Add note, the Add QC note command or the Grid's Add QC note… open the editor at the top of the list. The time is the video's, or the active line's start without a video; Video time and Line start reset it. The section is the last one used (`qcNotes.lastSection`, first "General"); the combo box lists the sections and takes a new one.
- Double-click or F2 edits a note in place, with Done and Delete.
- Ctrl+Enter saves, Esc cancels. Add and Save are disabled while the note is empty or the time doesn't read.

## Import and export

- The ⋯ menu: Import notes file…, Import notes from the clipboard, Export notes…, Mark all as not done, Remove all notes…. A file dropped on the panel is imported, and the empty panel offers Import notes file… and Paste notes (disabled when the clipboard has no notes).
- Import reads Note Browser's format (a note starts with `h:mm:ss` or `m:ss`, fractions kept; `[Section]` lines group the notes after them; notes before the first section go into General) and mpvQC files (their category becomes the section). When the file already has notes, a question offers Add (the default, skipping notes with the same time, section and text) or Replace. The result says "Imported N notes.", how many were already there, and how many have no line.
- Export notes… writes Note Browser's format: Include done notes (off) and Only the chosen section.

## Departures from Note Browser

- Multi-line notes follow Note Browser's documented rule (a blank line ends a note unless the line before it was indented). Note Browser 1.3.6's code drops the lines after the blank line.
- Digits right after the separator stay in the text: "0:05 - 2 frames early" keeps "2 frames early".
- Notes are written into the file instead of only being loaded from a .txt; there is no Reload and no "file changed" message.
