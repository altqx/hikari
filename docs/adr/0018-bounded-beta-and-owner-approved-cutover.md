---
status: accepted
---

# Bound public betas and require owner approval for stable cutover

On 2026-09-27, altqx accepted bounded opt-in betas under applicable workflow, safety and package checks, with incomplete capabilities clearly disclosed and every stable gate retained. The [readiness policy](../proposals/cutover-readiness.md) requires isolated legacy/Qt profiles and independently tested rollback; this allows feedback before full qualification without treating beta use as passed stable evidence.

Amended by [ADR-0021](0021-opt-in-offer-of-the-rewrite-to-legacy.md): when wave 6 ends, `main` becomes `legacy` and `qt` becomes `main`, before the stable cutover, and Qt betas install over legacy rather than beside it.

The first stable Qt release, not the branch rename, needs every stable gate to pass for a concrete candidate and the owner's approval. Preserve the old line and recovery material; wx then becomes an archive unless a scoped support line is commissioned. Policy acceptance authorizes no release, branch operation or Windows 10 retirement, and rollback never silently downgrades Documents.
