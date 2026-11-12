# Project tiers

A Project ([ADR 0030](adr/0030-open-a-folder-as-a-project.md)) can keep its own copy of shared things in its `.hikari/` folder, beside the user folder's copy. Each shared thing is a **tier store**, and each store combines its two tiers by one rule. The stores are listed in `tierStores()` (`src/application/tiers.cpp`).

## The three rules

- **Hidden by name.** A Project item hides the user's item of the same name. Both files stay on disk, and the user's item is still listed, muted, with "Hidden by the project's copy". Choosing it opens it read-only, so its copy can be copied or moved but not edited. Name lookups resolve the Project first.
- **Merged.** Both tiers apply together.
- **Per key.** The Project's value replaces the user's, key by key.

Without a Project nothing changes: only the user's tier is listed, with no chips.

## In the lists

- **Chips.** Each row has a **Project** chip (the `project` icon) or a **Mine** chip (the `mine` icon), and the list groups "This project" first, then "Mine".
- **Copy and Move.** A row's ⋯ menu has Copy to project or Copy to mine, which keep the source, and Move to project or Move to mine. A name the other tier already has goes through the clash review: Replace, Rename (a taken name gets "<name> (2)"…) or Skip, with the All row.
- **Where New saves.** While a Project is open, New and Save as (Save as preset in the preset lists) are split buttons: **Save to project**, the default, or **Save to mine**. The choice is kept per list in `state.json` as `workspace.saveTiers`, one `<list>\t<project|mine>` row each.
- **Nothing is written early.** Opening a Project adds no files. A tier folder such as `.hikari/catalogs/` is made the first time something is saved into it.

## The stores

| Store | Project copy | Rule | Notes |
| --- | --- | --- | --- |
| Style catalogues | `.hikari/catalogs/<name>.sty` | Hidden by name | The Style manager's Catalogue box. `Last Style Storage`, `styles.lastCatalog` and Convert's style catalogue (`convert.styleCatalog`) name a catalogue, which resolves the Project first. The user's `Default` can't be deleted or moved, only copied; the Project's can be deleted, and the user's shows again. |
| Script dialog presets | `.hikari/presets.toml`, `[automation.dialogPresets]` | Hidden by name | Keyed by dialog, so a Project preset hides only the user's preset of the same name in the same script dialog. The dialog's ⋯ › Presets menu marks each preset Project or Mine, a hidden one is greyed out, and the Copy to and Move to submenus sit under Delete preset. |
| Shift times presets and profiles | `.hikari/presets.toml`, `shiftTimes.presets` | Hidden by name | Legacy profiles become presets on the first start, so they share this store. The Preset box groups the presets by tier, and its ⋯ menu copies or moves the shown one. A hidden user preset still applies but can't be renamed or deleted. `shiftTimes.preset`, the name the panel shows, resolves the Project first. |
| Postprocessor presets | `.hikari/presets.toml`, `postprocessor.presets` | Hidden by name | As Shift times presets. The Timing layout's preset (`postprocessor.timingPreset`) resolves the Project first. |

**User-only stores** have no Project tier and never read a Project file: hotkeys; AI providers, keys and budgets; AI threads; recent files and window state; Hunspell `.dic` and `.aff` files.
