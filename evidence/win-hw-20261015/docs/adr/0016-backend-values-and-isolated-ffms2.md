---
status: accepted
---

# Own backend resources behind value ports and qualify isolated FFMS2 first

In the live review on 2026-09-27, altqx answered **“#62 yes”** to [Define backend interfaces, capabilities and resource ownership](https://github.com/altqx/hikari/issues/62), accepting the [reviewed contract](https://github.com/altqx/hikari/blob/752588a7f8146f4bd94bf9b185524875d8672af9/docs/qt/proposals/backend-interfaces.md) and its recommended isolated FFMS2 qualification direction. Application ports exchange typed values, capabilities, explicit outcomes and owned leases; adapters retain native handles, serialized owners and release responsibility, while application publication validates request identities/generations. The [backend contract](../qt/backends.md) records those responsibilities and distinct decode, scene, GPU and output-quiescence evidence.

Qualify an isolated FFMS2 media helper first, with Qt general playback in the application process, rather than initially engineering a common compatible FFmpeg closure. Existing Windows evidence loaded different hashes of same-named FFmpeg DLLs in separate processes, leaving shared-process coexistence unproved; isolation avoids relying on that assumption at the cost of IPC transfer/leases, startup, memory, crash recovery and packaging work. This is the accepted initial direction conditional on native, performance and deployment qualification, not a proven shipping process implementation, an assertion that every shared build must fail, or permission for silent architecture fallback. It is independent of Lua helper lifetime.

Concrete APIs/IPC, measured capacities, supported-platform packaging and all native gates remain to be substantiated. This decision does not approve the pending draft/transaction/lifecycle proposals, change script APIs or compatibility policy, or convert incomplete seek, audio-drain, font-replay or rendering evidence into a pass.
