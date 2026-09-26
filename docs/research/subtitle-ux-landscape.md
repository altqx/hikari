# Subtitle-editor UX landscape

Research for [#9](https://github.com/altqx/hikari/issues/9), checked 2026-09-27. Verified product/docs/source facts are separated below from user reports and proposed HikariSub experiments. This is a qualitative source survey, not a representative satisfaction survey or hands-on benchmark.

## Summary

Subtitle editors converge on a synchronized grid, text editor, media preview and timing display, but optimize different jobs. Aegisub is the strongest reference here for ASS typesetting and macro workflows; Subtitle Edit provides useful translation, quality-control and format-oriented patterns; NLEs contribute direct timeline trimming and selection-aware styling. HikariSub should preserve its parity capabilities and test these interaction patterns through QML/HTML prototype tickets. Figma remains Starter-only, for occasional hand-off.

## Aegisub and arch1t3cht

| Surface | Observed pattern / evidence | HikariSub implication (proposal) |
|---|---|---|
| Grid/list | Line-oriented selection with a separate active line and metadata editing; the arch1t3cht feature branch explicitly adds grouping/folding. [Editing guide](https://aegisub.org/docs/latest/editing_subtitles/), [pinned fork README](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/README.md) | Keep active line separate from selection; make grouping/filtering visibly distinct from deletion. |
| Edit box | ASS text and per-line fields coexist; preserve syntax and direct text access rather than hiding every override behind an inspector. [Editing guide](https://aegisub.org/docs/latest/editing_subtitles/) | Test plain dialogue and dense override-tag editing with the same shortcuts. |
| Audio/timing | Waveform and spectrum, ordinary and karaoke timing, explicit media loading/cache feedback. [Audio guide](https://aegisub.org/docs/latest/audio/) | Timing controls must explain selected interval, pending changes and commit/advance. Treat indexing as a visible operation. |
| Visual typesetting | Crosshair, drag/position/movement, Z/XY rotation, scale, rectangle and vector clips; preview readouts include frame/time and line-relative time. [Visual typesetting](https://aegisub.org/docs/latest/visual_typesetting/) | Keep script coordinates distinct from zoomed viewport coordinates, with numeric alternatives to dragging and a clear active tool. |
| Styles | Style manager separates reusable storage from styles in the current script. [Styles](https://aegisub.org/docs/latest/styles/) | Make library-to-document copy explicit and show which scope an edit affects. |
| Translation | Translation Assistant supports focused sequential translation rather than using the grid alone. [Translation Assistant](https://aegisub.org/docs/latest/translation_assistant/) | Prototype source/translation together, preserving tags, timing and unconfirmed states. |
| Keyboard | The fork adds bindable vector-clip modes and zoom/pan improvements. Its README also identifies high-DPI, spectrum and video-timing changes. [Pinned fork README](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/README.md) | Tool submodes need named commands, not mouse-only hidden states. |

The fork README is a statement about that feature snapshot, not proof that every feature remains exclusive to it. The maintainer describes a collection of feature branches, warns about force-pushing and does not promise general stability. Do not treat old upstream/fork comparisons in that README as the present release hierarchy. [Pinned README](https://github.com/arch1t3cht/Aegisub/blob/9bfd5008d9fc30bd86b3634416781d2c41ce8f89/README.md)

A direct practitioner-authored source shows why macro compatibility matters: arch1t3cht's AegisubChain records/replays macro sequences, pre-fills or suppresses dialogs and creates keyboard-friendly wrappers. This is evidence for composable workflows, not a request to expand HikariSub's Lua API. [Script author's documentation](https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts)

## Subtitle Edit: distinguish old documentation from the current rewrite

The cross-platform rewrite exists in the current `SubtitleEdit/subtitleedit` repository. At inspected commit `c795d39fa4ee2599e1f4d166d19d8dcaafb828ff`, `src/ui/UI.csproj` targets .NET 10 and references Avalonia 12.1.3. The repository documents Windows/macOS/Linux distribution. Older nikse.dk pages still describe the .NET Framework/WinForms generation; do not use those system requirements as current rewrite facts. [Project file](https://github.com/SubtitleEdit/subtitleedit/blob/c795d39fa4ee2599e1f4d166d19d8dcaafb828ff/src/ui/UI.csproj), [README](https://github.com/SubtitleEdit/subtitleedit/blob/c795d39fa4ee2599e1f4d166d19d8dcaafb828ff/README.md), [legacy help](https://www.nikse.dk/se/)

The inspected [main-window documentation](https://github.com/SubtitleEdit/subtitleedit/blob/c795d39fa4ee2599e1f4d166d19d8dcaafb828ff/docs/features/main-window.md) describes:

- A grid with start/end/duration, CPS/WPM, text and an original-text column in translation mode; active-row text and time fields sit beside/below it.
- Customizable toolbar/layouts and dockable video; waveform, spectrum or combined display with subtitle intervals, shot changes, selection and playhead.
- Direct boundary dragging, interval movement and keyboard commands, with configurable seeking and selection behavior.
- Format-specific ASSA menus for styles, properties, attachments, drawing and positioning, instead of presenting all commands for every format.
- Explicit original/reference editing modes, read-only source protection and save/discard prompts; timing-matched reference rows can remain distinct from actual output rows.

The [ASSA style guide](https://github.com/SubtitleEdit/subtitleedit/blob/c795d39fa4ee2599e1f4d166d19d8dcaafb828ff/docs/features/assa-styles.md) distinguishes file styles from storage templates, shows usage counts and preview, and explains that editing storage alone does not affect the document. This is a particularly useful guard against a common scope misunderstanding.

The current [shortcut reference](https://github.com/SubtitleEdit/subtitleedit/blob/c795d39fa4ee2599e1f4d166d19d8dcaafb828ff/docs/reference/keyboard-shortcuts.md) includes translation-mode toggling, timing, selected-line playback, waveform and quality tools, while distinguishing unbound commands, fixed dialog accelerators and imported legacy mappings. HikariSub should likewise keep command identity, context and default binding separate.

## Other editors and browser workflows

| Editor | Grid/text and translation | Timing/media | Styles/typesetting and keyboard evidence |
|---|---|---|---|
| Gaupol | Text-based subtitle creation, translation and common-error correction; GTK table/cell/dialog structure is reflected in its published API. | Built-in video player and external-player integration; timing/synchronization are advertised. A modern waveform parity claim was not established. | Treat it as a text/translation reference, not demonstrated ASS visual-typesetting parity. Its API exposes time/text editing and search/split/shift dialogs; exact current keyboard mapping was not inspected. [Project](https://otsaloma.io/gaupol/), [API structure](https://otsaloma.io/gaupol/doc/api/gaupol.html) |
| Jubler | Individual subtitle editing, split/join, undo/redo and parent/child translation mode. | Graphical intervals can be moved/resized; waveform and video preview. Current downloads require VLC for embedded preview/waveform, despite older feature-page references to mplayer/FFmpeg. | ASS/SSA style editing per subtitle/character and spelling are advertised; Aegisub-equivalent vector/rotation tools and exact keyboard map were not established. [Features](https://jubler.org/features.html), [current downloads](https://jubler.org/download.html) |
| Subtitle Edit Online | Browser list/text editing and translation-service choices; not evidence of a desktop-equivalent core. | Draw an interval then Enter to insert; list/timeline context menus, current-subtitle selection and browser-local autosave options. | Keyboard next/previous and play/pause are documented. Full ASS style/visual-tool parity is not documented on the inspected page. [Live help/UI](https://nikse.dk/SubtitleEdit/Online) |

Unknown capability means **not established by this survey**, not absent from the product. Gaupol's API page is old and is used only for structural evidence; current release/distribution claims come from its project page. No editor popularity ranking is inferred.

## NLE caption workflows

Premiere's Captions and Graphics workspace combines a searchable/editable Text panel, Program Monitor, Properties and timeline caption track. Caption clips can be adjusted with timeline editing tools; exports distinguish burned-in, sidecar and embedded outputs. Track/linked styles have explicit project/local storage and can be applied consistently. This suggests synchronized list/timeline selection plus clear styling scope; it does not justify replacing ASS override editing with NLE caption controls. [Caption overview](https://helpx.adobe.com/premiere/desktop/add-text-images/insert-captions/about-captions.html), [track styles](https://helpx.adobe.com/premiere/desktop/add-text-images/stylize-text/create-linked-and-track-styles.html)

Resolve describes subtitle tracks above video, move/trim like other media, per-language track switching, Inspector styling and separate subtitle export. This is a useful direct-manipulation timing model. These docs do not establish ASS karaoke/vector-clip equivalence or an original/translation review workflow; exact caption-specific keyboard bindings were not verified. [Resolve Edit page](https://www.blackmagicdesign.com/se/products/davinciresolve/edit)

A historical first-party Resolve guide documents track creation and setting track style before adding captions. It is a useful explanation of the selection model, but is explicitly version 15 evidence, not a current feature inventory. [Resolve 15 guide](https://documents.blackmagicdesign.com/SupportNotes/DaVinci_Resolve_15_New_Features_Guide.pdf)

## Pain points: direct reports, not prevalence estimates

| Evidence | Supported observation | Proposed test |
|---|---|---|
| [Aegisub #23](https://github.com/TypesettingTools/Aegisub/issues/23) | A request says Ctrl-wheel zoom conflicts with some workflows. | Configurable zoom/scroll bindings and clear modifier hints. |
| [Aegisub #448 maintainer reproduction](https://github.com/TypesettingTools/Aegisub/issues/448#issuecomment-3389098880) | A maintainer reproduced millisecond/centisecond rounding causing a small SRT overlap after snapping. The report's broader API/spec assertions were not accepted as facts. | Show relevant precision; verify exported boundaries after drag/snap and format conversion. |
| [Subtitle Edit #14320](https://github.com/SubtitleEdit/subtitleedit/issues/14320) | A user requests separate lanes because overlapping intervals are hard to distinguish. | Deliberate overlap fixture with three simultaneous lines, layers and filtered selections. |
| [Subtitle Edit #13935](https://github.com/SubtitleEdit/subtitleedit/issues/13935) | A report describes tooltips intercepting waveform-toolbar clicks; maintainer says repositioning did not solve it and offers disabling hints. | Tooltips must not intercept clicks or cover adjacent timing controls at screen edges. |
| [Resolve forum: linked subtitles](https://forum.blackmagicdesign.com/viewtopic.php?f=21&t=172824) | Users want subtitle timing to follow clip changes. | Define whether edits affect one interval, selection or subsequent lines; never infer ripple from a drag accidentally. |
| [Adobe forum: caption styling](https://community.adobe.com/questions-729/can-t-edit-caption-style-in-latest-premiere-pro-25-0-1413389) | Users report difficulty finding/applying styles after a panel change. | Preserve command aliases and visibly distinguish style scope; test migration discovery. |

The author-maintained Aegisub script collection is direct fansubbing/typesetting workflow evidence; public issue/forum posts provide concrete pain points, not representative praise/complaint percentages. No private community access or interviews were used. An anecdotal “users praise X” consensus would exceed this evidence.

## Recommended prototype tickets / acceptance fixtures

1. **Timing:** keyboard-only ten-line pass; pending versus committed intervals; waveform/spectrum; adjacent and overlapping lines; frame/keyframe snap with visible units; correct focus after playback.
2. **Translation:** original remains unchanged, tags preserved, missing/unconfirmed rows visible; next-untranslated and compare without a modal loop.
3. **Typesetting:** apply a document style, override one line, move/rotate/clip at zoom, return to precise text editing; distinguish the active line from multiple selected lines.
4. **Style library:** edit storage versus file style with explicit copy/apply semantics and usage counts; show missing fonts and a preview.
5. **Migration discovery:** a familiar hotkey opens the right action; old labels find renamed commands; hidden toolbar controls remain reachable by menu/keyboard.

Avoid automatic removal of “advanced” commands to make screens look simple, color-only overlap/selection indicators, pointer-blocking tooltips, unlabelled mode changes, silent source-text edits and unmeasured performance claims. These are research-backed hypotheses for user reaction, not final UX decisions or new-feature commitments.
