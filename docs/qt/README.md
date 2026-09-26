# Qt rewrite specification

This is the accumulating specification for [Wayfinder: rewrite HikariSub on Qt 6 + QML](https://github.com/altqx/hikari/issues/1), on the long-lived `qt` branch. It is not yet the complete migration specification or an implemented application. Research and runnable throwaway artifacts remain on their linked branches; `main` remains the wx bug-fix line.

| Area | Captured decision | Specification |
| --- | --- | --- |
| Application foundation | Hikari-owned Qt layer; no wholesale Muse framework adoption | [Architecture](architecture.md), [ADR](../adr/0001-hikari-owned-qt-layer.md) |
| Localisation | Full TS authoring migration with QM runtime | [Localisation](localisation.md), [ADR](../adr/0002-ts-localisation.md) |
| Visual language | Approved Compact Studio: C compactness with A layout | [Visual language](ux/visual-language.md) |
| Workspace ownership | Shared workspace; optional protected comparison and Home; movable/floating tools with follow/pin targets | [Workspaces](ux/workspaces.md); remaining IA choices are explicit |
| Subtitle grid | Custom-painted renderer | [Grid contract](ux/subtitle-grid.md), [ADR](../adr/0003-painted-subtitle-grid.md) |
| Vocabulary | The settled workspace/editing/reference/tool-target terms | [Root glossary](../../CONTEXT.md); broader domain glossary still open |

Figma stays on Starter and is reserved for occasional handoff. UX decisions use runnable QML/HTML prototype tickets and actual user reactions. Native correctness, performance and accessibility must be verified in Qt even when the design was reviewed in HTML.

The map remains the index of decisions and open dependencies. Outstanding work includes the remaining information architecture, core/data/time/undo/thread design, build/platform and backend choices, per-surface UX, compatibility/performance/test gates, release policy and the final agent-sized implementation plan. Accepted decisions above should not be asked again unless new evidence requires explicitly reopening them.

## Proposals awaiting decisions

The [video pipeline](proposals/video-pipeline.md) and [audio pipeline](proposals/audio-pipeline.md) are review proposals based on completed research. Their engine and clock choices have not been accepted.
