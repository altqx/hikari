# Find panel

One panel, **Find**, replaces the Search dock's three tabs (Find, Replace and In files). The owner approved the mockup and its ten recommendations on 2026-11-02 ([#597](https://github.com/altqx/hikari/issues/597#issuecomment-6569999210)). The panel is `src/ui/SearchTool.qml`; the search itself is `FindReplace` in `src/application/find_replace.h`.

## Layout

- **The find row** is always shown:
  - the ▸ toggle that shows or hides Replace;
  - the find field with **Aa** (Match case) and **.\*** (Regular expression) inside it;
  - the match count;
  - Find previous, Find next, Find all, and Options.
- **The replace row** holds the replace field, Replace and Replace all. With Search in: Folder it holds only "Replace in N files…", drawn in danger. It still asks first and still copies each file to ReplaceBackup.
- **Options** fold behind one button. While they are folded, a dot on the button and a row of chips under the field list every setting that is not at its default, and the button's accessible description reads them out.
  - **Search in:** This document · Open documents · Folder. Folder shows Folder, Files, Subfolders and Hidden folders, and hides Lines and Styles. Lines shows only for This document; the other scopes search every line.
  - **Field:** Text · Style · Actor · Effect.
  - **Lines:** All · Selected · From selected.
  - **Styles:** the Choose styles popover; "All styles" when none are chosen.
  - **In text:** All text · Outside tags · Inside tags (legacy's exclusive Skip tags / Skip text pair).
  - **Match at:** Anywhere · Start · End (legacy's exclusive Beginning of text / End of text pair).
  - **Include comments.**
- **Results** appear on the right when the panel is at least 640 pixels wide, and below the rows when it is narrower.
  - A summary leads them: "12 matches in 9 lines", or "5 matches in 3 files" for a folder.
  - With Replace shown, each match is struck through and followed by the replacement.
  - "Replace N checked" uses the replace row's text; the results have no replace field of their own.

## Behaviour

- **Ctrl+F** (Find…) opens the panel with Replace as it was last left. **Ctrl+H** (Replace…) opens it with Replace shown. Pressing the key again while the panel has focus closes it; Ctrl+H with Replace folded unfolds it instead.
- **The count** is for the editing target: "3 of 12", "5 matches" or "No matches". It follows what Find next walks, with the same Field, Lines, Styles, In text, Match at and comment options, and updates as the text, the options or the document change. Screen readers hear it announced ("3 of 12 matches"). Other scopes count only when Find all runs.
- **Find previous** (Shift+Enter, Shift+F3; command 5213) walks back from the current match, or from the active line when there is none.
- **Wrapping:** both directions go on from the other end without asking. This drops legacy's "Reached the end. Search from the beginning?" question (the capture cases that answered it are named departures). A search that finds nothing still says "Could not find “…”".
- **Regular expression errors** show under the find field in danger, and are its accessible description. While the pattern is invalid nothing is searched: Find, Find all and Replace are disabled, and Enter does nothing.
- **Saved settings:** every option is saved in `find.options` with legacy's bits, so the Folder options and Lines survive together. A legacy file that sets both bits of a pair reads as End and as Outside tags.

## Keys

| Key | In the panel |
| --- | --- |
| Enter, F3 | Find next |
| Shift+Enter, Shift+F3 | Find previous |
| Alt+Enter | Find all |
| Enter in the replace field | Replace, then go to the next match |
| Ctrl+Alt+Enter | Replace all |
| Alt+C, Alt+R | Match case, Regular expression |
| Up, Down | The recent finds or replacements in that field |
| Esc | Back to the Line editor; the panel stays open |

The tab order is the fold toggle, the find field, Match case, Regular expression, Find previous, Find next, Find all, Options, the replace field, Replace, Replace all, the options from top to bottom, the results, Check all, Uncheck all, and Replace checked.
