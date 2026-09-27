---
status: accepted
---

# Run Lua automation in a separate process

In the live Wayfinder review on 2026-09-27, altqx chose **“Separate process; prioritize isolation”** for [the QML automation host](https://github.com/altqx/hikari/issues/33), accepting a helper-process boundary over the proposed in-process worker despite the additional native-module, media and GUI bridging work. The [automation contract](../qt/automation.md) keeps Lua-facing calls synchronous, leaves Qt objects and authoritative document application in the main process, and requires native proof of unchanged-script behavior; process containment does not restrict ordinary Lua/FFI filesystem or network permissions. This accepts the process boundary only: helper lifetime, manager placement, exact cancellation/undo compatibility and C06–C08 dispositions remain open, with no implemented sandbox or passing compatibility result implied.

## Follow-up decision — 2026-09-27

Altqx selected **separate persistent state per loaded script**. Preserve each loaded script's Lua globals and module state across invocations in its own Lua state. This rules out a single shared Lua state or fresh per-invocation states. It does not by itself choose one shared helper versus a helper per script, macro concurrency, cancellation escalation or C06–C08 behavior. Those remaining choices and the bounded native evidence are recorded in the automation contract.

In the next review, altqx selected **one persistent helper process per loaded script and one active macro application-wide**, with explicit restart and no automatic rerun of an interrupted macro. This bounds a crash/force-stop to the affected script's state at the cost of additional processes and memory. Cancellation deadlines, transactions and remaining Lua compatibility subcases are still separate decisions; resource and full unchanged-script qualification remain required.
