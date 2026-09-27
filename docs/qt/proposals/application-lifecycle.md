# Document and Session lifetime: partial decisions

For [Design Document and Session lifetime, recovery and background work](https://github.com/altqx/hikari/issues/58). **Partially accepted, 2026-09-27:** the live review explicitly approved all three named outcomes **L58-write-close**, **L58-staged-replacement** and **L58-recovery-copy**, defined below. They become the default when implemented and validated under the [compatibility policy](../compatibility.md). This is not approval of the entire lifecycle proposal or a native qualification claim.

The document extends the accepted [architecture](../architecture.md), [document model](../document-model.md), [media ownership](../media.md) and [Workspace](../ux/workspaces.md). [Edit transactions](edit-transactions.md) now separately accepts C07 atomic rejection and C43 saved identity; its remaining draft navigation, Save inclusion, grouping and Undo-context choices are not settled here. Other lifecycle choices, including destination collisions, retention/cleanup and partial Session/closed-pin behavior, remain pending.

## Source boundary

The corrected [data inventory][data] and [core inventory][core] distinguish static observations from runtime evidence. Legacy [Session saving/loading][session] stores tab paths, video position, active Line, scroll and editor/provider flags, without draft/history archives. Recovery can substitute a newer autosave, restore the original path and mark it dirty. [Autosaves][autosave] are ordinary subtitle files discovered by filename and modification time: **there is no old autosave index**.

[Open/reload][open] coordinates widgets and associated-file prompts; external-change detection asks before reloading, while a removed file becomes unsaved. [Save/close][save] uses void [save][savefile]/[writer][writer] paths: a save-dialog cancellation is not propagated by SavePrompt, and SaveFile can mark saved after logged write errors. These are source-backed risks, not reproduced failures. Existing [worker shutdown][workers] joins media threads; [callbacks][callbacks] include queued GUI work and an alive-token guard. This is not evidence that every callback or shutdown race is safe.

## Ownership and interfaces

These are responsibilities, not mandatory class names or new domain terms:

| Owner | State and lifetime |
| --- | --- |
| Application Document registry | DocumentId, content/source snapshot, revision, history, saved identity, drafts and per-Document selection/view state. Owns Documents independently of tabs/panels. |
| Resource coordinator | Each Document's authored association, provenance, resolved location, missing status and resource generation. Adapters own actual decoder/device handles; cache sharing never shares mutable playback state implicitly. |
| Session coordinator | Ordered Document entries, restorable positions, editing/reference roles and tool-pin identities. Workspace geometry remains separate. A Session is neither subtitle content nor an undo archive. |
| File/recovery service | Destination reservations, write permits, results, immutable recovery generations and retention. It cannot mutate content or decide to commit drafts. |
| Task coordinator | Job identity, dependencies, progress, cancellation and terminal result; retains resources until workers acknowledge release. UI disappearance never substitutes for cancellation. |

Conceptual ports:

- open(request, lifetimeToken) → staged LoadResult; activate(result, expectedState) publishes it once.
- prepareSave(DocumentId, revision, destination, options) → revision-bound SavePlan; write(snapshot, plan, permit) → Written / Cancelled / Failed / DurabilityUncertain.
- captureRecovery(DocumentId) → committed snapshot + separate draft records + resource/view metadata; publishRecovery(bundle, generation) → activation result.
- requestClose(ids) → ClosePlan; resolve(plan, choices) → cancelled / waiting / closed.
- startJob(inputSnapshot, dependencyTokens) → JobId; cancel(JobId) requests cancellation; completion carries identities and outcome.

Standard C++ immutable value snapshots cross core boundaries; Qt adapters marshal them. The GUI/application thread serializes state publication and permissions. Dedicated owners serialize decoder/renderer access; bounded workers parse/index/encode. Render-thread resources stay with their presenter; audio callbacks consume prepared buffers. No worker captures a QML object, active-tab pointer or mutable core reference as its target. Lua keeps its accepted isolated process and versioned value IPC; this proposal does not settle the Lua API subcases of C06–C08.

## Open, save and replacement

Document lifetime is Opening → Ready → CloseRequested → Draining → Closed, with failed open retained as a diagnostic result. Saving and resource loading are separate activities; a missing video must not disable subtitle editing. Creation allocates fresh identity and format-appropriate content without a disk path. Opening stages bytes, parser diagnostics and association hints before replacing existing state. Parser fallbacks still follow characterized compatibility until individually approved; staging does not silently replace malformed-input semantics.

Reload prepares a replacement, then reconciles committed changes and drafts. Failure or cancellation retains the old Document. Accepted reload advances its lifetime generation, resets history against the loaded content and explicitly remaps/clamps view positions; it never guesses stable Line identity from row number. Recovery may restore IDs stored together in its own snapshot; legacy import receives fresh IDs.

Paths are associations, not Document identity. Preserve original path text and provenance alongside resolution. Session Audio omission resolves from that Document's own media context or none under approved **C05-audio-association**. Missing resources remain unresolved entries with retry/relink/remove-association choices. Relinking invalidates dependent jobs/caches and timebase assumptions, without rewriting authored times. Associated-file discovery must not substitute an unrelated basename silently.

Save and Save As resolve pending work using the eventual transaction policy, then validate the format/loss plan. A valid draft is not implicitly committed by this proposal. Before writing, revalidate the captured revision, options and destination. Once authorized, newer edits may continue only if the proposed transaction rule for saving an older snapshot is accepted; otherwise hold publication of edits for that save. Only the written content becomes saved. Save As changes path/association serialization and saved identity only after success; export never marks the source saved. Expose external modification/deletion before overwrite; offer reload/review, another destination, or explicit overwrite. Timestamp equality alone is insufficient evidence of unchanged bytes.

A destination already associated with another open Document is a distinct collision. Recommend blocking Save As/export to it until the user chooses another destination or explicitly resolves both Documents; an alternative may permit reviewed replacement while marking the other view externally changed. Serializing writes alone does not resolve contradictory saved identities. This collision policy remains pending, including aliases found by the platform adapter.

## Filesystem and recovery boundary

Serialize writes per destination, including aliases discovered by the platform adapter. Write a unique same-directory temporary file, check complete encoding/write results, and recheck expected destination identity/content before replacement. Use atomic replacement without direct-write fallback. [QSaveFile][qsave] supplies temporary-file/commit mechanics, but enabling its fallback forfeits atomicity and effective cancellation. Preserve the previous valid target on pre-replacement failure; never report a saved point merely because a writer returned.

Replacement and durability are distinct. The adapter reports write, replacement and persistence outcomes separately. Qualify file flush plus directory/metadata persistence on each supported filesystem: [Linux fsync][fsync] requires a separate directory sync for directory entries; [Windows FlushFileBuffers][flush] addresses buffered file writes. Neither citation establishes every filesystem/device's power-loss guarantee. A post-replacement flush failure means “replacement occurred; durability uncertain,” not rollback or clean success. External writers still create a final-check race; do not claim cross-process compare-and-swap. Conflicted/shared destinations need explicit handling and fixtures.

New recovery bundles are versioned application data, **not a changed legacy subtitle/session grammar**. Direct readers retain LastSession.txt, .kls and subtitle recovery inputs unchanged. Each complete generation identifies the committed snapshot, source bytes/raw spans, separate draft bases/changes, saved-file fingerprint and resource/view metadata. Draft recovery restores pending state, never silently commits it; native IME preedit is transient and cannot be promised after a crash. No complete undo archive is implied.

Write and verify a bundle before atomically activating its manifest entry. Keep the preceding valid generation until activation succeeds; reject incomplete/corrupt generations and report the fallback. Retention runs afterwards, never removing the last valid or actively referenced generation. Preserve configurable legacy autosave capacity as an input; its 0/1 edge behavior and new retention limits require explicit disposition. Legacy readers scan the selected old Subs/Recovery and Session files without modifying them or inventing a manifest.

**Accepted L58-recovery-copy, 2026-09-27:** recover as a new unsaved copy carrying provenance, with original and recovery files untouched. Restore committed content and recoverable drafts separately; drafts refer to that copy's committed base and remain pending. Restoring to the original destination requires an explicit subsequent save/overwrite decision. This named approval changes the legacy original-path rebinding outcome; it is separate from C05 and does not approve retention/cleanup defaults or promise recovery of transient native IME preedit.

## Cancellation, close and shutdown

Every result names DocumentId, lifetime generation, JobId and relevant content/resource/configuration revisions. Reject late results at publication; a UI signal can be stale even after cancellation was requested. Superseded reads can finish privately. Writes additionally require a current destination permit at final replacement, so ignoring their completion alone cannot stop a stale disk mutation.

Cancel/close and replacement are ordered by the file coordinator. Before replacement authorization, cancellation revokes the permit; afterwards it waits for the actual outcome and cannot promise “nothing was written.” A newer save cannot finish before an older same-destination write and then be overwritten by it. Retain temporary-resource ownership until cleanup completes.

**Accepted L58-write-close, 2026-09-27:** closing through Save requires affirmative write success for the resolved save scope. A cancelled or failed save cannot authorize close or discard the existing work. This corrects the source-observed legacy cancellation/failure propagation risk; it does not choose which drafts Save includes.

**Accepted L58-staged-replacement, 2026-09-27:** retain existing work when open or reload fails or is cancelled. Prepare a replacement before publishing it; failure must not destroy the old Document. This does not normalize parser fallback behavior or approve a new Session format. Partial Session replacement and failure handling remain separate proposed policies.

The proposed ClosePlan names committed changes, drafts, active writes/jobs, roles and pinned tools. Discard authorizes its named scope; Cancel preserves Documents and drafts. The proposed multi-Document quit review resolves all Documents before destroying any; earlier completed saves are not undone if a later choice cancels quit. Its grouping and the remaining close/draft mechanics are not blanket-approved by the three named outcomes.

Each close choice captures Document lifetime/content revision and the draft revision/set it covers. Revalidate immediately before final destruction. If content or drafts changed after Save/Discard approval, the old choice cannot discard that new work: refresh the review or keep the Document open. The application serializes this final permission check with edit publication, rather than relying only on stale-result rejection.

After approval, stop new commands, invalidate targets, cancel jobs, stop the active transport/audio owner, drain adapter callbacks, then release backend and render-thread resources. Never terminate an in-process worker using live library state. A stuck operation reports waiting/failure; helper termination follows its separately approved protocol. Persist Session status from acknowledged outcomes; only mark orderly shutdown after required drains/publications succeed.

Recommend a closed pinned target remain explicitly unavailable until unpinned/retargeted, rather than silently following another Document. Closing a protected reference removes that role; it does not promote it or another Document into an editing target. Session restoration stages entries, offers recoverable per-entry failures, restores protected/pinned identities only when resolved and never inherits media across entries.

## Accepted outcomes, remaining review and later proof

**The three named outcomes are settled:** acknowledged successful writing before Save-close, old-state retention after failed/cancelled replacement, and recovery into a separate unsaved copy with drafts kept pending. Do not ask for them again. Their approved defaults still require characterization and implementation evidence.

Remaining choices:

1. Draft navigation/reconciliation, Save inclusion, saving an older snapshot while newer edits continue, and Undo grouping remain with edit transactions; this batch does not approve automatic draft commit.
2. Destination collisions, multi-Document quit grouping and related close mechanics remain explicit lifecycle choices. Internal permit/state representation is an engineering choice, not another approval of the named outcomes.
3. Retaining completed generations after normal close, exact count/size/age defaults, capacity 0/1 behavior and discard-versus-recovery cleanup still need their defined policy. Recovery-copy approval does not authorize deletion.
4. On partial Session restoration and closed pinned targets, keeping failed entries/targets visibly unresolved for retry remains recommended; the alternative is explicit user-approved omission/retargeting, never silent substitution.

Implementation must exercise short writes, disk full, denied/remote paths, external changes, crash points around activation, stale saves, cancel-at-replacement, reload/close during decode, missing Session entries and pinned protected targets. Native filesystem durability, close/crash behavior and shutdown have **not** been tested by this document.

[data]: https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md
[core]: https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md
[session]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Notebook.cpp#L1323-L1564
[autosave]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutoSaveOpen.cpp#L137-L191
[open]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/TabPanel.cpp#L330-L372
[save]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/HikariSubFrame.cpp#L2116-L2148
[savefile]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L308-L405
[writer]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/OpennWrite.cpp#L34-L152
[workers]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/ProviderFFMS2.cpp#L495-L547
[callbacks]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/RendererVideo.cpp#L135-L167
[qsave]: https://doc.qt.io/qt-6/qsavefile.html
[fsync]: https://man7.org/linux/man-pages/man2/fsync.2.html
[flush]: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
