# Legacy observations: run 37034136343

Captured by [legacy-capture](../../../../.github/workflows/legacy-capture.yml) [run 37034136343](https://github.com/altqx/hikari/actions/runs/37034136343) from the legacy wx app at `20d647c4`, under the same conditions as [run 36591631319](../run-36591631319/README.md). `observations.json` is the complete record, including the key scripts. All earlier cases reproduced byte for byte, so only the two new cases' outputs are kept.

| Case | Key script | Old app observed | Rewrite |
| --- | --- | --- | --- |
| editor-tag-commands-srt (`tag-commands.srt`) | Return in the grid, then cue 1 `abc`: End, Shift+Home, Ctrl+B, Return; cue 2 `<b>abc</b>`: End, Ctrl+B, Return | `<b>abc</b>`; `<b>abc<b>` (the closing tag next to the caret becomes an opening one); Return on the last cue appended cue 3, 00:00:04,000 to 00:00:09,000 | The same (`tag_commands_tests`, `srt_tests`) |
| editor-tag-commands-microdvd (`tag-commands.sub`) | Line 1 `abc\|def`: Home, Right ×5, Ctrl+B, Return; line 2 `{y:b}abc`: Home, Right ×2, Ctrl+B, Return | `abc{Y:b}\|def` (the tag goes at the `\|` itself); `{Y:b}abc`; Return on the last Line appended `{40}{160}` at the legacy 23.976 fps default | The same tags; the appended Line ends at frame 160 with the Document's rate 24000/1001, and is left open (`{40}{}`) while the rate is unknown (C01-fps-isolation) |
