# Workspace review: where existing capabilities belong

Companion to [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31). **Capability-placement proposal; accepted workspace choices are noted below.** The runnable `workspace-model.html` explores document/layout ownership. This worksheet makes the remaining placement question concrete without pretending its small sample implements every tool.

Source baseline: [complete wx UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md). Current capability names identify what must survive; they do not fix future menu wording. No capability is dropped by this proposal.

Compact Studio supplies the accepted appearance; the latest default arrangement follows the current program (video left, audio above editor right, grid below). Shared workspace ownership, optional protected comparison, movable/floating tools, follow-editing with optional pinning and optional Home have since been accepted. Detailed capability grouping remains a proposal.

## Proposed placement

| Existing capability family | Proposed home and entry | Why / question for review |
| --- | --- | --- |
| Open/save/save as/save translation; recents; encodings; drag/drop | File menu and document tabs; native pickers; explicit save/discard/cancel dialogs | Keep filename, modified state and editing target visible. A save-translation operation remains distinct from saving the document. |
| Sessions, startup restore, external sessions | File menu plus optional Home entries | Home must remain escapable and preserve open documents. Missing media and external changes need recovery choices, not silent omission. |
| Five legacy visibility arrangements and new workspaces | View/Workspace selector; show/hide panel actions; restore preset | Retain equivalents of All, Video + subtitles, Audio + subtitles, Video only and Subtitles only. Timing/Translation/Typesetting presets supplement them. Movable/floating panels are accepted; preset memberships are under review. |
| Grid selection, columns, tags, metadata and row operations | Subtitle grid; header/context menus; Edit/Subtitles menu actions | Keep keyboard routes to insert/split/join/duplicate/swap, clipboard columns and paste special, selected/all sorting, preview and timing operations. Compactness must not make context-only actions undiscoverable. |
| Grid filters, hidden rows and grouping/tree operations | Grid filter strip plus expanded selection/filter controls | Show hidden-selection counts and action scope. Filtering visibility must not silently redefine the editing target. Tree/group behavior needs its own prototype. |
| Text, metadata, times, tag buttons, completion, font/color/style | Edit panel plus optional line properties; contextual pickers | Text and essential timing stay close. Decide which less-used metadata belongs in collapsible properties. Keep all 20 configurable tag-button actions discoverable. |
| Translation original/translated text, copying, tag movement and confirmation | Translation workspace and edit panel | Translation belongs to a document's fields; a second document is not required. Keep original text protection, untranslated/unconfirmed traversal and save translation explicit. |
| Two-document comparison and its criteria | Optional comparison mode with explicit document roles; criteria controls | The current prototype offers a protected reference. Legacy time/visible-line/selection/style comparisons must survive. This proposal does not yet decide synchronized scrolling or whether both documents may be editable. |
| Waveform/spectrum, timing, karaoke, gain/volume/playback/snap | Audio panel with context-sensitive timing controls | Distinguish audio selection, active subtitle and playback position. Preserve karaoke splitting, commit/advance and all short playback windows. Detailed interaction remains a separate prototype. |
| Video playback, frame/keyframe navigation, streams/chapters, capture | Video panel and transport; menus for less common commands | Separate exact-frame editing from general playback. Surface backend capability limits for filters/stream selection. File deletion needs an explicit file action, not an ambiguous close icon. |
| Visual typesetting tools, shapes and tag presets | Video tool strip plus contextual properties | Every drag operation also needs numeric/keyboard controls. Tools share selection and transforms, not independent subtitle state. Preset editors may be dialogs. |
| Styles, catalogs and font profiles | Styles/catalogs tool panel or detachable tool window; detailed style editor dialog | Repeated comparison/transfer benefits from staying beside the document. Catalog and document styles must remain visibly distinct; rename/copy/delete/import/transfer/order/clean all remain available. |
| Font selection, samples and colors | Searchable pickers from editor/styles; font-profile management with Styles | Keep preview, recent colors, alpha and screen picking. Provider-aware ASS font identity remains a separate architecture choice. |
| Find/replace, select lines, results and correction rules | Shared Search tool area with separate Find, Replace, Select and Corrections modes | Share scope/history/result navigation; never silently turn a selection action into a replacement. This is a candidate grouping, not a removal or approval to merge semantics. |
| Spelling suggestions and word-by-word checking | Inline editor suggestions plus a review tool area | Keep language/dictionary operations and ignore/replace choices. Whether spelling review shares Search placement needs reaction. |
| Shift times, postprocessor and named profiles | Timing tool panel with separate operation modes and preview/apply boundary | Preserve time/frame subsets, alignment, timed-tag handling, lead-in/out, continuity and keyframe thresholds. Do not apply preview changes until requested. |
| Script properties, resolution mismatch, resample, conversion | Subtitles menu; bounded dialogs showing scope and conversion effects | These alter a whole document or interpretation. Resolution and format changes need explicit consequences and undo rules. |
| Font collector and MKV tracks/attachments | Tools/Subtitles entry; dedicated task dialog/window with progress and results | These produce files and may run for a while. Preserve destination, track/font selection, cancellation and backend availability. |
| Automation manager, macros, shortcuts and generated dialogs | Automation menu and manager; script-defined modal dialogs; progress view | Preserve script API behavior. Dynamic dialogs cannot all be predesigned; the host needs a consistent schema-to-control contract and keyboard rules. |
| Settings, hotkeys, associations and external paths | One Settings window with searchable pages; scoped shortcut editor | Keep action/scope identity, conflicts/reset and automation mappings. System file associations stay platform-specific. Configuration import needs its own review flow. |
| Undo/redo, history and last saved revision | Edit actions plus History panel | Active document ownership must be obvious; inspecting a revision must not secretly replace it. The prototype does not yet settle typing versus document undo granularity. |
| Autosave browser, recovery and cache cleanup | File/Recovery view, optionally linked from Home; separate cleanup dialog | Open recovery as a separate copy. Distinguish recoverable subtitle snapshots from rebuildable media caches before removal. |
| Logs, progress/cancel, errors and update/about/help | Status area plus a task/log view; dialogs for blocking decisions | Preserve inspectable details and keyboard access. Progress must identify the owning task/document; updates must distinguish failed checks from no available update. |

## Focus and ownership proposal

- F6/Shift+F6 visits visible major regions. Tab stays in ordinary control order; editor arrows remain text input and grid arrows remain selection movement. Exact shortcut conflicts and native screen-reader behavior require QML validation.
- Content-changing commands name one editing document and one selection. A pinned reference can be inspected without acquiring edit ownership. An explicit role switch changes ownership while retaining both documents' context.
- Non-modal tools visibly show their document and scope. Decide whether they follow the active document or pin their target before building their flows; a tab change must not silently retarget a destructive preview.
- Home, workspace changes and restored layouts never discard unsaved content. Closing documents and replacing sessions require a separate save/discard/cancel path.
- Menus remain a complete, keyboard-reachable route to commands; toolbar/context shortcuts are additional routes. Exact menu grouping and command search are still open, and command search is not assumed to be a new parity requirement.

## Remaining reaction

Ownership, protected comparison, movable/floating panels, default following with optional pinning, and optional Home are settled. Review preset membership and comparison navigation in workspace-tools.html. The detailed groupings above remain inputs to focused per-surface prototype tickets, where no capability may disappear without an explicit decision.
