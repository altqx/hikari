---
status: accepted
---

# Require behavioral and native evidence for completed capabilities

In the live review on 2026-09-27, altqx accepted the [rewrite test strategy](../qt/testing.md): CTest coordinates GoogleTest, QtTest, Qt Quick Test, Spix, independent native accessibility checks, qualified visual goldens and calibrated native performance drivers, with fixtures tied to provenance and approved behavior. Applicable merge checks may allow code into `qt` while a named native/reference-runner follow-up remains, but the affected implementation ticket and capability stay incomplete until all required evidence passes; this permits incremental integration without presenting missing qualification as completion. Tool/session qualification, reference-host bindings and release gates remain mandatory future work, and this decision claims no native, accessibility or performance pass.
