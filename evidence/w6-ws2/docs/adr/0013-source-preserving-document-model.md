---
status: accepted
---

# Preserve source representation alongside the authoritative Document

In the live Wayfinder review on 2026-09-27, altqx accepted the full [subtitle document model and format-boundary contract](../qt/document-model.md) for [Design the subtitle document model and format boundaries](https://github.com/altqx/hikari/issues/41), following approval of its three named product outcomes. Ordered typed and opaque records, stable Line identities, explicit text roles and retained source bytes provide one authoritative content model without inheriting the old wx class structure or treating grid rows as document identity; application state owns selection, targets, resource context and undo, with versioned value snapshots crossing the isolated Lua boundary.

Same-format preservation and conversion use separate revision-bound plans. The accepted C03-preservation, C02-loss-preview and C05-audio-association outcomes retain unknown data, expose conversion losses while retaining the source Document, and prevent omitted Audio from inheriting another tab's association. Standard C++ value records and decoded UTF-8 keep Qt object ownership outside the core. Acceptance does not approve unrelated parser normalization, Lua semantics or group-breaking command behavior, and does not establish byte/semantic parity; those boundaries and required fixtures remain explicit in the contract and [approved-departure ledger](../qt/compatibility-decisions.md).
