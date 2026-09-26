# Qt rewrite specification

This is the accumulating specification for [Wayfinder: rewrite HikariSub on Qt 6 + QML](https://github.com/altqx/hikari/issues/1), on the long-lived `qt` branch. It is not yet the complete migration specification or an implemented application. Research and runnable throwaway artifacts remain on their linked branches; `main` remains the wx bug-fix line.

| Area | Captured decision | Specification |
| --- | --- | --- |
| Application foundation | Hikari-owned Qt layer; no wholesale Muse framework adoption | [Architecture](architecture.md), [ADR](../adr/0001-hikari-owned-qt-layer.md) |
| Localisation | Full TS authoring migration with QM runtime | [Localisation](localisation.md), [ADR](../adr/0002-ts-localisation.md) |
| Visual language | Compact Studio styling; current-program default panel arrangement | [Visual language](ux/visual-language.md) |
| Workspace ownership | Shared workspace; optional protected comparison and Home; movable/floating tools with follow/pin targets | [Workspaces](ux/workspaces.md); remaining IA choices are explicit |
| Subtitle grid | Custom-painted renderer | [Grid contract](ux/subtitle-grid.md), [ADR](../adr/0003-painted-subtitle-grid.md) |
| Vocabulary | Shared domain vocabulary, including Line, Document/Session, Line group and Unconfirmed | [Root glossary](../../CONTEXT.md) |
| Compatibility | Named approval for departures; individually approved defect fixes become default | [Compatibility](compatibility.md), [ADR](../adr/0005-observable-compatibility-and-defect-approval.md) |
| Qt and platform support | Qt 6.11.2 now, verified 6.12 adoption, then newer Qt with explicit Windows 10 retirement later | [Platform policy](platform-policy.md), [ADR](../adr/0004-qt-baseline-and-platform-support.md) |
| Automation boundary | Separate process for Lua; helper lifetime, IPC feasibility and UI details remain open | [Automation](automation.md), [ADR](../adr/0006-isolated-lua-automation-host.md) |
| Subtitle fonts | libass-authoritative previews and verified renderer/font-file agreement | [Fonts](fonts.md), [ADR](../adr/0007-renderer-verified-subtitle-fonts.md); native feasibility remains required |
| Build mechanism | Pinned vcpkg/overlays and official Qt provisioning through CMake workflow presets | [Build](build.md), [ADR](../adr/0008-stateless-cmake-workflow.md); clean reconstruction remains unproved |

Figma stays on Starter and is reserved for occasional handoff. UX decisions use runnable QML/HTML prototype tickets and actual user reactions. Native correctness, performance and accessibility must be verified in Qt even when the design was reviewed in HTML.

The map remains the index of decisions and open dependencies. The [coverage ledger](coverage.md) checks the full destination and each inventory surface against actual accepted evidence. Outstanding work includes the remaining information architecture, core/data/time/undo/thread design, build/platform and backend choices, per-surface UX, compatibility/performance/test gates, release policy and the final agent-sized implementation plan. Accepted decisions above should not be asked again unless new evidence requires explicitly reopening them.

## Proposals awaiting decisions

The [video pipeline](proposals/video-pipeline.md) and [audio pipeline](proposals/audio-pipeline.md) are review proposals based on completed research. Their engine and clock choices have not been accepted.

The [performance budgets](proposals/performance-budgets.md) await decisions. The retained [compatibility review ledger](proposals/compatibility-policy.md) lists candidate defects whose individual dispositions remain unapproved; accepting the policy did not accept those fixes.
