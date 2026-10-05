# Localisation

The rewritten application uses **TS as the authoritative translation source and QM at runtime**. altqx accepted “migrate fully to TS” in [Choose the localisation format and migration path](https://github.com/altqx/hikari/issues/29#issuecomment-5849403539); [ADR-0002](../adr/0002-ts-localisation.md) records that decision. The migration rules and verification gates below describe required implementation work. The catalog migration ([O4](https://github.com/altqx/hikari/issues/196)) is recorded in `i18n/migration/` (key map, extracted keys and accounting report); no Qt language-switch check has been performed for this specification.

The source evidence is [Localisation in Qt: gettext .po vs Qt .ts, migrating existing translations](https://github.com/altqx/hikari/blob/78ea3804dd9a732ff1f26183353d79535582c3f7/docs/research/qt-localisation.md). It inventories Polish, Korean, Thai and Tamil catalogs, English source text, context-sensitive plural entries and printf placeholders; its suggestions to retain PO authoring or a mixed runtime are alternatives superseded by the accepted format decision.

## Ownership and contribution

| Artifact | Role in the rewrite |
| --- | --- |
| Existing PO/POT at a recorded commit | Immutable migration input, including translator attribution and history. Never regenerate it from Qt strings. |
| Reviewed TS catalogs | Sole authoring source for rewritten application translations, including the Lua compatibility entries described below. |
| QM files | Generated runtime artifacts compiled from TS by the selected Qt toolchain. Never hand-edit them. |
| wx application on `main` | Existing PO workflow for bugfix maintenance only; separate from Qt translation authoring. |

Start from the [inspected PO baseline](https://github.com/altqx/hikari/tree/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Locale). Record the exact input commit in the migration report; preserve the original files/history. A later relevant maintenance fix may be imported through an explicit reviewed mapping update, not bidirectional synchronization or continuous PO-to-TS generation.

Contributors can edit TS with Qt Linguist or another TS-capable editor and submit Git changes. No new hosted service is a prerequisite. **No translation provider has been selected**; a later service must operate on the same authoritative TS catalogs and pass import/export fidelity checks before adoption.

## Keys and migration

Use stable semantic contexts for QML and C++ translation calls, shared where the meaning is shared. For example, an editor action can use `HikariSub.Editor` rather than obtaining its identity accidentally from a movable QML filename. Explicit QML contexts and their C++ equivalents must be discoverable by `lupdate`; a wrapper is acceptable only after extraction is demonstrated. Source wording and disambiguation remain part of a source-based translation key, so a wording or meaning change requires review.

Maintain a reviewable migration map from old `(msgctxt, msgid, msgid_plural when present)` to new `(context, source, disambiguation, numerus)` and any placeholder transformation. Record one-to-many mappings where an old message serves multiple new meanings. Do not match by English text alone or silently collapse identical text from different contexts. In particular, retain the separate font-count meanings “found or copied” and “not found or not copied”.

Convert only in a scratch area, extract the new Qt keys, then merge translations through this map. `lconvert` is a candidate file-format converter, not proof of semantic equivalence. Exact, unchanged meanings may retain reviewed translations; changed wording, meaning, contexts with uncertain correspondence, and formatting changes require translator review. Preserve comments, translator attribution, locale identifiers and fuzzy/obsolete/review status in TS or an accompanying migration record when they have no exact TS representation. Do not promote fuzzy or uncertain translations to finished entries, or discard obsolete entries before the migration audit accounts for them.

### Plurals and placeholders

Use Qt numerus entries for counted UI text and language-specific forms, including an English plural catalog where needed. The baseline has three forms for Polish, one for Korean and Thai, and two for Tamil; migration must check the selected Qt version's language rules and actual count results rather than copying form positions blindly.

For rewritten UI calls, map printf placeholders such as `%d` and `%s` to Qt `%n` or numbered placeholders only together with their call-site formatting. Validate placeholder identity, multiplicity, ordering and types in every translated form. Preserve literal `%%`, accelerator ampersands, line breaks, quoting and non-ASCII text deliberately. Do not perform a global textual substitution. The Lua compatibility context retains its original formatting convention.

## Lua compatibility

Keep the public `aegisub.gettext` entry point and its existing argument conversion/error behavior, UTF-8 string results and untranslated-source fallback. The [current implementation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L83-L88) passes one source string to wx translation lookup; registration is [unchanged at the script boundary](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L590-L600). Broader automation compatibility is covered by [Hosting Aegisub-compatible Lua automation in a QML app](https://github.com/altqx/hikari/blob/a99a2470e958437290de73dd2451ee57cc763336/docs/research/lua-automation-host.md).

Provide a dedicated TS context, `HikariSub.Automation.Gettext`, whose source keys are the original literals reachable through that legacy lookup. A Qt host adapter resolves those keys through QM and returns the input when no reviewed translation exists. Keep this namespace independent of rewritten UI contexts. Do not expose previously context-only messages through an invented context-free match, invent a plural argument, or interpret placeholders in the adapter. A script may pass the returned `%s`/`%d` text to its own formatter; changing it to `%1` would break the script.

Maintain these compatibility entries in TS even if `lupdate` cannot discover dynamic Lua calls; extraction must not obsolete or delete them simply because they are absent from QML/C++ source. Bundled literal-call extraction or a maintained key registry may supply them, with an explicit merge check. Neither the adapter nor translator contribution requires legacy PO authoring or an MO runtime in the rewritten application.

Verify lookup behavior against the old host before replacing it. Calls after a language change should use the newly installed catalog; already returned Lua strings remain ordinary script values. Do not rerun scripts implicitly to refresh labels. Any registered labels that require a script reload must have an explicit refresh/reload policy in the automation implementation.

## Build and runtime behavior

Separate developer-invoked source extraction/update from ordinary builds. Normal builds compile reviewed TS into QM without rewriting source catalogs. Pin the Qt Linguist tools with the application's Qt baseline; choose CMake arguments compatible with that baseline. Keep unfinished translations out of production runtime catalogs, with source-language fallback, and package the required application and Qt component translations for Windows and Linux.

Language changes must replace the relevant translators with defined precedence and refresh all live QML engines. Notify or rebuild cached model/action/dialog strings and accessible names as well as visible labels; Qt retranslation alone does not refresh arbitrary cached strings. Keep translator lifetime and worker lookups safe while switching. Define locale selection, language-only fallback, missing catalogs and persistence explicitly and exercise the same policy on both platforms.

UI language and layout direction must not alter ASS/SRT serialization, decimal separators in document data, time/frame calculations, subtitle text or its bidi behavior. Test RTL/pseudolocalized UI layouts separately from mixed-direction subtitle content. Mirroring video coordinates or a time axis is a separate UX choice, not a consequence of installing a translation.

## Required verification before migration ships

All checks in this table are **pending implementation acceptance gates**, not reported passes.

| Gate | Required evidence |
| --- | --- |
| Migration accounting | Input commit; per-language message/plural counts; old-to-new key map; attribution/comments/review-state report; every dropped, split, merged or changed entry explained. |
| Key and formatting fidelity | Context collisions, both font-count meanings, literal percent signs, accelerators, line breaks and Unicode; placeholder signatures for every plural form and corresponding call sites. |
| Plural/fallback behavior | Polish, Korean, Thai, Tamil and English with at least counts 0/1/2/5/12/22; absent/unfinished messages, unavailable language and locale fallback. Compare expected rendered strings, not only successful compilation. |
| Deterministic workflow | Extraction discovers rewritten keys, preserves Lua keys, and has reviewable diffs; ordinary builds leave TS unchanged; clean builds compile/package/load QM using the pinned toolchain. |
| Native Qt interaction | Cold startup and live language changes in QML, dialogs, actions, models and accessible names on Windows and Linux; long labels, keyboard focus and RTL/pseudolocalized layouts. |
| Lua boundary | Existing and missing source keys, UTF-8, argument/error edge cases, preserved printf payloads, lookup after language changes and safe catalog replacement during worker activity; unchanged scripts need no catalog-format awareness. |
| Document independence | Identical document content and serialized bytes, time/frame results and numeric separators before/after UI-language changes; no subtitle direction or editing changes. |

Review failures before marking entries finished or enabling the migrated catalog in a release. The accepted TS decision does not assert conversion fidelity, native accessibility or unchanged-script compatibility without this evidence.
