# HikariSub throwaway design prototypes

These throwaway assets answer Wayfinder questions. Accepted directions are recorded below and in the [Qt specification](https://github.com/altqx/hikari/tree/qt/docs/qt); other interactions remain proposals. None is production code.

Start at `index.html` (or `http://127.0.0.1:8765/` when serving this directory) for the prototype review desk. It links the HTML studies, immutable native-QML run guides and their review tickets. The native preview images are copied unchanged from the commits credited there.

## Visual language

Ticket: [Prototype the HikariSub visual language and component set in QML or HTML](https://github.com/altqx/hikari/issues/32).

Open `visual-language.html` directly in a browser, or run:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/prototypes
```

Visit `http://127.0.0.1:8765/visual-language.html?variant=D`.

- **A / Studio:** balanced panels, compact controls and restrained boundaries.
- **B / Focus:** a reading list next to a focused editor, softer controls and more breathing room.
- **C / Workbench:** a dominant data grid, tight spacing and technical typography.
- **D / Compact Studio (default):** C’s compact visual treatment, initially combined with A’s panel placement following [altqx’s review](https://github.com/altqx/hikari/issues/32#issuecomment-5848973463). The latest live correction changes the default to video left, audio above the editor on the right, and the full-width subtitle grid below, matching the current program. Appearance and components retain the accepted Compact Studio baseline. The hybrid and reviewed gallery were [approved](https://github.com/altqx/hikari/issues/32#issuecomment-5849386836); the original A/B/C comparisons remain available as reference.

Use the bottom switcher or left/right arrows outside editable controls. URL parameters preserve variant (`A`, `B`, `C`, `D`), theme (`dark`, `light`, `contrast`) and view (`workspace`, `gallery`). The gallery covers semantic tokens, buttons, inputs, disabled/pressed/focus/error/mixed states, a dialog, keyboard-operated document tabs and navigation expectations.

Select a line, edit source text, insert italic tags, confirm, undo, filter, hide tags, try the dialog, and switch themes. F6 cycles regions. If the selected row is filtered out, its selection is retained and the first visible row remains a Tab entry; Enter there chooses that row for editing. Nothing is saved. The illustration, waveform and playhead are synthetic: this does not decode media, validate Qt accessibility or establish performance. Panel layouts illustrate density; the information-architecture ticket decides workspace behavior separately.

Outcome: Compact Studio is the accepted visual baseline. Its native implementation must still verify readability, scaling, keyboard semantics and accessibility; no repeat design-direction vote is needed.

Research: [UX patterns](https://github.com/altqx/hikari/blob/4be444f9fc097a9220c59014c973d79083e12bcf/docs/research/ux-patterns.md), [Qt accessibility](https://github.com/altqx/hikari/blob/fb2413d7e98732fe38859268e33f8c0c2eb6eb2e/docs/research/qml-accessibility.md).

Figma remains on Starter for occasional handoff. Future unresolved UX questions use their own runnable prototype and human review.

## Workspace behavior

Ticket: [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

Open `workspace-model.html` directly, or use the same server and visit `http://127.0.0.1:8765/workspace-model.html?variant=A`.

- **A / Shared panels:** one application workspace follows the active document's content, selection and media.
- **B / Document-owned layouts:** each document retains its own panel arrangement.
- **C / Editing + pinned reference:** a protected comparison view with explicit editing ownership and independent reference selection.

The pure state reducer drives all controls. Guided experiments cover switching documents, protecting reference text, restoring a hidden panel and opening a sample autosave as a separate copy. The Empty / missing / unsaved walkthrough starts without documents, opens samples, simulates unavailable video, edits without it, prompts to save/discard/cancel on close and relinks the sample. Save is explicitly simulated in memory. Presets, saved arrangements, media positions, dirty state and selections stay in memory. Switching model or starting a walkthrough resets the sample. Home is an optional navigation view; it keeps the open documents.

Browser checks exercised shared versus document-owned preset retention, read-only reference text during editing, panel restoration, recovery-copy creation, the empty/missing-media flow, all three close choices and closing the last document. F6/Shift+F6 traverse major panels even from text or grid focus. The [close-decision capture](workspace-close-preview.png) records the concrete dialog. The ownership study is a behavioral reference; Compact Studio supplies the accepted visual baseline. This does not prove native docking, screen-reader support, decoding or file compatibility.

Accepted: [shared workspace with optional protected comparison](https://github.com/altqx/hikari/issues/31#issuecomment-5849397748). Per-document ownership is not the selected default. Movable/floating tools, follow-editing with optional pinning, and optional Home are also accepted. Remaining review concerns preset membership and comparison navigation.

The [capability placement worksheet](ia-capability-placement.md) maps the complete legacy UI families to proposed panels, dialogs and menu entries. It covers the placement questions beyond the runnable sample and explicitly marks unresolved decisions; it is not an approved information architecture.

## Native grid direction

[Use painted grid](https://github.com/altqx/hikari/issues/27#issuecomment-5849499565) is accepted. The linked native spike remains evidence, including its 28 posted-key observations. Native accessibility and performance against agreed budgets remain follow-up work; the Python drawing path is not a production API decision.

## Tool placement follow-up

Open [workspace-tools.html](workspace-tools.html?variant=B). B movable/floating panels is accepted; A remains a comparison reference. The new default Editing preset follows the current-program arrangement with all four core panels visible and auxiliary tools closed. Timing, Translation and Typesetting are optional proposals, alongside the five legacy visibility equivalents. [Review notes](workspace-tools-notes.md) explain the follow/pin targets, protection, Preview/Apply and limits. Shared ownership and optional protected comparison are retained in both variants. Follow-editing with optional pinning and optional Home are accepted. The default-layout correction supersedes the earlier task-preset question; optional preset contents and detailed comparison navigation remain under review.
