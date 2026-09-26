---
status: accepted
---

# Run Lua automation in a separate process

In the live Wayfinder review on 2026-09-27, altqx chose **“Separate process; prioritize isolation”** for [the QML automation host](https://github.com/altqx/hikari/issues/33), accepting a helper-process boundary over the proposed in-process worker despite the additional native-module, media and GUI bridging work. The [automation contract](../qt/automation.md) keeps Lua-facing calls synchronous, leaves Qt objects and authoritative document application in the main process, and requires native proof of unchanged-script behavior; process containment does not restrict ordinary Lua/FFI filesystem or network permissions. This accepts the process boundary only: helper lifetime, manager placement, exact cancellation/undo compatibility and C06–C08 dispositions remain open, with no implemented sandbox or passing compatibility result implied.
