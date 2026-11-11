# AU4: cleanup script actions

What [AU4 #839](https://github.com/altqx/hikari/issues/839) takes native from three user-installed scripts, read (not run) on 2026-11-11. Sources: ua.ScriptCleanup and ua.Masquerade from [unanimated/luaegisub](https://github.com/unanimated/luaegisub), l0.ASSWipe from [line0-Aegisub-Scripts](https://github.com/TypesettingTools/line0-Aegisub-Scripts). The owner chose the set on 2026-11-11: comments in Lines, redundant tags and empty Lines.

**Native** means the action is a checkbox in Remove tags…. **Elsewhere** means it's another job that HikariSub has, or would have, somewhere else. **Script** means it stays in the script.

## ua.ScriptCleanup

| Action | Where |
|---|---|
| Remove comments from lines | Native: Remove comments (ledger D3) |
| Clean up tags | Native: Drop overridden tags (D2) and Clean Tags rules |
| Delete empty lines | Native: Delete lines left empty |
| Remove all colour tags, alphas, rotations, size/scaling, shadows, perspective, `{\tags}` | Native: the tag families |
| Kill-column tags (`\bord`, `\blur`, `\fs`, `\i`, `\pos`, `\t`, …) and its start/inline scope | Native: the tag families and the Blocks choice |
| Remove inline tags | Native: Blocks › Inline blocks with every family |
| Clear Actor, Clear Effect, Raise layer, Delete unused styles | Elsewhere: Grid columns and Style manager |
| Fix 1-frame gaps/overlaps | Elsewhere: Timing postprocessor |
| Delete commented lines | Elsewhere: Select lines, then Delete |
| Remove TS timecodes | Script (a `{TS …}` block is also removed by Remove comments) |
| Clean up transforms, Try to fix alpha / colour tags, Fix spaces | Script |
| Remove parentheses, Remove hard spaces, Remove line breaks, Hide tags, Print info, "only from `\t`", "except last", inverse | Script |

## l0.ASSWipe

| Pass | Where |
|---|---|
| Merge consecutive tag blocks (karaoke excepted) | Native: Merge adjacent tag blocks; Clean Tags rules keep `\k` blocks apart |
| Clean tags (levels 1–4), tag sort order, tags to keep | Native in part: Clean Tags rules and Drop overridden tags; ASSFoundation's own levels and sort order stay in the script |
| Strip comments | Native: Remove comments |
| Remove junk (unknown tags) | Native: the Other tags family |
| Remove invisible Lines, filter ineffective clips, purge contours | Script: they need the renderer's bounds |
| Combine Lines, extradata filter, drawing fixes and scaling | Script |

## ua.Masquerade

| Action | Where |
|---|---|
| `tagmerge`: join `{\…}{\…}` within a Line | Native: Merge adjacent tag blocks, byte for byte (`hikari_core_cleantags_parity_tests`) |
| Merge Tags: merge selected Lines with the same text into one | Script (ledger D1) |
| Masks, alpha shift, and the rest | Script |
