# OP1 Macro chains: end-to-end run

HikariSub built from w8-op1 (HA2b #1116) on Linux, under vx11, with a fresh user folder and the plugin installed from hikari-official-plugins `op1-macro-chains`. Fixture macros: `E2E/Tag` (a dialog with `tag` and `times`), `E2E/Upper` (no dialog) and `E2E/Fail` (always fails).

Record, edit, replay and import were run on a build of #1116 before Q6. Learning and the failure path were run on #1116 head 0fb1b2202, with `run_macro` answering with its run's `dialogs`.

| Shot | What it shows |
| --- | --- |
| 14-tag-filled | Recording: Tag's dialog answered `#` × 2 |
| 16-after-upper | Recording: Tag and Upper are steps |
| 17-saved | Stop and save: the chain is in the library |
| 26-edited | Editor: `times` set to Ask, slot 1 |
| 30-replay-filled | Run chain slot 1: one merged dialog asks `times` (3) |
| 31-replayed | Result `ONE###` |
| 33-history | History: one step, "Macro chains: Signs" |
| 34-undone-once | One Undo restores `one` |
| 48-import-dialog | Import of `arch.AegisubChain.json`: what it found, before adding anything |
| 49-imported | Imported chains; "Old" names its missing macro |
| 62-fade-merged | First run of the imported "Fade fix": its one Ask field |
| 64-library-later | After that run both steps learned (chains.json: `learn` gone, Tag's real key `888cb51fcefaa0cb` and controls with min/max); the row no longer says it's imported. It stayed learned after a restart. |
| 67-stopped | "Breaks" (Upper, then Fail) stops at step 2 with the macro's message, and offers Undo the chain |
| 68-chain-undone | Undo the chain: line 1 is `one` again, the tab unmodified |
