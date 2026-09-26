# Qt rewrite specification

This is the accumulating specification for [Wayfinder: rewrite HikariSub on Qt 6 + QML](https://github.com/altqx/hikari/issues/1), on the long-lived `qt` branch. It is not yet the complete migration specification or an implemented application. Research and runnable throwaway artifacts remain on their linked branches; `main` remains the wx bug-fix line.

| Area | Captured decision | Specification |
| --- | --- | --- |
| Application foundation | Hikari-owned Qt layer; no wholesale Muse framework adoption | [Architecture](architecture.md), [ADR](../adr/0001-hikari-owned-qt-layer.md) |
| Localisation | Full TS authoring migration with QM runtime | [Localisation](localisation.md), [ADR](../adr/0002-ts-localisation.md) |
| Visual language | Compact Studio styling; current-program default panel arrangement | [Visual language](ux/visual-language.md) |
| Workspace and shell | Shared workspace; optional protected comparison/Home; movable/floating tools with follow/pin; Classic controls and optional task presets | [Workspaces](ux/workspaces.md); detailed surfaces continue in their own prototypes |
| Subtitle grid | Custom-painted renderer | [Grid contract](ux/subtitle-grid.md), [ADR](../adr/0003-painted-subtitle-grid.md) |
| Vocabulary | Shared domain vocabulary, including Line, Document/Session, Line group and Unconfirmed | [Root glossary](../../CONTEXT.md) |
| Compatibility | Named approval for departures; individually approved defect fixes become default | [Compatibility](compatibility.md), [ADR](../adr/0005-observable-compatibility-and-defect-approval.md) |
| Qt and platform support | Qt 6.11.2 now, verified 6.12 adoption, then newer Qt with explicit Windows 10 retirement later | [Platform policy](platform-policy.md), [ADR](../adr/0004-qt-baseline-and-platform-support.md) |
| Automation boundary | Separate process for Lua; helper lifetime, IPC feasibility and UI details remain open | [Automation](automation.md), [ADR](../adr/0006-isolated-lua-automation-host.md) |
| Subtitle fonts | libass-authoritative previews and verified renderer/font-file agreement | [Fonts](fonts.md), [ADR](../adr/0007-renderer-verified-subtitle-fonts.md); native feasibility remains required |
| Build mechanism | Pinned vcpkg/overlays and official Qt provisioning through CMake workflow presets | [Build](build.md), [ADR](../adr/0008-stateless-cmake-workflow.md); clean reconstruction remains unproved |
| Media/audio direction | Active playback mode owns audio/clock; PortAudio editor output, QSG waveform and spectrum tiles | [Media](media.md), [transport ADR](../adr/0009-active-mode-media-transport.md), [audio ADR](../adr/0010-portaudio-editor-output.md); player/presenter native feasibility remains open |
| Performance | Starting release/stretch gates on the lower hardware class | [Performance](performance.md), [ADR](../adr/0011-performance-reference-contract.md); hosts/fixtures must be bound before qualification |
| Distribution | Offline Windows installer/portable ZIP; Linux AppImage/recovery tar; notification-only updates; trusted stable Windows signing | [Distribution](distribution.md), [ADR](../adr/0012-distribution-and-updates.md) |
| Document model | Ordered source-preserving records, stable Line IDs and core value types with external Qt adapters | [Document model](document-model.md), [ADR](../adr/0013-source-preserving-document-model.md) |
| Time semantics | Signed microsecond values, rational media time and distinct interval/frame lookup operations; named fixes only | [Time semantics](time-semantics.md), [ADR](../adr/0014-typed-time-and-frame-semantics.md) |
| Verification | CTest/GoogleTest, QtTest/Quick Test, test-only Spix, native accessibility/render/performance evidence; evidence-gated completion | [Testing](testing.md), [ADR](../adr/0015-behavioral-and-native-test-evidence.md) |
| Translation/comparison layout | Stacked fields, bottom reference tray and independent navigation by default; remaining operation policies open | [Translation/comparison](ux/translation-comparison.md) |

Figma stays on Starter and is reserved for occasional handoff. UX decisions use runnable QML/HTML prototype tickets and actual user reactions. Native correctness, performance and accessibility must be verified in Qt even when the design was reviewed in HTML.

The map remains the index of decisions and open dependencies. The [coverage ledger](coverage.md) checks the full destination and each inventory surface against actual accepted evidence. Outstanding work includes command/undo and lifecycle design, native build/backend feasibility, detailed surface UX, cutover strategy, and the final agent-sized implementation plan. Accepted decisions above should not be asked again unless new evidence requires explicitly reopening them.

## Proposals awaiting decisions

The reviewed [video](proposals/video-pipeline.md) and [audio](proposals/audio-pipeline.md) alternatives led to the accepted media direction above. Qt Multimedia is the first general-player candidate to verify; chapter/track, clock, rendering and deployment gates remain explicit.

The [native docking contract](proposals/docking.md), [settings/import contract](proposals/settings-import.md) and [edit/undo transaction contract](proposals/edit-transactions.md) await decisions. Six exact outcomes from the document/timing batches are captured in the [approved-departure ledger](compatibility-decisions.md); other compatibility subcases remain open.
