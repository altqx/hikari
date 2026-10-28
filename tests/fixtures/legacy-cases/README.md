# Legacy cases

The inputs the legacy probes and drivers ran, kept because the tests replay the same cases against the observations in [`legacy-observations`](../legacy-observations):

- [`inputs/`](inputs): the case files (`*-cases.txt`) and the documents the capture plan opened.
- [`plan.json`](plan.json): the capture plan. The tests read its `ui` cases (Find and replace, the Spellchecker walk) and its `automation` dialog steps.
- [`automation/capture-probe.lua`](automation/capture-probe.lua): the Lua probe; the automation tests run it in the rewrite's host.
- [`automation/document-scripts/`](automation/document-scripts): the S4 Document scripts.

The probes, drivers and the legacy capture workflow were removed in LC1 ([#459](https://github.com/altqx/hikari/issues/459)). To rerun a capture, use [`tools/legacy-capture`](https://github.com/altqx/hikari/tree/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture) at 350c1152a. Its commands name `tools/legacy-capture/inputs` and `plan.json`, which are these files.
