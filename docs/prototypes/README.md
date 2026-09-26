# HikariSub throwaway design prototypes

These assets answer Wayfinder questions. They are not production code or approved UX.

## Visual language

Ticket: [Prototype the HikariSub visual language and component set in QML or HTML](https://github.com/altqx/hikari/issues/32).

Open `visual-language.html` directly in a browser, or run:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/prototypes
```

Visit `http://127.0.0.1:8765/visual-language.html?variant=A`.

- **A / Studio:** balanced panels, compact controls and restrained boundaries.
- **B / Focus:** a reading list next to a focused editor, softer controls and more breathing room.
- **C / Workbench:** a dominant data grid, tight spacing and technical typography.

Use the bottom switcher or left/right arrows outside editable controls. URL parameters preserve variant (`A`, `B`, `C`), theme (`dark`, `light`, `contrast`) and view (`workspace`, `gallery`). The gallery covers semantic tokens, buttons, inputs, disabled/pressed/focus/error/mixed states, a dialog and navigation expectations.

Select a line, edit source text, insert italic tags, confirm, undo, filter, hide tags, try the dialog, and switch themes. F6 cycles regions. Nothing is saved. The illustration, waveform and playhead are synthetic: this does not decode media, validate Qt accessibility or establish performance. Panel layouts illustrate density; the information-architecture ticket decides workspace behavior separately.

Review: Which direction is the best starting point? What controls, density or colours should be borrowed from another? What makes long sessions harder?

Research: [UX patterns](https://github.com/altqx/hikari/blob/4be444f9fc097a9220c59014c973d79083e12bcf/docs/research/ux-patterns.md), [Qt accessibility](https://github.com/altqx/hikari/blob/fb2413d7e98732fe38859268e33f8c0c2eb6eb2e/docs/research/qml-accessibility.md).

Figma Starter is optional for occasional handoff. Human reaction is required before the ticket closes.
