# Observable compatibility and defect approval

**Accepted policy, 2026-09-27.** In the live Wayfinder review, the user chose **“Approved fixes become default.”** [ADR 0005](../adr/0005-observable-compatibility-and-defect-approval.md) records this decision for [Define observable compatibility and the legacy-defect policy](https://github.com/altqx/hikari/issues/38). This settles how an approved departure takes effect. It does not approve any candidate defect fix or claim a passing compatibility suite.

## Carry-over commitments

Existing subtitle files, style/font catalogs, sessions/autosaves and Lua scripts must work as-is. Opening or importing them must leave original source files unchanged. Settings and hotkeys receive one-shot migration, retaining their meaning, scopes and identities and reporting unresolved entries. Preserve authored rules and dictionaries. **Legacy theme files, color mappings and `PROGRAM_THEME` are excluded from migration.**

Every inventoried capability remains in scope unless the user explicitly approves its merge or removal. Combining UI surfaces must retain their operations and command scopes; accepted styling, renderer or workspace decisions do not implicitly remove capabilities. The rewrite uses a new core in the big-bang migration. Compatibility is an observable contract, not a requirement to reuse wx classes or their internal structure.

## Acceptance classes

| Class | Contract and required decision |
| --- | --- |
| Required compatibility | Preserve documented workflows, data meanings and working script behavior. Internal implementation can change. An observable departure requires explicit classification and approval rather than being excused by the rewrite. |
| Explicit defect fix | Document a named defect entry with its trigger, old evidence, intended result, affected data/scripts/workflows and fixtures. The user must explicitly approve that entry or an enumerated batch. Once approved and implemented, the corrected behavior is the default; users do not need to opt into each fix. |
| UX redesign | Obtain reaction to a concrete prototype and record which interactions changed. A visual approval does not authorize altered serialization, script APIs or command outcomes. Trace any approved capability merge/removal explicitly. |
| Unsupported input / recovery | Bound the unsupported case, preserve original/raw input, explain the diagnostic and provide the applicable recovery, relink or export path. Rejecting formerly accepted input or dropping information requires explicit user approval. Missing media, fonts or other dependencies must remain distinguishable from malformed documents. |

An approval must identify the intended outcome, not merely acknowledge that a source pattern looks wrong. “Fix bugs,” research completion, or an agent's recommendation is not approval of unnamed departures. An enumerated batch may approve several specified outcomes together; its scope must remain recoverable from the decision record.

There is **no per-fix opt-in requirement** after approval. A compatibility switch is not the default remedy for every corrected defect. This policy also does not authorize the agent to choose and implement unreviewed fixes automatically.

Suspected crashes, destructive behavior and data loss are not automatically golden requirements. Characterize them against copies or controlled fixtures, retain the old-result evidence and request a disposition. Do not silently reproduce a hazard as intended behavior or silently replace it with a different observable contract.

## What a fixture compares

Keep these contracts separate; a pass in one does not establish the others.

| Surface | Evidence and comparison rule |
| --- | --- |
| Original bytes and file layout | Read/import must not modify originals. Record hashes and paths before/after. For saved/exported output, define format-specific encoding, line-ending, ordering and normalization expectations; this policy does not promise universal byte-identical reserialization. An undefined byte contract does not authorize information loss. |
| Semantic document and application state | Compare text/tags, styles, metadata, timing/frame meaning, translation/tree state and resource associations as applicable. Include selection, dirty state, undo and command scope where observable. Specify any intentionally normalized representation. |
| Script API behavior | Run unchanged scripts and compare argument/coercion behavior, return types/values, errors, side effects, invocation ordering and undo/cancellation outcomes. Upstream documentation is comparison evidence, not permission to change HikariSub semantics. |
| Rendered output and interaction | Pin fonts, renderer/dependency versions, media/time, platform and scale. Define exact comparisons or justified tolerances before accepting results. Keyboard, focus and accessible behavior require their own native observations. |

An unexplained mismatch fails parity review. A source reading, successful compilation or screenshot alone is not a substitute for the relevant observable fixture. Performance budgets and release/cutover scheduling are separate decisions.

## Ledger and approval record

Every proposed departure needs a stable ID, source commit and evidence classification; a trigger and affected capabilities; fixture provenance/hash and reproduction steps; runtime/platform/dependency details; the old observed result or an explicit “not yet executed” status; the proposed expected result and comparison rule; and the exact user approval record, including its scope and date. Link that decision to the implementation and validation evidence when they exist.

Preserve both old characterization and the approved new expectation. Do not replace an old oracle without retaining the explanation for the change. Track unexecuted, inconclusive, failed and passed observations distinctly; no invented coverage counts or blanket platform success follows from one fixture.

The [approved-departure ledger](compatibility-decisions.md) records the subsequently accepted document and timing batches: C03 preservation, C02 loss preview, C05 audio association, C01 equality and FPS isolation, and T42-A audio frame alignment. These approvals select outcomes, not passing implementations. The [candidate ledger C01–C08](proposals/compatibility-policy.md#candidate-ledger) retains the broader evidence and unresolved subcases; no blanket acceptance applies to an entire mixed row.

In particular, the current `set_undo_point` and `register_filter` stubs, mutable validation behavior, late command rejection and truncated dialog button-ID arguments await their own named decisions. Do not silently substitute upstream Aegisub behavior. The unchanged-script commitment and explicit-departure review apply even when an upstream behavior appears preferable.

Source evidence remains the [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md), [corrected data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md), [UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md) and [Lua-host research](https://github.com/altqx/hikari/blob/a99a2470e958437290de73dd2451ee57cc763336/docs/research/lua-automation-host.md). Their static findings guide fixture work; this accepted policy records no executed parity result.
