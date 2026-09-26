# Proposed edit drafts, commands and undo

For [Define edit drafts, commands and undo transactions](https://github.com/altqx/hikari/issues/43). **Proposal, not an accepted transaction policy.** Enter commits and advances outside IME composition; editable hidden tags are required. The draft-navigation question and native editor's mapping policies remain open. This proposal connects the accepted [document](../document-model.md), [time](../time-semantics.md), [workspace](../ux/workspaces.md) and [compatibility](../compatibility.md) contracts without approving unlisted departures.

## Evidence and boundaries

The [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md) records snapshot history, a 500-state cap, current/saved indices and same-Line typing amendments. [SetModified](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1125) records content and selection/view state. Save, explicit commands and undo interrupt typing amendment. These are source observations; generic time-window coalescing is not established legacy behavior.

Runnable studies expose alternatives: [mapped native editor](https://github.com/altqx/hikari/blob/616d2f0bfb0bfe28f0dcd792c043e7034ee1606e/HikariSub/prototypes/ass-editor-mapped/README.md), [translation drafts](https://github.com/altqx/hikari/blob/8f6c3b1ee861c2a75a2acc57fee5da44fc9dfc34/docs/prototypes/translation-comparison-notes.md), [audio timing](https://github.com/altqx/hikari/blob/72eee89191f60eece36ebd661a67702924d1a414/docs/prototypes/audio-timing-notes.md) and [automation](https://github.com/altqx/hikari/blob/72eee89191f60eece36ebd661a67702924d1a414/docs/prototypes/automation-host-notes.md). Their draft/commit behavior is not approved merely because it runs.

## Draft ownership and reconciliation

Recommend one application-owned draft per Document/Line, based on a named content revision. It holds changed text roles, timing and metadata fields plus the base values needed to detect conflicts. Raw/hidden text are projections of the same draft; Audio and Editor cannot silently maintain two competing pending times. Native preedit stays transient inside its input method. Selection/caret and undo checkpoints belong to the editing session, not serialized ASS.

Recommend retaining drafts when navigating Lines or tabs, with a pending-draft indicator and return/discard controls. The alternatives are commit on navigation or an explicit commit/discard choice. No alternative permits a tab switch, reparented panel or worker callback to discard text silently. Finish or explicitly cancel native composition before a command requiring its final text; candidate confirmation must not dispatch commit-and-advance.

An external command declares its read/write field scope and exact Line IDs. A pending draft on a touched field requires reconciliation: recommend commit the affected drafts, discard them, or cancel the command. Show which Lines are affected; do not silently include all selected or filtered-out drafts. Non-overlapping fields may rebase only when base comparison proves they are unchanged, including structural dependencies. Deleting a Line, replacing a text role or changing format requires whole-Line reconciliation. A rejected draft remains recoverable.

## Command and history boundary

Commands capture Document identity, expected revision, explicit scope, permissions, parameters and validated prerequisites. A prepared result includes content changes, diagnostics and the next editing/selection context. Publish it through one application commit; no QML delegate or worker mutates authoritative records directly. Reject stale/protected/missing targets without retargeting. A multi-Line operation is one named document history entry. Cross-document tools produce a reviewed plan and per-Document outcomes; an application-wide atomic history is not assumed.

| Action | Proposed boundary |
| --- | --- |
| Typing, tag insertion, spelling replacement | Draft history restores exact raw source across raw/hidden projections. Coalesce adjacent typing within the same role/Line and input sequence; paste, completed composition, explicit tag commands, cursor jumps and view/Line changes are boundaries. Exact native grouping still needs review. |
| Enter / Commit / Commit + next | Commit all valid fields of that Line as one named entry. Enter and Commit + next advance; Commit stays. A validation failure keeps the draft and focus with a usable reason. Translation confirmation is a separate explicit action. |
| Numeric/audio/visual gesture | Stage throughout a gesture; commit on the selected explicit/auto-commit boundary, never each pointer sample. Cancel restores the pre-gesture draft. Default auto-commit and advance policy remain the surface's review question. |
| Bulk timing/style/search | Preview the declared scope, reconcile conflicting drafts, then one validated commit. Missing prerequisites reject the whole operation under proposed C07-atomic-rejection below. |
| Undo while editing | Draft Undo while that editor has draft history; once exhausted, make the transition to document Undo visible in the action label. History tool entries always identify the Document and committed operation. Never make a native context menu operate an unrelated hidden stack. |
| Document Undo/Redo | Restore content and the entry's active/selected/marker Line IDs; resolve deleted/restored identities explicitly. Recommend restoring useful Grid position, while playback position, audio ownership, workspace geometry and other documents stay separate. Reconcile affected drafts before moving history. |

Proposed **C43-save-identity**: save points identify the exact committed content written, not a mutable list index or merely the latest revision number. Undoing to that content clears committed dirty state; unsaved drafts still count as pending work. New edits after Undo truncate redo. Preserve the existing 500-state capacity initially; this does not imply preserving its dirty-state behavior. Legacy saved indices can be forgotten by pruning or redo truncation. Keeping a durable saved-content identity across those events is a named proposed departure, still requiring approval and characterization fixtures; content equality was not established as the old rule. Any byte-budget pruning or changed retention needs its own measured policy. Do not claim persistence of in-memory undo through restart.

## Save, close and recovery

Recommend Save commits reviewed valid drafts for that Document before encoding, as explicit named history entries; an invalid/composing/conflicted draft blocks that save with a direct resolution path. This avoids a success notification that omitted visible edits. Saving an older immutable snapshot while newer edits arrive may succeed, but only that written snapshot becomes the saved point. Export has an explicit role/scope and reports whether drafts were included. It never silently marks the source Document saved.

Close/quit names both unsaved committed content and pending drafts. Save, Discard and Cancel act on that complete scope; Cancel preserves state. Recovery must retain committed content plus recoverable pending drafts separately, without pretending the drafts were saved into a legacy subtitle file. The on-disk recovery/session format and autosave browser are separate decisions.

## Automation and named compatibility boundary

The accepted helper process cannot own application history. Proposed staged results carry captured Document/revision/selection and apply atomically after reconciliation. Cancel/crash before authorization discards staged edits; an already committed result is undone normally. External script I/O is outside document rollback. Helper lifetime, target reservation and cancellation escalation remain live automation decisions.

**C07-atomic-rejection is not yet approved:** old timing code can shift earlier rows before a later exact-timebase prerequisite fails. Proposed outcome leaves content, selection, dirty state and history unchanged on rejected commands. Preserve a fixture demonstrating the old partial mutation and the approved replacement if accepted. This does not approve read-only macro validation.

C06 `set_undo_point`/`register_filter` stubs, C07 mutable validation/callback order and C08 button-ID behavior remain unchanged unless their own named outcome is approved. One proposed macro history entry is not authority to implement upstream undo semantics. No implementation or parity pass is attached to this proposal.
