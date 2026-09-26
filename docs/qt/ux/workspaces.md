# Shared workspace and protected comparison

Accepted on 2026-09-27: [“shared workspace with optional protected comparison is fine”](https://github.com/altqx/hikari/issues/31#issuecomment-5849397748). This settles layout ownership and the comparison model. Detailed panel membership, Home/startup policy, docking/floating and tool-target behavior remain open in [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

The accepted model combines the shared-layout behavior demonstrated by A with an optional protected reference demonstrated by C in the [reviewed prototype](https://github.com/altqx/hikari/blob/163faf7c699c81a12338b04acea9433f356f0814/docs/prototypes/workspace-model.html). The visual baseline is [Compact Studio](visual-language.md). Per-document layouts and always-on comparison are not the chosen defaults.

## Ownership contract

The workspace belongs to the application. Switching the editing document changes displayed content, selection and media context while retaining the shared panel arrangement. A workspace change rearranges tools; it does not rewrite subtitles, switch their format or discard a draft.

Each document retains its own selection and associated media context. The editing target identifies the document receiving content-changing commands. Keyboard focus can visit other UI regions without implicitly changing that target.

Comparison is optional. Its protected reference can be read, selected and copied, but editing commands cannot mutate it. The UI must distinguish editing target and reference through explicit labels as well as visual treatment. An explicit operation can make the reference the editing target; existing content and per-document context survive that change. Reference focus alone is insufficient.

Translation mode remains a document capability with original/translated text and confirmation semantics. It is not replaced by opening a second document for comparison. Existing comparison criteria—time, visibility, selection and styles—remain capabilities to design; the ownership decision does not drop them.

## Captured interaction evidence

The HTML study demonstrates shared versus per-document layout retention, protected reference content while the editing target changes, panel restoration, recovery as a separate copy, a zero-document state, missing video while subtitle editing remains available, and save/discard/cancel close choices. Save and media relinking are simulated in memory. F6/Shift+F6 traverse the demonstrated major panels, including from text/grid focus.

These demonstrations clarify the ownership model. They do not establish real file/session compatibility, native docking, screen-reader behavior, decoding, persisted workspaces or a final save policy.

## Remaining information-architecture choices

1. Constrained resizable presets versus arbitrary docking/floating, including multiple monitors and recovery of off-screen panels.
2. Final panel membership and default visibility in Timing, Translation and Typesetting; equivalents of the five legacy visibility arrangements must remain available.
3. Whether Styles, Search, Timing and History become persistent tools, and whether each follows the editing target or can pin a document. Preview/apply must never silently change its target.
4. Home/startup behavior and the exact session/recovery entry points.
5. Comparison navigation/synchronization and the controls for changing editing/reference roles.

The [capability-placement worksheet](https://github.com/altqx/hikari/blob/12bb79bba302f0ccbb6558f2ab508b46ce111685/docs/prototypes/ia-capability-placement.md) is still a proposal for these remaining choices. It is not adopted wholesale by the narrower ownership answer. Subsequent prototype tickets must preserve the accepted shared/protected model and ask only about the unresolved behavior.
