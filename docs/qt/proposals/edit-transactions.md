# Edit drafts, commands and undo: partial decisions

For [Define edit drafts, commands and undo transactions](https://github.com/altqx/hikari/issues/43). **Partially accepted, 2026-09-27:** the live review explicitly approved both **C07-atomic-rejection** and **C43-save-identity**, defined below. Under the [compatibility policy](../compatibility.md), these corrected outcomes become the default when implemented and validated. Approval is not an implementation or qualification result. All other transaction proposals remain pending unless separately accepted: draft navigation/reconciliation, Save inclusion, typing/gesture grouping, Undo context and Lua behavior are not approved by this batch.

Enter commits and advances outside IME composition; editable hidden tags are already required. The native editor's mapping-policy questions remain separate. This document connects the accepted [document](../document-model.md), [time](../time-semantics.md) and [workspace](../ux/workspaces.md) contracts without approving unlisted departures.

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
| Bulk timing/style/search | Preview the declared scope, reconcile conflicting drafts, then one validated commit. These review/grouping choices remain proposed; whole-operation rejection without mutation is accepted under C07-atomic-rejection below. |
| Undo while editing | Draft Undo while that editor has draft history; once exhausted, make the transition to document Undo visible in the action label. History tool entries always identify the Document and committed operation. Never make a native context menu operate an unrelated hidden stack. |
| Document Undo/Redo | Restore content and the entry's active/selected/marker Line IDs; resolve deleted/restored identities explicitly. Recommend restoring useful Grid position, while playback position, audio ownership, workspace geometry and other documents stay separate. Reconcile affected drafts before moving history. |

**Accepted C43-save-identity, 2026-09-27:** save points identify the exact committed content written, not a mutable list index or merely the latest revision number. Undoing to that content clears committed dirty state; unsaved drafts still count as pending work. Retain the saved-content identity when history pruning or a new branch removes the old saved history entry. Preserve the existing 500-state capacity initially; this does not preserve the old dirty-state behavior. Legacy saved indices can be forgotten by pruning or redo truncation, and content equality was not established as the old rule. Retain old/new characterization fixtures for that difference. This approval does not select a new history-retention budget, native Undo grouping or persistence of in-memory undo through restart.

## Save, close and recovery

Recommend Save commits reviewed valid drafts for that Document before encoding, as explicit named history entries; an invalid/composing/conflicted draft blocks that save with a direct resolution path. This avoids a success notification that omitted visible edits. Saving an older immutable snapshot while newer edits arrive may succeed, but only that written snapshot becomes the saved point. Export has an explicit role/scope and reports whether drafts were included. It never silently marks the source Document saved.

Close/quit names both unsaved committed content and pending drafts. Save, Discard and Cancel act on that complete scope; Cancel preserves state. Recovery must retain committed content plus recoverable pending drafts separately, without pretending the drafts were saved into a legacy subtitle file. The on-disk recovery/session format and autosave browser are separate decisions.

## Automation and named compatibility boundary

The accepted helper process cannot own application history. Proposed staged results carry captured Document/revision/selection and apply atomically after reconciliation. Cancel/crash before authorization discards staged edits; an already committed result is undone normally. External script I/O is outside document rollback. Helper lifetime, target reservation and cancellation escalation remain live automation decisions.

**Accepted C07-atomic-rejection, 2026-09-27:** a rejected command leaves content, selection, dirty state and history unchanged. Old timing code can shift earlier rows before a later exact-timebase prerequisite fails; the approved default rejects that whole operation without those partial mutations. Preserve a fixture documenting the old outcome and validating the new one. This approval does not change Lua mutable validation/callback ordering, implement undo/export-filter stubs, or promise rollback of external script effects.

C06 `set_undo_point`/`register_filter` stubs, C07 mutable validation/callback order and C08 button-ID behavior remain unchanged unless their own named outcome is approved. One proposed macro history entry is not authority to implement upstream undo semantics. No implementation or parity pass is attached to this proposal.

## Accepted draft, reconciliation and history policy (2026-09-29)

altqx settled the remaining choices in a live review. With C07-atomic-rejection and C43-save-identity above, this closes [Define edit drafts, commands and undo transactions](https://github.com/altqx/hikari/issues/43).

- **Commit on leave.** At most one pending draft exists per Document: the current Line's. Enter commits and advances (outside composition). Moving to another Line, switching tabs or Documents, or reparenting the editor panel commits the draft as one undo step. Esc discards it. Native composition is finished or cancelled first, never silently dropped. This supersedes the earlier per-Line retained-draft recommendation.
- **Commands commit the draft first.** A command that touches the draft's Line (time shift, replace all, style change, a macro) first commits the draft as its own undo step, then runs as a second step. Nothing typed is lost, and Undo removes them separately. A rejected command (C07) still leaves the just-committed draft step in place.
- **Save commits, then saves.** Save, Save As and Save-on-close commit the pending draft first, so the written file matches the view. The written snapshot is the save point (C43). Edits made while a write is in flight make the Document dirty again, without changing that save point.
- **Undo restores content and selection.** Each history step records its content and the selected/active Lines, which Undo and Redo restore. Video position, scroll, panel layout and media ownership are not history state.
- **One user action, one step.** A committed draft, one drag or nudge gesture, and one bulk command over many Lines are one step each. In-field Ctrl+Z works while typing; after commit, typing is not separately undoable. The 500-state capacity is retained.

Macro runs apply as one step through the automation host; see [automation](../automation.md). Implementation evidence stays required: **T43-draft** commit-on-leave across navigation, tabs, panel moves and IME composition; **T43-reconcile** command-over-draft ordering and C07 rejection; **T43-save** commit-then-save with edits during an in-flight write; **T43-history** selection restoration, gesture/bulk grouping and the 500-state cap with C43 identity.
