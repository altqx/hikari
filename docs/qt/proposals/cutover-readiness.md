# Proposed beta readiness and Qt cutover policy

For [Set beta readiness, rollback and Qt branch cutover policy](https://github.com/altqx/hikari/issues/64). **Proposal for review, not release authorization or a readiness claim.** The accepted [testing](../testing.md), [compatibility](../compatibility.md), [distribution](../distribution.md), [platform](../platform-policy.md), [build](../build.md) and [performance](../performance.md) contracts remain authoritative. This proposes beta gate applicability and accountable sequencing; it sets no date, stable-gate waiver, feature removal or branch operation.

## Labels describe evidence

| Label | Proposed minimum and permitted claim |
| --- | --- |
| Development preview | Identified `qt` commit and dependency inputs, applicable merge checks, runnable instructions, known gaps and disposable/copied-data guidance. Unsupported test build; never a stable update. Unsafe or uncharacterized workflows belong here, visibly restricted to controlled fixtures. |
| Public beta | Opt-in, immutable prerelease packages with exact platform/package claims, capability matrix and unresolved issues. All applicable package integrity, licensing, offline startup, profile isolation and recovery checks must pass for the distributed artifacts. Each advertised usable workflow has relevant correctness/failure evidence. Missing capabilities or native qualification remain prominently incomplete; beta neither closes their tickets nor satisfies stable gates. Label unsigned Windows artifacts explicitly, as already permitted. |
| Stable / replacement candidate | Every required capability and applicable native, accessibility, performance, compatibility, build and deployment gate passes on the exact claimed targets. Complete payload/source/license records and direct-download verification; trusted timestamped signing for stable Windows installers and binaries. No unresolved mandatory gate. |

A beta can expose unfinished read-only inspection or disable an unqualified mutation, with the limitation visible before use. This temporarily bounds the experiment; it does not remove that capability from the rewrite. A known data-loss path without a demonstrated safe boundary/recovery procedure blocks public beta exposure of that path. “Use at your own risk” is insufficient. A platform omitted from one experimental artifact remains an outstanding first-class delivery obligation.

Allowing public beta before full native qualification is a **new staging policy requiring approval**, not a consequence of the existing unsigned-beta permission. Unsigned beta relaxes stable Authenticode only: final SHA-256 manifests, the owner-controlled signature/public-key verification procedure for direct downloads, and applicable license/source records still apply.

Stable notification remains the default channel. Beta requires opt-in; checks only notify and open a validated page after user action. No automatic install, restart, channel migration or downgrade follows from these labels. A beta's missing full-system evidence is disclosed incompleteness, never a passed or waived stable gate.

## One candidate, one evidence record

Maintain a candidate manifest keyed to source commit, dependency/artifact locks and final package digests. Expand the [coverage ledger](../coverage.md) into inventory-level capability rows: accepted outcome or named departure, implementation ticket, fixture/command/environment, observation links, and **passed / failed / unexecuted / inconclusive / not applicable with reason**. Approval, code merged, and behavior verified are separate fields. Prototype reactions settle only their recorded UX scope.

Evidence must cover unchanged subtitle/catalog/Session/autosave inputs and Lua scripts, one-shot settings/hotkeys import, semantic/byte/API/render contracts where applicable, and the [approved departures](../compatibility-decisions.md). Native media/fonts, IME/RTL, accessibility, calibrated resource/performance gates and clean-machine package recovery cannot inherit a pass from HTML, Python or one Windows sample. Reproduce the pinned CMake workflows on declared Windows/Linux toolchains, including missing/corrupt inputs and offline prefetched reconstruction. Qt credentials/license authorization remain explicit prerequisites, never embedded release inputs.

An unexplained mismatch, failed/unavailable mandatory observation, unsafe migration, missing provenance/license material, incompatible payload or missing stable signing blocks the affected stable candidate. Quarantined tests retain owner, issue, impact and replacement coverage; quarantine does not erase the gate. Only already accepted deferrals, such as initial Flatpak/store packaging and the later macOS port, are outside initial completion. New feature/platform deferral or a weakened threshold requires a separate named decision before revising the manifest. This proposal grants no waivers.

Changed code, dependencies, fixtures or packages invalidate affected evidence. The verifier records what must rerun and why unchanged evidence remains applicable; a rerun never deletes the original failure. Missing design decisions remain blockers until resolved, including lifecycle/import/transaction details still labelled proposals.

## Side-by-side use and independent recovery

Recommend separate legacy, Qt beta and Qt stable installation identities and writable profiles, with visibly distinct launchers. Portable packages unpack beside existing versions with their explicit sibling data directories. Do not share mutable caches/recovery stores or silently take associations, defaults or legacy profile ownership. Verify install/uninstall of one leaves the others and user data intact.

Existing files, catalogs, Sessions, autosaves and Lua scripts remain directly usable under the accepted compatibility contract. Import is separate: snapshot legacy settings/hotkeys and authored rules/dictionaries, preserve originals, report unresolved mappings, and activate only a reviewed Qt destination. **No legacy themes, theme colors or `PROGRAM_THEME`.** Preserve the previous destination profile independently of the new application's recovery mechanism. Exact importer/conflict UX awaits its own decision; release policy cannot approve C04 or other unresolved fixes.

Beta guidance uses copies for writable Documents/catalogs and separate recovery paths. Shared source/media paths are not a concurrency guarantee: never promise conflict-free concurrent editing or automatic merging. Unchanged scripts/native modules retain ordinary OS permissions; separate profiles are not a filesystem sandbox.

Keep the previous verified package, dependency/source provenance and pre-migration profile backup with restoration instructions tested independently of the failed build. Rollback means stop safely, select/reinstall the prior package and restore a compatible profile after reviewing newer profile changes. It does **not** silently restore older subtitle files or discard work created after backup. Recover such Documents separately, preserving original and newer copies; do not assume a legacy Session contains drafts or undo history. Format-specific backward readability/export loss needs evidence. Incompatible profile migration has no promised automatic downgrade.

## Ownership and replacing `main`

Until cutover, `main` remains the wx bug-fix line. Recommend limiting it to security, data-loss and critical-regression fixes with characterization; no new rewrite features or accidental second architecture. Transfer applicable outcomes/fixtures to `qt`, not wx internals. Observable changes still require the compatibility approval rule.

Name three responsibilities in the candidate record: evidence coordinator assembles coverage; verifier checks required results, integrity and rehearsed restore; release owner controls signing/accounts, final authorization and notices. One person may hold multiple roles where independent review is not required by an existing contract. Agents can prepare evidence and execute already authorized mechanics; they cannot invent publisher consent or waive gaps. Maintainer responsibility for each supported line must also be explicit.

Recommend replacing `main` only with a stable-ready Qt candidate. Preserve the exact old-main commit and a recoverable reference, source and package; record the exact candidate commit/digests, evidence manifest, blockers resolved, recovery rehearsal, maintainer and release owner's go/no-go. Then execute a reviewed repository transition preserving history/protections and updating CI/default development guidance. A tree-changing integration needs affected checks before adoption; no force-replacement strategy is selected here. Repository cutover and public release are separately recorded actions, not implied by accepting this policy.

After cutover, recommend archiving wx as a reference rather than promising indefinite parallel maintenance. Any continuing wx support line needs explicit scope, owner and end condition. This cannot retire Windows 10: Qt initially qualifies Windows 10/11, current Ubuntu LTS and Fedora. Windows 10 obligations end only through the separate explicit retirement decision and notice; advancing Qt or changing `main` is insufficient.

## Decision groups

1. **Readiness:** explicitly approve bounded public betas before full native qualification under the proposed artifact/workflow checks, with incomplete capabilities visible and all stable gates mandatory? Recommended; this is a new phase-applicability decision.
2. **Recovery:** require isolated installations/profiles, preserved import sources and independently rehearsed package/profile rollback, without automatic Document downgrade? Recommended.
3. **Cutover:** require the single concrete stable-ready candidate record and owner go/no-go before Qt replaces `main`, then archive wx unless a bounded maintenance line is explicitly commissioned? Recommended. This chooses policy now; the later go/no-go verifies one named candidate, rather than reopening every accepted design.

No candidate, installer, migration, rollback rehearsal, signing workflow or native release gate has been qualified by this document.
