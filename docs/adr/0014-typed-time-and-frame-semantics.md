---
status: accepted
---

# Separate precise Document time from frame lookup and format quantization

In the live Wayfinder review on 2026-09-27, altqx accepted the full [typed time and frame contract](../qt/time-semantics.md) for [Set time and frame semantics for editing and playback](https://github.com/altqx/hikari/issues/42), following its three named fixes. Signed 64-bit microsecond DocumentTime and TimeDelta, distinct frame/sample-frame indices, rational media timing and half-open intervals separate authored timing from display, playback estimates and format output; explicit at-or-after versus containing-frame lookups prevent one ambiguous helper from serving incompatible purposes.

Preserve characterized legacy frame representatives, format quantization, karaoke and postprocessor behavior except for approved departures. C01-equality makes equal values satisfy both inclusive comparisons; C01-fps-isolation gives each Document an explicit rational MicroDVD rate; T42-A computes audio-alignment offsets from the difference between frame indices. Source frame integers and unknown/estimated provenance remain visible, and exact decoder requests return the requested index or an error. Acceptance does not approve unrelated rounding, late-rejection or Lua changes, and does not claim measured timing accuracy; implementation must satisfy the contract's old/new fixtures and the [approved-departure ledger](../qt/compatibility-decisions.md).
