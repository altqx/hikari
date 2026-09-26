# Preferences, shortcuts and import review — prototype 52

**Throwaway, memory-only UX proposal for [#52](https://github.com/altqx/hikari/issues/52).** Open [settings-import.html](settings-import.html) directly in a browser. No server, dependencies, configuration directory or account is needed. Reload resets everything; the URL only remembers arrangement A/B. No file picker, storage API, network request, script execution, association registration or actual profile mutation exists.

The question is whether users can identify ownership and provenance, inspect conflicts, choose replacements and recover without confusing an import preview with an applied change. The import transaction and routing behavior illustrate the [settings/import design ticket](https://github.com/altqx/hikari/issues/44); they are not accepted mechanics or native implementation evidence. Per-user default storage with explicit portable mode is already accepted through the distribution policy and is not another vote here.

## Arrangements

- **A — Page task dialog** (`?variant=A`, default): a Classic page rail, compact preference rows and one source/current/disposition import table. Best candidate when scan speed and direct comparison matter.
- **B — Review workbench** (`?variant=B`): horizontal page navigation, preference cards and an import review card per item. Source/current/choice stay together, at the cost of more scrolling.

Both use the accepted Compact Studio palette and bounded Preferences task-dialog convention, placed inside an illustrative Classic shell. They share state when switched. The fixed prototype bar is separate from the proposed application; its arrows work with the pointer or when focus is outside interactive controls. Text fields, selects and buttons keep their normal arrow behavior. HTML keyboard affordances do not establish native Qt focus, input-method or screen-reader behavior.

## Walkthroughs and free play

Each walkthrough tab resets to a known synthetic starting state. Buttons perform actual in-memory transitions and enable the next step. The live status and expandable state view expose active destination values, origins, draft changes, receipt count, source revision and staging state.

1. **Keep & replace:** build a preview, choose only audio offset and personal dictionary replacements, then apply. Other current values stay; the receipt lists the reviewed replacements. Preference drafts are separate from the active profile; Apply/Discard and individual/page resets show their scope.
2. **Repeat & rollback:** import the offset, make the later 90 ms user edit, repeat the receipt, then preview and confirm rollback. Repeat is a no-op while the receipt is active. Rollback displays replaced values and binding differences, restores the full pre-import destination as a new generation, and retains source input. After rollback the old receipt is inactive: repeat does not silently reapply it.
3. **Stale & interrupted:** simulate an interrupted stage, change the source, attempt the old plan, then rebuild. An incomplete stage cannot activate; source/destination revision mismatch blocks the old preview. Changing preferences after preview demonstrates the destination mismatch too.
4. **Shortcut overlap:** select global Find, propose Ctrl+Space, inspect its conflict with the Audio example, and retain overlap for exploration. Inspect Grid/Audio focus, text/IME consumption, modal ownership, protected-reference attempts and active transport. The inspector exposes candidates rather than inventing a registration-order winner. Switch, replace, reuse, unbind, reset and cancel are independently available. Chords use the shown canonical spelling; this toy editor is not a key-event normalizer.
5. **Excluded & unresolved:** inspect legacy theme exclusion alongside preserved ASS color history, rules and personal words. Missing paths, same-basename macro ambiguity, the four-binding file, timing-profile interpretation and unknown fragments remain held in the report. Applying with no replacements makes no destination changes.

Choosing source A/B is explicit. Neither represents the real machine nor wins by modification time. The snapshot is a frozen set of illustrative records; source tokens are **not cryptographic hashes**. The real importer still needs byte snapshots, encoding evidence, source hashes, typed parsers and durable atomic activation. No state transition here proves crash consistency, losslessness, security, routing parity or idempotence in production.

## Evidence and coverage

The HTML embeds a searchable historical registry extracted from the [corrected complete data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md), Appendices A–D: **207 persisted option keys, 243 action IDs, 139 theme colors and 474 legacy conversion aliases**. Entries retain storage families/default expressions or action identities/default bindings. These are source evidence; a searchable entry is not an implemented setting control or approved migration mapping. Aliases retain their original family; enumeration does not authorize theme import.

Interactive values, origins and imported records are synthetic. Six preference fields and seven binding examples are interactive; sixteen import dispositions cover selected settings and the difficult preservation cases. The effective defaults displayed in the small preference sample are illustrative UI choices. Exact legacy expressions remain separate in Coverage, including unset values. `EDITBOX_COMMIT` demonstrates keypad distinction; the toy binding defaults are deliberately not presented as the complete legacy defaults.

| Complete-inventory capability family | Represented here | Remaining work |
| --- | --- | --- |
| Program/editor/audio/video preferences | Search, application ownership, current/default/imported origin, pending override, item/page reset | Every remaining control, units, range validation, unsupported platform/device choices and dynamic default behavior |
| Advanced/conversion and external paths | Registry inventory; missing recent path retained | Actual advanced/conversion/path pages, platform availability and reviewed relink flow |
| Shortcuts G/S/E/V/A, aliases and macro bindings | Seven actions, explicit contexts, conflict operations, unresolved macro identity and routing inspector | Complete eligibility table, fixed clipboard/numpad handlers, legacy N/W aliases, native key normalization, all action availability and macro registration reconciliation |
| Toolbar, twenty tag buttons, histories and packed timing profiles | Ordered toolbar replacement, retained color history and held timing-profile interpretation | Full editors, tag insertion modes, preserved empty/false/zero distinctions, packed masks and collection-limit fixtures |
| Rules, dictionary and subtitle colors | Separate authored-data rows; Unicode words; no reset with preferences | Actual lossless readers/editors, rule enable flags/regex compatibility, merge semantics and catalog transfer |
| Legacy appearance | `PROGRAM_THEME`, theme files and all 139 theme mappings explicitly excluded | New visual-system settings are a separate accepted-design implementation, not a legacy theme importer |
| Sessions, catalogs, subtitles and recovery | Ownership boundary only; they are not preference defaults | Direct preserved-input readers and their dedicated UI flows; caches remain regenerable |
| Associations | No registration or implicit import | Explicit platform-specific association task and final packaging qualification |
| Import/repeat/rollback | Side-by-side or card diff, item disposition, stale revision, incomplete stage, receipt, later-edit warning and complete destination restore | Real discovery/encoding/hash validation, interrupted disk writes, schema versions, durable receipt/rollback history and production fixtures |

The surface coverage uses the [complete UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md), especially OptionsDialog/HkeysDialog and the OptionsPanels conversion family. [Legacy TabPanel routing](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.cpp#L100) includes cross-panel routes and fixed handlers; [legacy conflict handling](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp#L449) provides the operation vocabulary. The inspector is a visibility proposal, not a replacement inferred from action prefixes.

## Decisions still required

- Choose A/B or a combination of their review layout. This does not choose storage again.
- Confirm whether keep-current with explicit per-item replacement is understandable, including authored collections, rather than implicitly treating every import as a whole-profile replacement.
- Judge whether the conflict/routing explanation makes intentional reuse and unresolved ambiguity clear. The **local-first toggle is an unapproved departure**; no new precedence follows from a layout choice.
- Judge whether the complete-generation rollback warning adequately names later edits it replaces.

**C04-short-file remains unapproved.** The four-binding example is held, with no apply choice; this mockup does not authorize replacing the legacy count-triggered defaults outcome. The production fixtures and named decision belong to #44/compatibility follow-up.

JavaScript syntax was checked without executing the UI. No formal tests were added, consistent with the prototype skill. Root browser checks exercised repeat import preserving a later 90-ms edit, explicit complete-generation rollback, failed staging without activation, stale-source rejection/rebuild and shortcut overlap with IME consumption blocking dispatch. Both arrangements were visually inspected and captured at the default 1280-wide browser viewport without document overflow. This is browser evidence only, not durable import or native accessibility proof. Human reaction remains pending; keep the ticket open.
