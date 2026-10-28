# Legacy observations: run 37024532554

Captured by [legacy-capture](../../../../.github/workflows/legacy-capture.yml) [run 37024532554](https://github.com/altqx/hikari/actions/runs/37024532554) from the legacy wx app at `20d647c4`, under the same conditions as [run 36591631319](../run-36591631319/README.md). `observations.json` is the complete record, including the key script. All earlier cases reproduced byte for byte, so only the new case's output is kept.

| Case | Key script (after opening `tools/legacy-capture/inputs/tag-commands.ass`) | Old app observed | Rewrite |
| --- | --- | --- | --- |
| editor-tag-commands | Return in the grid focuses the editor. Line 1 `abc`: End, Shift+Home, Ctrl+B, Return. Line 2 `{\b1}abc`: End, Ctrl+B, Return. Line 3 `{\i1}abc`: Home, Right ×4, Ctrl+B, Return. Line 4 `{\b1}abc`: Home, Right ×2, Ctrl+B, Return. | `{\b1}abc{\b0}`, `{\b1}abc{\b0}`, `{\b1\i1}abc`, `{\b0}abc`; Return on the last Line appended a Line from 0:00:08.00 to 0:00:13.00 with no text. | The same results (`tag_commands_tests`), and the same appended Line (`shell_tests`). |
