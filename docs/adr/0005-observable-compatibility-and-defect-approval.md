---
status: accepted
---

# Make explicitly approved compatibility fixes the default

In the live Wayfinder review on 2026-09-27, altqx chose **“Approved fixes become default”** for [observable compatibility and legacy-defect policy](https://github.com/altqx/hikari/issues/38), rather than requiring users to opt into each corrected behavior. The [compatibility contract](../qt/compatibility.md) classifies required compatibility, explicit defect fixes, UX redesign and unsupported input/recovery; every departure still needs explicit user approval of a named entry or enumerated batch, while original-data preservation and unchanged-script commitments remain in force. This accepts the policy only: C01–C08 and the existing Lua stubs, validation and dialog-argument differences remain unresolved, and no upstream semantic substitution or executed parity pass is implied.
