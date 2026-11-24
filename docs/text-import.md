# Import text…

**File › Import text…** turns a plain text file into untimed Lines, as Aegisub does when it opens a `.txt`. Choose the file, then set the options while a preview shows the first 20 Lines as they will be imported. Card: PF-4 of [#1214](https://github.com/altqx/hikari/issues/1214), parity row 18.

**File › Open** on a `.txt` is unchanged: it opens the file as plain text lines and saves it back as it was.

## Options

Each line of the file becomes one Line, starting and ending at 0:00:00.00.

- **Actor separator** (default `:`). The text before the first separator is the Line's actor, trimmed. A line that starts with a space or tab never takes an actor. Leave it empty to read no actors.
- **Comment starter** (default `#`). A line starting with it becomes a comment Line, without the starter and with no actor. Leave it empty to read no comments.
- **Keep blank lines as comment lines.** Off by default, so blank lines are skipped. A line left with no text after its actor is a comment Line too.
- **A line without an actor keeps the last one** (on by default, as in Aegisub). Off: such a Line has no actor.

The choices are kept in `textImport.options`.

The file must be UTF-8, with or without a byte order mark, and at most 8 MB. An Aegisub timecodes file (`# timecode…`) is refused, with Video › Open timecodes… named instead. The dialog says why when it can't import, and Import stays off.

## Where the Lines go

- **After the last line of the open file:** the default when an ASS file is open. Every Line takes the chosen **Style**, which defaults to the current Line's Style. The whole import is one undo step, "Import text", and the new Lines are selected afterwards.
- **A new file:** opens untitled with unsaved work, starting as File › New does, with every Line in Default. Save as suggests the text file's name with `.ass`.

## Not yet

- An **Encoding** choice, once Reopen with encoding… (PF-2) brings the shared encoding list.
- Aegisub's Tool/Import/Text options are not read by Import settings.

## Tests

- `hikari_core_text_import_tests`: actors, comments, blank lines, each option, and the refused files.
- `hikari_application_text_import_tests`: the new file and the one-step insert.
- `shell_tests` `importTextAddsLinesToTheOpenFileInOneStep` and `importTextOpensANewFileAndRemembersItsOptions`.
