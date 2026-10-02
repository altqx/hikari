# Legacy observations: run 37011681370

Captured by [legacy-capture](../../../../.github/workflows/legacy-capture.yml) [run 37011681370](https://github.com/altqx/hikari/actions/runs/37011681370) from the legacy wx app at `20d647c4`, under the same conditions as [run 36591631319](../run-36591631319/README.md). `observations.json` is the complete record. The C03-preservation, supplement-TLMode and supplement-format-precision outputs were byte-identical to run 36591631319, so only the new case's output is kept here.

| Case | Route | Old app observed | Disposition |
| --- | --- | --- | --- |
| srt-crlf-blank-lines (input `tools/legacy-capture/inputs/crlf-two-cues.srt`) | Save As (SRT) | Cue 1 `Hello` followed by a CRLF blank line is saved as `Hello` plus an extra blank line: the cue text became `Hello\N`. Cue 2 (no blank line after it) is unchanged. A UTF-8 BOM and a trailing blank line were also added, as in supplement-format-precision. | Changed by approved [C82-srt-blank-break](../../../../docs/qt/compatibility-decisions.md): blank lines add nothing, so the rewrite reads `Hello`. The SRT test compares against these bytes. |
