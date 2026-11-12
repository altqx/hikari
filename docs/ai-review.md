# AI review

Every subtitle change an AI caller applies waits for the user to keep it or undo it ([ADR 0024](adr/0024-hikari-ai.md), [AI2 #430](https://github.com/altqx/hikari/issues/430)). Qt owns the review: the sidecar and MCP clients only call `document.apply`.

This card ships the review of applied changes (Auto and Full access). Manual mode, where nothing is applied until Keep, comes with the agent panel ([AI7 #435](https://github.com/altqx/hikari/issues/435)): staged proposals, the proposed-version overlay, stale proposals and the close-tab prompt.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/application/ai_review.{h,cpp}` | `AiReview`: changes, turns, Keep, Undo and checkpoints, per Document. No Qt. |
| `src/application/ai_review_files.cpp`, `file_diff.{h,cpp}` | Staged file writes: Keep, Undo, checkpoints, and the line diff the panel shows. No Qt. |
| `src/application/word_diff.{h,cpp}` | The word diff the Grid and the Line editor draw. |
| `src/api/write_operations.cpp` | `document.apply` records a change when the caller has `reviewed`. |
| `src/app/review_controller.{h,cpp}` | `ReviewController`: the panel's turns, the Line editor strip, the Grid marks, and the actions. |
| `src/ui/ReviewPanel.qml`, `ReviewStrip.qml` | The Review panel and the strip above the Line text. |

## Changes and turns

- **A change** is one `document.apply` from a caller with `Caller::reviewed`: one history step, named `<caller label>: <name>`. Its `name` is the summary the panel and History show. Its items are the Lines, Styles and Script Info properties it touched. A Line item keeps the Line before and after, and where it stood.
- **Callers.** The sidecar's caller is `{id: "ai", label: "AI", reviewed: true}`. An MCP connection's caller (`McpServer::callerFor`) is `{id: "mcp:<connection>", label: <client name>, reviewed: true}`, so its changes land in the review too, in a turn of their own. [Inline actions](ai-inline-actions.md) write as `{id: "ai:inline", label: "AI", reviewed: true}`, one turn per run. Plugins don't set it.
- **A turn** groups the changes made for one user message. A caller's write opens a turn when it has none open, or when the user edited that Document since the caller's last step there. AI7 opens one per message with `beginTurn`. Each turn starts with a checkpoint.
- **Waiting.** An item waits while the Document still differs from its before. Undoing it through History hides it; Redo brings it back. An item is **edited since** when the Document no longer holds what the newest AI change on it left.
- **A staged file.** A reviewed caller's `files.write` never reaches the disk. It stages in the caller's open turn, and the AI reads its own staged copy back (`files.read`). A later write to the same path replaces the row and keeps the first before. The disk changes only when the user keeps it, in every Project, trusted or not ([#903](https://github.com/altqx/hikari/issues/903), owner, Q473). A write to a file open in a tab is refused (`open_document`); the AI edits it with `document.apply`. A file is **changed on disk** when the disk no longer holds the before.

## Keep, Undo and checkpoints

| Action | Effect |
| --- | --- |
| Keep | Clears the marks; no history step. |
| Undo | One step per action, named for what it undoes (`Undo AI change to Line 12`, `Undo AI: Tightened`, `Undo all AI changes`). A Line goes back to exactly its before copy, its change version too (`ChangeMark::Restored`). Deleted and moved Lines go back in place. |
| Undo, edited since | Asks first. "Undo anyway" drops the user's edit; otherwise the Line stays. On a whole change the question says how many of its Lines were edited since, and "Undo anyway" undoes the whole change as one step; Cancel leaves it all. Undo all asks too and also offers "Leave my edits", which undoes the rest. |
| Restore checkpoint | Undoes every change from that turn on, kept ones too, in every tab, one step per tab. The user's edits since stay unless "Also undo my edits" is ticked. |
| Keep a file | Writes the AI's version. A file changed on disk asks first; "Keep anyway" replaces the user's version. A failed write leaves it waiting. |
| Undo a file | Drops the staged copy; the disk is never touched. |
| Keep all, Undo all | Also keep or drop every staged file. Keep all leaves files changed on disk waiting. |
| Restore checkpoint, files | Drops the turn's staged files and writes kept ones back to what they were before the turn (a new file is removed). A kept file the user changed since stays unless "Also put back n file(s) changed on disk" is ticked. |
| Quit with staged files | Asks: Keep all, Discard all or Cancel. Files Keep all can't write stay, and the window stays open. |

## In the window

- **Grid:** a 3-pixel stripe in the `#` column, the accent hue while waiting and the warning hue once edited since, and the changed words with a tinted background and an underline (`ReviewRole`, `ReviewMarksRole`).
- **Line editor:** for the active Line, "Changed by AI", the change's name, Undo, Keep, "n of m" with Previous and Next, and the word diff with removed words struck through.
- **Review panel** (panel id `Review`, registry 13): the active tab's changes by turn, newest first, each turn with Restore checkpoint, each change and item with Undo and Keep. Other tabs with waiting changes are listed under "Also to review". A turn's staged files follow its changes, each with its path, "New file" or "Changed" and the line counts, Undo and Keep; clicking one shows its diff, or "Not a text file, or too large to compare." past 256 KiB.
- **Tabs:** a badge with the waiting count. **Status bar:** "n AI change(s) to review", which opens the panel.
- **History:** an AI icon on AI steps, and a "Before turn N" divider above each turn's first step.

## Commands

| Id | Symbol | Default |
| --- | --- | --- |
| 5245 | `GLOBAL_KEEP_AI_CHANGE` | Ctrl+Shift+Enter |
| 5246 | `GLOBAL_UNDO_AI_CHANGE` | Ctrl+Shift+Backspace |
| 5247 | `GLOBAL_NEXT_AI_CHANGE` | Ctrl+Alt+Down |
| 5248 | `GLOBAL_PREVIOUS_AI_CHANGE` | Ctrl+Alt+Up |
| 5249 | `GLOBAL_KEEP_ALL_AI_CHANGES` | none |
| 5250 | `GLOBAL_UNDO_ALL_AI_CHANGES` | none |
| 5251 | `GLOBAL_SHOW_AI_REVIEW` | none |

They are under Edit > Review. Keep, Undo and Next AI change, with "Show in Review panel", head the Grid's context menu on a Line with a waiting change.

## With AI off

`application::aiCommand` names every command whose symbol has an `AI` token. With "Turn off all AI features" on:

- `HotkeysController` keeps their stored keys but neither binds nor lists them;
- Edit > Review and the Grid entries are not in the menus;
- the Review panel is closed, out of View > Panels and of `panels.list`, and its body is not built;
- no marks, badge or status show.

The `Review` dock itself stays registered with the layout, so turning AI back on needs no restart and a saved layout keeps its place.

## Tests

- `hikari_application_ai_review_tests`: per-Line waiting and exact Undo, Keep with no step, inserted, deleted and moved Lines, Styles and Script Info, edited since, History undo and redo, turns, other callers, checkpoints across tabs, closing a Document, the Line target.
- `hikari_application_word_diff_tests`.
- `hikari_application_ai_review_files_tests`: staged files wait, Undo, a later write, changed on disk, a failed write, Keep all and Undo all, turns, checkpoints; the file diff.
- `hikari_api_tests`: a reviewed caller's write waits in the review; a plugin's doesn't. An AI file write stages, reads back, and leaves the disk until kept; open documents and excluded paths are refused.
- `hikari_ui_api_host_tests`: the Grid marks, the strip and Keep; Undo and the edited-since question, on an item and on a whole change; Restore checkpoint; file rows, their diff, Keep, Keep anyway, Keep all and Undo all, restore, and the quit question; AI off takes the commands, menus and panel away and AI on brings them back.
- `hikari_ui_ai_features_tests`: every command with an AI token is one the switch takes.
