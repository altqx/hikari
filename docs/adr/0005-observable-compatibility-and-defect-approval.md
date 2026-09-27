---
status: accepted
---

# Make explicitly approved compatibility fixes the default

In the live Wayfinder review on 2026-09-27, altqx chose **“Approved fixes become default”** for [observable compatibility and legacy-defect policy](https://github.com/altqx/hikari/issues/38), rather than requiring users to opt into each corrected behavior. The [compatibility contract](../qt/compatibility.md) classifies required compatibility, explicit defect fixes, UX redesign and unsupported input/recovery; every departure still needs explicit user approval of a named entry or enumerated batch, while original-data preservation and unchanged-script commitments remain in force. This accepts the policy only: C01–C08 and the existing Lua stubs, validation and dialog-argument differences remain unresolved, and no upstream semantic substitution or executed parity pass is implied.

## Addendum — later approvals on 2026-09-27

After the policy decision above, altqx approved six narrow outcomes in the document and timing batches: **C03-preservation, C02-loss-preview, C05-audio-association, C01-equality, C01-fps-isolation and T42-A audio frame alignment**. The [approved-departure ledger](../qt/compatibility-decisions.md) preserves each trigger, exact outcome and approval scope; these approvals do not accept whole mixed candidate rows or unrelated parser, Lua, validation, dialog or late-rejection changes. Corrected behavior becomes default when implemented and validated; both old evidence and new expectations remain required, and this addendum records no implementation or parity pass.
