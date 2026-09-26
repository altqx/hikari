# Isolated Lua automation host

**Partially accepted, 2026-09-27.** The user chose **“Separate process; prioritize isolation”** in the live Wayfinder review. [ADR 0006](../adr/0006-isolated-lua-automation-host.md) records that process boundary. It supersedes the in-process recommendation in the [preparation proposal](proposals/automation-host.md); it does not approve every lifetime, transaction or UI detail below. [Choose how automation dialogs render in QML](https://github.com/altqx/hikari/issues/33) remains partially accepted pending manager placement, compatibility decisions and native feasibility evidence.

## Accepted boundary and compatibility

Lua execution and native automation modules belong in a helper process, outside the Qt application process. The application owns authoritative Documents, application commands, Qt objects and GUI lifetime. Keep the Lua-facing API synchronous and existing scripts usable unchanged; the GUI must remain responsive while a script awaits a dialog or host service.

Preserve the LuaJIT-compatible runtime, native-module/FFI interface, module paths, HikariSub extensions and script state semantics. Process separation is not permission to replace LuaJIT with plain Lua or remove DependencyControl. The [pinned host research](https://github.com/altqx/hikari/blob/a99a2470e958437290de73dd2451ee57cc763336/docs/research/lua-automation-host.md) inventories these requirements and the old host's shared globals/direct GUI access; it did not execute an isolated host.

**Process containment is not a security sandbox.** Lua/native/FFI filesystem and network access retain ordinary operating-system permissions. Terminating the helper cannot roll back file writes, network requests or other external effects. No additional permission system or restricted scripting API is accepted here.

The [compatibility contract](compatibility.md) still governs observable changes. C06–C08 remain unapproved: `set_undo_point` and `register_filter` stubs, mutable validation, callback invocation order/count, and truncated dialog button-ID arguments must not silently acquire upstream Aegisub semantics. Ignored float-step behavior and alpha-field interpretation also require characterization before change. Approved fixes become default only after their own named decisions.

## Proposed helper lifetime and concurrency

Start with **one long-lived helper containing separate persistent Lua states for loaded scripts**, and one active macro application-wide. This is a recommendation to preserve script globals/module lifetime across invocations while keeping ordering tractable, not a lifetime or concurrency decision already made by choosing process isolation. Load, registration, validation, active-state callbacks and execution access each state through its serialized owner.

A helper crash loses those in-memory states. Proposed recovery marks affected scripts unavailable and offers an explicit restart/reload; it never automatically reruns the interrupted macro. Reloading executes script top-level code, so it must be visible rather than disguised as continuation. Per-script helpers or different restart/lifetime policies remain alternatives to evaluate against unchanged scripts and resource measurements.

## Versioned IPC and service ownership

Use a typed, versioned protocol with a startup handshake, process-session generation, run ID and request ID. An incompatible helper must fail clearly before a script runs. Messages must distinguish commands, replies, progress/log events, cancellation and terminal outcomes; late, duplicated or unknown replies cannot be applied to another run.

| Service | Boundary |
| --- | --- |
| Dialogs and file pickers | Helper decodes Lua arguments using the compatibility adapter. App creates fixed QML control types from plain data and returns typed values; no script-generated QML. Preserve grid cells/spans, hints, names, coercions and false/nil distinctions. |
| Document and selection | App supplies a snapshot with Document identity/revision and explicit target, selection and active Line. Helper maintains a script-visible staged view; app alone validates and applies results. |
| Media, frames and audio analysis | Requests identify the captured media context, frame/time/range and generation. Return owned data or scoped transferable buffers with explicit dimensions, strides, format and lifetime. Helper-side userdata must preserve existing pixel/sample access semantics; do not transfer application pointers or decoder handles. |
| Fonts, text measurement and localisation | App-owned services use the captured configuration/revisions and return values in the existing Lua units/types. The accepted TS/QM bridge preserves `gettext` behavior. |
| Clipboard, status and editor helpers | App dispatches GUI-affine operations on the GUI thread. Define ordering and returned values explicitly; helper never dereferences Qt objects. |
| Progress and logs | Drain asynchronously with bounded queues/backpressure. Coalescing display updates must not alter script return behavior or discard required diagnostics. |

The [current dialog decoder](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp), [GUI/progress bridge](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp) and [host helpers](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp) supply the compatibility evidence, not an existing IPC implementation.

Recommend a dedicated local protocol channel and separate captured stdout/stderr diagnostics. Native modules can write directly to stdio; mixing those bytes with protocol messages is invalid. Any stdio-based experiment must first separate script/native output reliably. Define framing, payload/schema validation, maximum message/buffer sizes, outstanding-request limits and bounded log handling; numeric limits and transport/encoding remain to be chosen from fixtures. Truncated, malformed or incompatible messages produce a failed session, never partial document application. Large frame/PCM transfers need measured copying/shared-buffer ownership and cleanup, not an assumed zero-copy path.

## Synchronous calls, staged edits and cancellation

The helper may wait for a correlated host reply while its IPC dispatcher can still receive cancellation. The app continues its event loop; it must never synchronously wait for Lua while Lua waits for a GUI request. Resolve each dialog request once on accept/reject/close/cancel or helper loss, and restore focus to a surviving initiating control. Pending GUI requests cannot outlive their run.

Propose reserving the target against conflicting edits while a macro owns its staged transaction. A protected reference remains non-mutable. Before applying any result, validate process/run generation, Document identity/revision, target permissions and operation invariants. Reject stale results instead of retargeting them. Apply an approved result atomically through the app's command/undo boundary; do not stream partial authoritative document writes from the helper.

On Cancel, latch the run as cancellation-requested and send cooperative cancellation; resolve outstanding dialogs through their specified cancellation path. After a bounded grace period, terminate an unresponsive helper process. Grace periods, escalation notification and shutdown deadlines require a decision; no timeout seconds are selected here. Native/FFI calls need not respond to Lua hooks, and killing a C++ thread is not the cancellation mechanism.

Cancellation before commit authorization, helper crash or protocol loss discards staged edits and restores the pre-run document/selection/dirty/undo state. Serialize completion and cancellation so a late success cannot commit after cancellation; an already completed atomic commit remains an ordinary undoable edit. These are required outcomes for the isolated transaction design **to validate**, not approval of new legacy undo/validation semantics: exact return values, undo grouping, mutable-validation effects and rollback differences remain C06–C08 decisions before implementation acceptance. GUI and external side effects need their own defined handling; document rollback does not reverse arbitrary script I/O.

Shutdown stops new runs, resolves pending requests, attempts cooperative exit, then applies the chosen bounded process-termination policy. Reap the helper and release IPC/shared resources. Distinguish script error, cancellation, crash, protocol failure and normal completion in diagnostics. Restart creates a new generation and cannot accept old replies or silently recover lost script globals.

## Remaining decisions and required proof

Automation menu/manager placement remains a concrete UI decision: the proposed task window versus persistent tool panel has not been chosen. Preserve load/reload, macro discovery, enabled/active state, script errors/editing, rerun-last and shortcut management while reviewing that surface. Process isolation does not decide shell grouping.

Before accepting the complete host, demonstrate unchanged bundled scripts, long-lived globals, native modules/FFI and the shipped DependencyControl; all dialog classes/spans, Unicode/IME, keyboard and assistive access; media/frame/audio/font/clipboard IPC with correct units and lifetime; target/revision races and validation side effects; cooperative cancel, stuck native calls, crash/restart and shutdown while a dialog waits; malformed/truncated/oversized IPC, stdout/log floods and resource cleanup; and document/selection/undo outcomes under the approved compatibility ledger. Measure helper startup, transfer cost and memory growth separately. **No implementation, native compatibility run, isolation test or performance pass is claimed.**
