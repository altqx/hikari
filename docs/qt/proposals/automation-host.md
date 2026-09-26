# Proposed QML automation host

For [Choose how automation dialogs render in QML](https://github.com/altqx/hikari/issues/33). **Reviewed alternatives: the user chose a separate process on 2026-09-27, superseding the in-process recommendation below.** The [automation contract](../automation.md) records the accepted boundary and remaining decisions. The [Hikari-owned architecture](../architecture.md), [domain vocabulary](../../../CONTEXT.md), unchanged-script requirement, [TS/QM localisation](../localisation.md) and compatibility approval rule are settled. Host concurrency, interaction details and the [candidate compatibility departures](compatibility-policy.md) remain proposals.

Evidence is the [completed Lua-host research at a99a2470](https://github.com/altqx/hikari/blob/a99a2470e958437290de73dd2451ee57cc763336/docs/research/lua-automation-host.md), based on pinned HikariSub source. It found a worker macro runner, queued GUI requests and semaphore waits, but also direct GUI access and shared host globals. That supports retaining synchronous Lua semantics through a new boundary, not reusing the old threading assumptions.

## Recommended execution and request bridge

Use an **in-process, worker-owned LuaJIT host with one active macro application-wide initially**. Each loaded script keeps its Lua state; load, registration, validation, active-state callbacks and execution access that state through one serialized owner. Preserve module loading, FFI, extensions and script globals; replacing LuaJIT with plain Lua is not transparent. Native-module ABI and bundled DependencyControl behavior need their own fixtures. [Runtime/module evidence](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationUtils.cpp).

At invocation, capture the explicit document/tool target, selection, active line and media context. Reject mutation of a protected reference. Keep the run bound to that target even if focus changes; do not let scripts accidentally address a newly selected tab. Propose reserving that document against conflicting edits/close while the run owns its transaction, with the target and busy state visible. Application transaction ownership is settled; exact locking and cancellation outcomes still need review.

For a dialog call:

1. The Lua owner decodes arguments using the compatibility adapter into an immutable typed request containing run/request identity, controls, layout, buttons and initial values. No Lua stack pointers or GUI objects cross threads.
2. A queued GUI controller creates the dialog from fixed component factories. The worker awaits its result; the GUI never waits on that worker or executes Lua through a nested callback.
3. Accept, reject, close, cancellation and shutdown resolve the outstanding request exactly once. Return typed data to the owner, which constructs the Lua return values. Ignore stale replies by request identity; restore focus to a surviving initiating control.

Route clipboard, status, file pickers and editor helpers through the same ownership boundary. Snapshot or explicitly request their state rather than reading GUI objects from the Lua thread. The existing [progress/dialog bridge](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationProgress.cpp) supplies the behavioral reference.

## Typed controls and compatibility

Map the existing [dialog schema](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/AutomationDialog.cpp) to themed QML Controls: labels; single/multiline text; integer/float fields; dropdowns; checkboxes; color/coloralpha fields; and the existing alpha text control. Preserve grid-cell coordinates/spans, names, hints, defaults, coercions and returned types. Use scrolling for large layouts and accessible labels/focus order. Instantiate typed components, never script-generated QML source.

Preserve synchronous `dialog.display` results, name-keyed values, custom labels, close/cancel returning false plus readback, and file-picker nil on cancellation. Do not silently enable the currently truncated third button-ID argument, repair ignored float-step behavior or reinterpret alpha values. Unknown/malformed controls retain characterized errors.

Likewise, retain the current `set_undo_point` and `register_filter` stubs until individually approved changes exist. Validation currently receives mutable userdata; making it read-only, caching it, or adding speculative invocations can change scripts. Preserve characterized invocation order/count and mutations through the serialized owner; asynchronous menu feedback requires a fixture and explicit treatment under compatibility entries C06–C08. [Registration/validation source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp).

## Completion, progress and cancellation

Propose staged document edits and one application undo transaction on successful macro completion, consistent with the existing host's macro-level edit. Exact rollback, selection, dirty-state and validation-mutation outcomes remain compatibility fixtures; this proposal does not authorize new undo-point semantics. Filesystem/FFI side effects are outside document undo.

Marshal progress/title/task/log events to the GUI, coalescing presentation without changing returned API values or discarding required diagnostics. Cancel requests set a cooperative flag, resolve pending dialogs through their defined cancellation path and prevent a late success from committing. Cancellation and script failure have distinct terminal states; errors retain useful script context.

An instruction hook may assist pure-Lua cancellation, but an arbitrary native/FFI call need not return promptly. **Stopping a C++ thread is not a safe Lua cancellation mechanism.** Keep ownership alive until the executor exits; shutdown must not destroy its live state. A worker thread supplies no filesystem/FFI isolation.

## Placement, choices and evidence

Propose an **Automation menu plus manager task window** for load/reload, script errors/editing, rerun-last and shortcut management; registered macros remain discoverable commands with dynamic enabled/active states. Progress belongs in the task/status area; script dialogs remain schema-defined task dialogs. This grouping preserves the [inventoried capabilities](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md) and awaits IA review.

Human choices now: **in-process cooperative cancellation (recommended), or process isolation requiring additional FFI/media compatibility work?** Independently, **manager task window (recommended), or persistent tool panel?** Neither approves legacy API fixes.

Required native fixtures: all controls/spans, Unicode/IME, keyboard/readers, sequential dialogs, stale replies, cancel/error/shutdown while waiting, uncooperative native calls, target switching, undo/selection rollback, validation side effects, unchanged bundled scripts and DependencyControl. Record observed versus expected results; none is claimed passed here.
