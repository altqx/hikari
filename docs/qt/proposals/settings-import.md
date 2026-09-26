# Proposed settings, shortcuts and preserved-input importer

Draft for [Design settings, shortcuts and the one-shot importer](https://github.com/altqx/hikari/issues/44), using accepted [vocabulary](../../../CONTEXT.md), [architecture](../architecture.md), [compatibility](../compatibility.md), [automation](../automation.md) and [media ownership](../media.md). Settings/import UI review follows in a separate prototype. No implementation or compatibility pass is claimed.

## Evidence before choices

The [corrected data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md) supplies the complete option/default, action/binding, legacy conversion and packed-field registries. Legacy data lives beside the executable, not automatically in AppData/XDG. Main/audio partitioning follows enum ordinals, not an AUDIO name prefix. Old converters rewrite inputs; malformed headers and short hotkey files can install defaults. Never invoke those loaders for import.

[Routing source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.cpp#L100) adds fixed grid clipboard/numpad-Enter handlers, cross-panel actions and video/audio shortcuts in Grid. Therefore “focused scope, then global” alone is not an established legacy contract. [Conflict handling](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Hotkeys.cpp#L449) offers switching, deleting, cancelling and cross-context “set anyway”. Preserve these capabilities.

## Ownership and records

| Owner | Persisted meaning |
| --- | --- |
| Application profile | Program/editor/audio/video/conversion preferences, language, shortcut overrides, ordered toolbar actions, tag-button definitions, histories, recents and named timing profiles. Backend/device preferences remain distinct from active playback ownership. |
| Shared Workspace | Layout/panel visibility, named presets and relevant view preferences; versioned docking geometry belongs to its dedicated contract. No per-Document layout or shortcut set is invented. |
| Document / Session | Subtitle content and Script properties belong to Document; selection, media associations and restorable view positions follow their application/session contracts. A preference supplies a default only where that capability already supports one. |
| Authored collections | Rules, personal dictionary, Style catalogs and Font catalogs retain their separate formats/readers. They are not expendable preference defaults. |

Use stable, untranslated SettingIds with declared type, scope, units, default, capability requirement and migration aliases. Distinguish missing, explicit empty/false/zero, malformed and unsupported values. Preserve original tokens and provenance alongside interpretation. Effective defaults, imported values and later user overrides are separately attributable; no universal Document-override layer.

Every registry entry receives a destination or explicit unresolved/excluded disposition. Preserve ordered histories, empty entries, recent-path order, profile masks, numeric units and tag-button insertion modes. The second positional MoveToVideoTime profile field means audio anchoring. Do not parse profiles by label alone or silently change collection limits.

**Exclude legacy theme files, theme color mappings and PROGRAM_THEME entirely.** This does not exclude ASS Style colors or subtitle color-picker history. New appearance choices use the accepted visual system. Rules retain enabled flags, regex/replacement bytes and masks; changing wx regex semantics requires its own compatibility decision. Preserve Unicode personal words and unavailable font-family names.

## Actions, routing and conflicts

ActionId is a stable host-owned key, independent of translated label, menu placement and transient numeric ID. Retain legacy symbolic/numeric aliases. A binding records ActionId, G/S/E/V/A context, logical key/modifiers/keypad distinction, original spelling and disposition. Missing/default/unbound/unresolved are distinct; blank legacy records alone do not prove deliberate unbinding. Legacy N/W contexts map through documented aliases to S/V.

Route from captured focus/input context through an explicit eligibility table including legacy cross-context entries, then the action's target/scope/availability checks. Text/IME consumption, modal ownership and protected-reference rules apply before dispatch. Global means application-wide, not an OS hotkey. Focus cannot retarget an edit or override active media ownership. Native editor Enter decisions remain separate.

Propose an inspectable routing result: candidates, chosen action, target and disabled/conflict reason. Preserve valid same-chord bindings in disjoint contexts. Show global/local shadowing; ambiguous simultaneous candidates must be reviewed rather than resolved by registration order. New focused-before-global precedence is a proposal, requiring a named departure wherever legacy observations differ. Keep reset, unbind, switch, replace and intentional cross-context reuse available.

Automation uses host registration identity plus script provenance, with a legacy filename+macro-ordinal alias. Runtime IDs beginning at 30100 are not durable. Script path/hash, registration ordinal and displayed name are evidence, not a universal stable macro ID. Missing scripts, basename collisions or changed registration order remain unresolved; do not bind to the nearest name. Script updates need reconciliation without changing existing Lua APIs. Import must not execute scripts to discover identity; normal isolated registration can resolve them later through review.

## Import transaction

1. Discover known executable-adjacent Config roots and allow an explicit installation/data directory. List candidates; never combine installations by newest mtime. Selected files include Config/AudioConfig, Hotkeys/AudioHotkeys, Rules and personal dictionary. Sessions/catalogs/recovery remain directly readable; caches are regenerable, not imported preferences.
2. Copy selected input bytes to a preserved snapshot; record path, hash, encoding evidence and header. Parse that snapshot with dedicated readers. Preserve unknown keys, unsupported bindings and malformed fragments in the report. Ambiguous decoding offers an explicit interpretation; no silent replacement characters or default rewrite.
3. Produce a revision-bound plan showing source → normalized value → destination/current value, with import/keep/unresolved/excluded status. Report missing and foreign-platform paths without deleting them or guessing replacements. Proposed relinks retain original paths and require review. Windows-only choices stay represented as unavailable capabilities. File associations require a separate explicit platform action.
4. Review conflicts, then stage a new destination generation with typed values, preserved authored data and receipt. Recheck source hashes/destination revision before commit. Back up the previous destination generation; activate the complete new generation through one manifest switch. Failure before activation leaves the prior generation active; report incomplete staging for cleanup/retry.
5. Receipt identity includes source content hashes, mapping/schema version and destination profile. Repeating the same accepted plan is a no-op, including lists/rules. Changed sources or mappings generate a fresh diff; never overwrite later user edits automatically. Rollback restores the previous complete destination generation, reports subsequent edits it would replace, and never modifies legacy sources.

## Concrete cases and decision frontier

- A four-binding file must remain preserved. Importing those bindings instead of the old count-triggered defaults is **C04-short-file**, requiring its named outcome/fixtures; non-destructive input handling is already mandated.
- Ctrl+Space in Audio and Grid is not automatically a duplicate to delete; actual eligible routes must be shown. Two different same-basename scripts cannot silently receive one imported macro binding.
- Missing D:\\media remains an unresolved path; a second import neither duplicates recents nor “repairs” the original file.

Pending user choices, with recommended answers:

1. **Storage is accepted:** per-user writable profile by default with explicit portable mode, through the distribution decision. Discovery still reads legacy locations; no repeat storage vote is needed.
2. **Existing destination:** keep current choices unless individually replaced, or replace the whole profile? Recommend keep-current with reviewed per-item replacement and complete-generation rollback.
3. **Shortcut shadowing:** allow intentional local/global overlap with explicit priority, or prohibit new overlap? Recommend allow with visible routing, retaining legacy cross-context reuse. Any changed legacy precedence needs named approval; unresolved old behavior awaits fixtures.

These answers do not approve C04-short-file, other candidate fixes or capability removals. Follow-up UI prototypes demonstrate review, mapping and recovery; implementation fixtures verify source hashes, repeat import, interrupted activation, rollback and actual routing.
