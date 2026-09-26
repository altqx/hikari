---
status: accepted
---

# Verify subtitle font collection against the renderer

In the live review on 2026-09-27, altqx chose **“Verify renderer/font-file agreement”** for [font enumeration and matching](https://github.com/altqx/hikari/issues/35). The [font contract](../qt/fonts.md) uses provider-aware identities, document-matched libass ASS previews and verified collection of actual bytes/faces and fallback provenance; Qt picker labels alone are insufficient. This prioritizes trustworthy collection over a simpler best-effort guarantee and accepts provider-adapter and diagnostic-instrumentation maintenance, potentially including a narrow libass hook if public diagnostics are insufficient. Native identity feasibility and per-surface UI review remain required; no platform agreement or collection-completeness pass is claimed.

After reviewing the Windows identity experiment, altqx accepted maintaining the narrow diagnostic hook and separate captured/missing/fallback/incomplete statuses. The experiment demonstrated selected-byte/face capture and exposed a multilingual reimport failure; Linux, fallback reconstruction and production lifetime/ABI qualification remain required rather than waived by this acceptance.
