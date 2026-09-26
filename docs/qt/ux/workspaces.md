# Shared workspace and protected comparison

Accepted on 2026-09-27: [“shared workspace with optional protected comparison is fine”](https://github.com/altqx/hikari/issues/31#issuecomment-5849397748). Subsequent live reviews chose movable/floating panels (B), follow-editing tools with optional pinning, optional Home, Classic menus/local controls/status, and the named tool/dialog grouping. The default Editing layout follows the current program: video left, audio above the editor on the right, and the subtitle grid across the bottom. Timing, Translation and Typesetting are accepted optional presets. This settles the top-level [information architecture](https://github.com/altqx/hikari/issues/31); detailed comparison navigation continues in [Prototype translation mode and protected comparison navigation](https://github.com/altqx/hikari/issues/49).

The accepted model combines the shared-layout behavior demonstrated by A with an optional protected reference demonstrated by C in the [reviewed prototype](https://github.com/altqx/hikari/blob/163faf7c699c81a12338b04acea9433f356f0814/docs/prototypes/workspace-model.html). The visual baseline is [Compact Studio](visual-language.md). Per-document layouts and always-on comparison are not the chosen defaults.

## Default editing arrangement

The user corrected the initial mockup in the live review: **“the default layout should look more like the current program”**, with a screenshot showing video on the left, audio/spectrum above the text editor on the right, and a full-width subtitle grid below both columns. This is the default composition. It supersedes the earlier A-derived full-width audio strip and the prototype opening in a task-specific Typesetting preset.

The default **Editing** preset shows Video, Audio, Editor and Grid together. Start with approximately 44% of the upper area for video and 56% for the audio/editor stack; the audio is the shallow upper section, while the editor takes the remaining right-hand height. The grid spans the workspace width below both. These are adjustable starting proportions, not fixed sizes that clip text or prevent scaling.

Styles, Search, Timing and History remain available movable/floating tools and start closed in this default, leaving the core editing arrangement unobstructed. Hiding/showing or rearranging a panel preserves document state. Keep the accepted Compact Studio appearance, shared ownership, follow/pin targets and optional Home/protected comparison.

Timing, Translation and Typesetting were subsequently accepted as optional presets; none replaces Editing as the default. The five legacy visibility combinations remain available. The earlier question about making task-preset defaults the primary experience is superseded by the default-layout correction.

| Optional task preset | Visible core panels | Initially open auxiliary tools |
| --- | --- | --- |
| Timing | Audio, Editor, Grid; Video can be opened | Timing, History |
| Translation | Video, Editor, Grid; Audio/comparison can be opened | Search |
| Typesetting | Video, Editor, Grid; Audio can be opened | Styles |

These are starting arrangements, retaining the accepted hide/show/rearrange/restore behavior and document state. Selecting Translation as a layout does not by itself convert document text or create a protected comparison reference.

Runnable references for this correction: [Editing workspace](https://github.com/altqx/hikari/blob/2dc12567c66f79d5d0a74d4b3356082c55566b30/docs/prototypes/workspace-tools.html), [run/review notes](https://github.com/altqx/hikari/blob/2dc12567c66f79d5d0a74d4b3356082c55566b30/docs/prototypes/workspace-tools-notes.md) and [capture](https://github.com/altqx/hikari/blob/2dc12567c66f79d5d0a74d4b3356082c55566b30/docs/prototypes/workspace-tools-preview.png). Browser checks confirmed the full-width grid, panel recovery retaining selection, and Video → Audio keyboard traversal; native docking remains a separate gate.

## Ownership contract

The workspace belongs to the application. Switching the editing document changes displayed content, selection and media context while retaining the shared panel arrangement. A workspace change rearranges tools; it does not rewrite subtitles, switch their format or discard a draft.

Each document retains its own selection and associated media context. The editing target identifies the document receiving content-changing commands. Keyboard focus can visit other UI regions without implicitly changing that target.

Comparison is optional. Its protected reference can be read, selected and copied, but editing commands cannot mutate it. The UI must distinguish editing target and reference through explicit labels as well as visual treatment. An explicit operation can make the reference the editing target; existing content and per-document context survive that change. Reference focus alone is insufficient.

Translation mode remains a document capability with original/translated text and confirmation semantics. It is not replaced by opening a second document for comparison. Existing comparison criteria—time, visibility, selection and styles—remain capabilities to design; the ownership decision does not drop them.

## Movable and floating panels

Accepted in the live tool-placement review: **B / movable and floating panels**. Tools can be rearranged, hidden, restored and detached from the main workspace. Preserve keyboard-accessible placement actions alongside pointer interactions. A shared workspace keeps the chosen arrangement when the editing document changes.

The [reviewed HTML follow-up](https://github.com/altqx/hikari/blob/9781b3cb51fb87d9640366a9619b9c5589ff1e26/docs/prototypes/workspace-tools.html) illustrates Left/Right/Bottom and Float positions. Its floating cards stay inside the browser page; this acceptance chooses the UX capability, not that simulation as a native implementation. Native docking dependency, accessible cross-window focus, multi-monitor geometry recovery and versioned layout persistence are covered by [Choose native docking and workspace persistence architecture](https://github.com/altqx/hikari/issues/46) and Qt verification.

## Persistent tools and Home

Accepted in the live review: Styles, Search, Timing and History **follow the editing document by default**, with an **optional Pin current document** control. A pinned tool retains its named document across editing-tab changes. When that document is a protected comparison reference, content-changing Apply remains unavailable. A tool must expose its document and scope; a preview must not silently retarget when the document or selection changes.

The sample permits a deliberately pinned tool to apply to an inactive, unprotected document after showing that target in Preview. Treat a tool's explicit target separately from the application's editing target; general editing shortcuts still use the editing target. Review detailed per-tool scope, confirmation and stale-preview rules in their surface tickets.

**Home is optional**, with the workspace as the normal editing destination. Home provides recent-file and recovery entry points without discarding the open documents or shared arrangement. It is not a mandatory start screen. Actual startup/session restoration, file recovery and unsaved-close flows retain their data and lifecycle contracts.

## Shell commands, properties and task grouping

Accepted in the live follow-up on 2026-09-27: **Classic menus and local controls**, plus **the proposed tool/dialog grouping**. The [reviewed Classic shell](https://github.com/altqx/hikari/blob/ae07d2de29157c5a131cfed28fa207f70e778bac/docs/prototypes/shell-chrome.html) retains traditional top-level menus, controls attached to their owning panels, line properties beside the text editor and detailed media/status information across the bottom. A contextual right inspector and compact shared command strip are not the default. Individual commands remain discoverable through the shared action system and keyboard routing; the sample menu subset does not remove unshown commands.

Styles, Search, Timing and History are persistent tools that can stay open while editing, following the accepted movable/floating and follow/pin rules. Timing here means shifting/postprocessing; it does not replace the Audio panel. History is document edit history. Search/find/replace/select-lines/results keep explicit scopes; Styles retains document styles and external catalogs as distinct collections. Their detailed workflows still need individual prototypes.

Script properties, Resample, Font collector, MKV extraction, Preferences and script-created options use task dialogs. This grouping does not require long operations to block the UI: collection/extraction keep progress, cancellation and inspectable results. Script dialogs preserve their schema/API contract. Automation manager placement and other surface families remain separate detailed decisions; this acceptance does not adopt every row of the older placement worksheet.

The [Classic capture](https://github.com/altqx/hikari/blob/ae07d2de29157c5a131cfed28fa207f70e778bac/docs/prototypes/shell-classic-preview.png) records the reviewed structure. Browser observations cover sample menu/dialog entry, Escape dismissal, context/tool switching, F6 from editor to Grid and the full-width Grid in both alternatives. Native menus, focus, accessibility, task lifetimes and full per-surface behavior remain future verification.

## Captured interaction evidence

The HTML study demonstrates shared versus per-document layout retention, protected reference content while the editing target changes, panel restoration, recovery as a separate copy, a zero-document state, missing video while subtitle editing remains available, and save/discard/cancel close choices. Save and media relinking are simulated in memory. F6/Shift+F6 traverse the demonstrated major panels, including from text/grid focus.

These demonstrations clarify the ownership model. They do not establish real file/session compatibility, native docking, screen-reader behavior, decoding, persisted workspaces or a final save policy.

## Detailed surface work

The [translation/comparison contract](translation-comparison.md) now accepts stacked fields, a bottom reference tray and independent navigation by default; draft, export and detailed matching behavior continue in its prototype ticket. Detailed placement and interaction for the remaining surface families, including automation management and task results, need their own prototypes. These refine the accepted top-level structure rather than reopening workspace ownership, default arrangement or Classic shell placement.

The [capability-placement worksheet](https://github.com/altqx/hikari/blob/12bb79bba302f0ccbb6558f2ab508b46ce111685/docs/prototypes/ia-capability-placement.md) is still a proposal for these remaining choices. It is not adopted wholesale by the narrower ownership answer. Subsequent prototype tickets must preserve the accepted shared/protected model and ask only about the unresolved behavior.
