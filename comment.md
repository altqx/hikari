**AI12 gate: "Run this Lua script?" in the agent panel.** The card has no design gate, but approving AI-written Lua is a new card in the panel, so here is a mockup before any UI code.

| Light | Dark |
|---|---|
| ![light](https://raw.githubusercontent.com/altqx/hikari/evidence/w8-ai12/sheet-light.png) | ![dark](https://raw.githubusercontent.com/altqx/hikari/evidence/w8-ai12/sheet-dark.png) |
| **High contrast white** | **High contrast black** |
| ![hcw](https://raw.githubusercontent.com/altqx/hikari/evidence/w8-ai12/sheet-highContrastWhite.png) | ![hcb](https://raw.githubusercontent.com/altqx/hikari/evidence/w8-ai12/sheet-highContrastBlack.png) |

Source: `panel.template.html` on `evidence/w8-ai12` (built from AI7's panel mockup).

How it works: the sidecar only writes the text. A new HA1 write operation, `automation.run {name, script, macro?}`, carries it to Qt. Qt always opens this card, in every mode, before anything is saved or loaded. On **Run**, Qt saves the file to `<user folder>/ai/scripts/`, loads it into the Lua helper and runs the macro on the editing target as an ordinary Automation macro. The agent gets the outcome and the log back.

Questions. Answer "yes" to take the recommendation.

1. **Asks every run, in every mode.** Manual, Auto and Full access all show this card. The judge never decides it, there is no "Allow in this thread", and a rerun of the same script asks again. Buttons: **Run**, **Don't run**, **Don't run and tell the agent…**. *Recommend yes.*
2. **The script shows in full.** The first 12 lines show in monospace with line numbers, and "Show all N lines" opens the rest in place. A script over 256 KiB is refused before the card opens. *Recommend yes.*
3. **Applies like a macro, not through the review.** A run is one undo step, "Fade signs", and Ctrl+Z undoes it. It does not stage in the Keep/Undo review: the review has nothing to show while a macro runs, and you have already read the script. While it runs, the usual Automation progress card offers Force stop, and the thread shows a "Running…" row with Force stop too. *Recommend yes.*
4. **DependencyControl.** When the script calls `DependencyControl{…}`, the card lists each module: installed ones with their version, and a warning for each missing one. It does not install anything; installing stays a separate action that always asks. *Recommend yes.*
5. **Scratch folder.** Saved as `ai/scripts/<name>.lua` in the user folder, overwriting an earlier script with the same name. It is never added to Autoload or to the subtitle's Script Info, and it is unloaded after the run. "Open" on the thread row opens the saved file in your script editor. *Recommend yes.*
