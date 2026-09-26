# Shared workspace and protected comparison

Accepted on 2026-09-27: [“shared workspace with optional protected comparison is fine”](https://github.com/altqx/hikari/issues/31#issuecomment-5849397748). This settles layout ownership and the comparison model. The subsequent live review chose movable/floating panels (B). Tools follow the editing document by default with optional pinning, and Home is optional. The default Editing layout follows the current program: video left, audio above the editor on the right, and the subtitle grid across the bottom. Optional task-preset membership and comparison navigation remain open in [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

The accepted model combines the shared-layout behavior demonstrated by A with an optional protected reference demonstrated by C in the [reviewed prototype](https://github.com/altqx/hikari/blob/163faf7c699c81a12338b04acea9433f356f0814/docs/prototypes/workspace-model.html). The visual baseline is [Compact Studio](visual-language.md). Per-document layouts and always-on comparison are not the chosen defaults.

## Default editing arrangement

The user corrected the initial mockup in the live review: **“the default layout should look more like the current program”**, with a screenshot showing video on the left, audio/spectrum above the text editor on the right, and a full-width subtitle grid below both columns. This is the default composition. It supersedes the earlier A-derived full-width audio strip and the prototype opening in a task-specific Typesetting preset.

The default **Editing** preset shows Video, Audio, Editor and Grid together. Start with approximately 44% of the upper area for video and 56% for the audio/editor stack; the audio is the shallow upper section, while the editor takes the remaining right-hand height. The grid spans the workspace width below both. These are adjustable starting proportions, not fixed sizes that clip text or prevent scaling.

Styles, Search, Timing and History remain available movable/floating tools and start closed in this default, leaving the core editing arrangement unobstructed. Hiding/showing or rearranging a panel preserves document state. Keep the accepted Compact Studio appearance, shared ownership, follow/pin targets and optional Home/protected comparison.

Timing, Translation and Typesetting may remain optional presets for later review; none replaces Editing as the default. The five legacy visibility combinations remain available. Their exact optional tool defaults are still proposals, and the earlier question about making task-preset defaults the primary experience is superseded by this correction.

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

## Captured interaction evidence

The HTML study demonstrates shared versus per-document layout retention, protected reference content while the editing target changes, panel restoration, recovery as a separate copy, a zero-document state, missing video while subtitle editing remains available, and save/discard/cancel close choices. Save and media relinking are simulated in memory. F6/Shift+F6 traverse the demonstrated major panels, including from text/grid focus.

These demonstrations clarify the ownership model. They do not establish real file/session compatibility, native docking, screen-reader behavior, decoding, persisted workspaces or a final save policy.

## Remaining information-architecture choices

1. Optional Timing, Translation and Typesetting preset membership; the current-program Editing arrangement is the settled default, and the five legacy visibility combinations remain available.
2. Comparison navigation/synchronization and the controls for changing editing/reference roles; detailed criteria and interaction can be resolved in its surface prototype.
3. Final placement of menus, command toolbars, properties/inspector and the status/task area, plus which additional dialog families become panels or share a tool. The capability-placement worksheet is still a proposal; choosing movable tools did not approve every grouping.

The [capability-placement worksheet](https://github.com/altqx/hikari/blob/12bb79bba302f0ccbb6558f2ab508b46ce111685/docs/prototypes/ia-capability-placement.md) is still a proposal for these remaining choices. It is not adopted wholesale by the narrower ownership answer. Subsequent prototype tickets must preserve the accepted shared/protected model and ask only about the unresolved behavior.
