# HikariSub throwaway design prototypes

These assets answer Wayfinder questions. They are not production code or approved UX.

Start at `index.html` (or `http://127.0.0.1:8765/` when serving this directory) for the four-prototype review desk. It links the HTML studies, immutable native-QML run guides and their review tickets. The native preview images are copied unchanged from the commits credited there.

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
- **D / Compact Studio (default):** C’s compact visual treatment and A’s panel placement, following [altqx’s review](https://github.com/altqx/hikari/issues/32#issuecomment-5848973463). Video and editor sit above full-width audio and subtitle grid. This hybrid still needs review; the original A/B/C comparisons remain available.

Use the bottom switcher or left/right arrows outside editable controls. URL parameters preserve variant (`A`, `B`, `C`, `D`), theme (`dark`, `light`, `contrast`) and view (`workspace`, `gallery`). The gallery covers semantic tokens, buttons, inputs, disabled/pressed/focus/error/mixed states, a dialog, keyboard-operated document tabs and navigation expectations.

Select a line, edit source text, insert italic tags, confirm, undo, filter, hide tags, try the dialog, and switch themes. F6 cycles regions. If the selected row is filtered out, its selection is retained and the first visible row remains a Tab entry; Enter there chooses that row for editing. Nothing is saved. The illustration, waveform and playhead are synthetic: this does not decode media, validate Qt accessibility or establish performance. Panel layouts illustrate density; the information-architecture ticket decides workspace behavior separately.

Review: Does D capture the requested mix of C compactness and A layout? What controls, density or colours still need changing? What makes long sessions harder?

Research: [UX patterns](https://github.com/altqx/hikari/blob/4be444f9fc097a9220c59014c973d79083e12bcf/docs/research/ux-patterns.md), [Qt accessibility](https://github.com/altqx/hikari/blob/fb2413d7e98732fe38859268e33f8c0c2eb6eb2e/docs/research/qml-accessibility.md).

Figma Starter is optional for occasional handoff. Human reaction is required before the ticket closes.

## Workspace behavior

Ticket: [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

Open `workspace-model.html` directly, or use the same server and visit `http://127.0.0.1:8765/workspace-model.html?variant=A`.

- **A / Shared panels:** one application workspace follows the active document's content, selection and media.
- **B / Document-owned layouts:** each document retains its own panel arrangement.
- **C / Editing + pinned reference:** a protected comparison view with explicit editing ownership and independent reference selection.

The pure state reducer drives all controls. Guided experiments cover switching documents, protecting reference text, restoring a hidden panel and opening a sample autosave as a separate copy. The Empty / missing / unsaved walkthrough starts without documents, opens samples, simulates unavailable video, edits without it, prompts to save/discard/cancel on close and relinks the sample. Save is explicitly simulated in memory. Presets, saved arrangements, media positions, dirty state and selections stay in memory. Switching model or starting a walkthrough resets the sample. Home is an optional navigation view; it keeps the open documents.

Browser checks exercised shared versus document-owned preset retention, read-only reference text during editing, panel restoration, recovery-copy creation, the empty/missing-media flow, all three close choices and closing the last document. F6/Shift+F6 traverse major panels even from text or grid focus. The [close-decision capture](workspace-close-preview.png) records the concrete dialog. Appearance is provisional, not an approved design. This does not prove native docking, screen-reader support, decoding or file compatibility.

Review: Should layouts belong to the application or the document? Should comparison be an optional protected reference view? Which panels should be visible for Timing, Translation and Typesetting, and should Home be optional?

The [capability placement worksheet](ia-capability-placement.md) maps the complete legacy UI families to proposed panels, dialogs and menu entries. It covers the placement questions beyond the runnable sample and explicitly marks unresolved decisions; it is not an approved information architecture.
