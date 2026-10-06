# Compact Studio visual language

## Status and evidence

**Accepted design baseline: D / Compact Studio.** The initial approval combined C's compact technical presentation with A's panel arrangement; the later live correction changes the default arrangement to match the current program, as described below. The user first wrote, **“I like the compact design of C and the lay our of A”** ([26 September 2026](https://github.com/altqx/hikari/issues/32#issuecomment-5848973463)), then replied **“LGTM”** to the completed study ([26 September 2026](https://github.com/altqx/hikari/issues/32#issuecomment-5849386836)). That approval covers the reviewed appearance, component gallery and document-tab example; the later instruction revises the default composition. It does not need another design-direction approval.

The initial immutable appearance reference is [`visual-language.html` at `163faf7c699c81a12338b04acea9433f356f0814`](https://github.com/altqx/hikari/blob/163faf7c699c81a12338b04acea9433f356f0814/docs/prototypes/visual-language.html), using `variant=D`. Its [README](https://github.com/altqx/hikari/blob/163faf7c699c81a12338b04acea9433f356f0814/docs/prototypes/README.md) describes a throwaway study; any wording there awaiting review predates the linked LGTM. This specification records the approved reference and its limits rather than promoting the HTML implementation into production code.

HikariSub owns the Qt application layer under [ADR 0001](../../adr/0001-hikari-owned-qt-layer.md). The vocabulary of **workspace**, **editing target** and **protected reference** comes from [CONTEXT.md](../../../CONTEXT.md). Panel membership, workspace ownership and document behavior belong to the [workspace specification](workspaces.md); this document defines their visual and interaction presentation.

## Composition

Compact Studio has quiet, nearly square boundaries, close panel spacing, a compact command area and monospaced tabular data. The default editing scene follows the user's current-program screenshot: video at the left, audio/spectrum above the line editor at the right, and the subtitle grid spanning the full width below both. The latest layout instruction supersedes the initial A-derived full-width audio strip while retaining Compact Studio appearance and movable/floating panels.

Use approximately 44% / 56% for the default upper split, with a shallow audio region above the right-hand editor and a substantial full-width grid below. The [workspace contract](workspaces.md) defines this Editing preset and the initially closed auxiliary tools. Treat prototype CSS measurements as review proportions, not application minimum sizes; allow resizing and system text/scaling without clipping controls.

The study header, A–D comparison switcher, direction badge, study guide, review question and debug/status narration are review scaffolding. The component gallery is a specification aid, not a new product screen. Synthetic scenery, waveform and playback demonstrate composition only.

The [revised D study](https://github.com/altqx/hikari/blob/2dc12567c66f79d5d0a74d4b3356082c55566b30/docs/prototypes/visual-language.html) and [capture](https://github.com/altqx/hikari/blob/2dc12567c66f79d5d0a74d4b3356082c55566b30/docs/prototypes/compact-studio-preview.png) implement the current-program arrangement. The earlier immutable study remains the provenance for the approved appearance and component gallery.

**Panel header: the dock title bar (2026-10-05).** The D study draws a header inside each panel. With docking, each panel also has a dock title bar, so the two repeated the same title. The user reviewed the Classic shell on 2026-10-05 and dropped the in-panel header. The dock title bar is now each panel's only header. A panel keeps its accessible name (the Grid's description names the editing target). The document name appears only where it adds information: the Document tab and the window title. The Grid shows the active tab's Document, so it does not repeat the name. The Protected reference has no Document tab, so its tray's dock is titled "Reference: <name>". The status bar does not name the editing target: its first field is legacy's help and Automation status text. The dock chrome takes its colours from the theme layer's roles, as the panels do, so every appearance stays consistent: the title bar is `raised` with `text` and its buttons' `line` boundary, the panel tabs' buttons hover in `select` with `text` glyphs, the separators, group frames and floating windows use `bg` and `line`, and the accent appears only where K2 uses it. The focused panel's ring on its title bar is in the `focus` role (see Keyboard focus). The panel-header metrics and heading treatment below now apply to the dock title bar. [D3](https://github.com/altqx/hikari/issues/207) reshapes the dock chrome after MuseScore 4's KDDockWidgets convention (a "⋯" menu in place of the Float and Close buttons, borderless floating panels) and may revise them.

## Semantic appearance tokens

Use semantic roles shared by all components, with a complete palette switch. Hex values below are the exact CSS reference values expanded to six digits. The Dark palette is the study's initial appearance.

| Role | Reference variable | Dark | Light | High contrast |
| --- | --- | --- | --- | --- |
| Application background | `bg` | `#171B20` | `#E5E9EC` | `#000000` |
| Panel and popup surface | `panel` | `#20262D` | `#F9FAFB` | `#080808` |
| Raised control / table-header surface | `raised` | `#29313A` | `#EDF0F3` | `#151515` |
| Input / recessed surface | `field` | `#171D24` | `#FFFFFF` | `#000000` |
| Primary text | `text` | `#E8EDF2` | `#202832` | `#FFFFFF` |
| Secondary text | `muted` | `#A5B1BD` | `#526170` | `#EEEEEE` |
| Boundary | `line` | `#414B57` | `#AAB5BE` | `#FFFFFF` |
| Action / active accent | `accent` | `#9CDBC9` | `#145C4C` | `#FFFF00` |
| Text on action fill | `onaccent` | `#102C24` | `#FFFFFF` | `#000000` |
| Selected background | `select` | `#304C47` | `#D6EBE4` | `#253F60` |
| Keyboard focus | `focus` | `#F9D784` | `#8D4200` | `#00FFFF` |
| Error / warning emphasis | `danger` | `#FFADAD` | `#A51F31` | `#FFADAD` |

The reference's `focus` values are superseded: the theme layer draws keyboard focus in primary text (see "Keyboard focus" under the Hikari theme layer).

## Hikari theme layer (K2)

The user decided on 2026-10-05 to follow MuseScore 4's appearance model ([research at v4.7.5](../../research/musescore-appearance.md)) with no per-colour editing. [K2](https://github.com/altqx/hikari/issues/206) implements it as the Hikari theme layer, `src/ui/theme.h`: one set of the roles above, resolved from the chosen appearance and exposed once. QML reads the `Theme` singleton (`Theme.panel`, `Theme.accent`, ...); the Qt Quick Controls draw with the application palette the layer sets, in the `HikariStyle` controls style (`src/ui/style`: Fusion on every platform, since the native Windows style draws with the system's colours, with every control outline in `line`, keyboard focus drawn as the focus ring below (`FocusFrame`, in `focus` from `StyleColours`, which the layer sets with the palette), and a focused field's border from the accent; Fusion's own outline, its window colour darkened 140%, measures about 1.1:1 against the Dark and High contrast black panels, so fields and buttons lost their boundaries there; `hikari_ui_theme_tests` controlsOutlineInTheBoundaryColour, keyboardFocusRingInTheTextColour); controls the shell draws itself take `FocusRing`, the same ring (the Appearance page's cards and swatches); the icons take `IconTheme`'s colours, which are the layer's; owner-drawn items (the Grid, the audio display, the spelling marks) read the layer's roles and content colours and repaint when it changes.

**Themes.** Light, Dark, High contrast white and High contrast black (`appearance.theme`: `light`, `dark`, `highContrastWhite`, `highContrastBlack`; Dark by default, legacy's default theme). High contrast black is the spec's high-contrast column; High contrast white is drawn to match it on white.

**Follow system theme** (`appearance.followSystem`, on by default). While on, Light or Dark comes from the platform's colour scheme (`QStyleHints::colorScheme`, live: the Windows setting, the XDG desktop portal on Linux); it never turns high contrast on or off, it only moves a high-contrast theme to its matching side (white for a light scheme, black for a dark one). An unknown scheme keeps the chosen theme. Choosing a theme by hand turns following off.

**Accent.** Seven presets per mode (`appearance.lightAccent`, `appearance.darkAccent`: Light and Dark remember their own), in hue order, the spec's green the default. Each preset sets three roles: `accent`, `onaccent` and `select` (its hue at the green's selected lightness); `focus` does not change with the accent. No custom accent outside high contrast. The accent drives selection, primary buttons, toggles, the tab underline, the focused field border, text selection, progress, the icons' accent layer and the selection-type marks of owner-drawn content.

| Key | Light accent / text on it | Dark accent / text on it |
| --- | --- | --- |
| `red` | `#B3261E` / `#FFFFFF` | `#F4A9A3` / `#2C1210` |
| `orange` | `#9C4600` / `#FFFFFF` | `#F5BB8A` / `#2C1D10` |
| `gold` | `#7A5E00` / `#FFFFFF` | `#E3CF7E` / `#2C2610` |
| `green` (default) | `#145C4C` / `#FFFFFF` | `#9CDBC9` / `#102C24` |
| `blue` | `#1D5BA8` / `#FFFFFF` | `#A6C8F2` / `#101D2C` |
| `purple` | `#5B3FB0` / `#FFFFFF` | `#C6B5F4` / `#18102C` |
| `pink` | `#9E2A73` / `#FFFFFF` | `#EFAAD3` / `#2C1021` |

Measured WCAG 2.x contrast (`hikari_ui_theme_tests` accentPresetsMeetContrast; the swatch sheet `k2-accents.png` and `k2-contrast.txt` are written with `HIKARI_THEME_SHEET_DIR`): the lowest of each accent against its mode's bg, panel, raised and field, text on the accent, and primary text on the preset's selected background.

| Key | Light: on surfaces / text on it / text on selected | Dark: on surfaces / text on it / text on selected |
| --- | --- | --- |
| `red` | 5.35 / 6.54 / 10.79 | 6.93 / 9.20 / 9.87 |
| `orange` | 5.23 / 6.38 / 11.38 | 7.75 / 9.58 / 8.84 |
| `gold` | 5.02 / 6.12 / 11.92 | 8.46 / 9.69 / 8.01 |
| `green` | 6.45 / 7.88 / 11.95 | 8.40 / 9.51 / 7.92 |
| `blue` | 5.52 / 6.74 / 11.07 | 7.63 / 9.86 / 9.39 |
| `purple` | 6.18 / 7.55 / 10.51 | 7.10 / 9.85 / 10.52 |
| `pink` | 5.67 / 6.92 / 10.82 | 7.12 / 9.44 / 9.87 |

**High contrast.** Accent, text-and-icons and border colours are freely pickable (`appearance.highContrastWhite.*` and `appearance.highContrastBlack.*`, each theme its own, "#RRGGBB"; MuseScore's low-vision exception, its non-working "Disabled text" picker left out); "Reset to default" puts the theme's back. Every other role is fixed. Text on a picked accent turns black or white to stay readable. These six are the only colour settings (`settings_tests` NoColourSettingOutsideHighContrast).

**Role tables.** Every theme with its default accent, and each role's lowest contrast against the theme's four surfaces (`hikari_ui_theme_tests` rolesMeetContrast; text roles at least 4.5:1, 7:1 in high contrast; accent, icons and disabled at least 3:1; `focus` is `text`, focusIsTheTextColour). `warning` is the rewrite's warning emphasis (WINDOW_WARNING_ELEMENTS' role), `success` the Font collector's success text (legacy's fixed `#008000`, FontCollector.cpp:872, 978), `disabled` disabled text and icons.

| Role | Light | Dark | High contrast white | High contrast black |
| --- | --- | --- | --- | --- |
| `bg` / `panel` / `raised` / `field` | `#E5E9EC` / `#F9FAFB` / `#EDF0F3` / `#FFFFFF` | `#171B20` / `#20262D` / `#29313A` / `#171D24` | `#FFFFFF` / `#FFFFFF` / `#EBEBEB` / `#FFFFFF` | `#000000` / `#080808` / `#151515` / `#000000` |
| `text` | `#202832` 12.19 | `#E8EDF2` 11.18 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `muted` | `#526170` 5.21 | `#A5B1BD` 6.03 | `#1A1A1A` 14.60 | `#EEEEEE` 15.74 |
| `line` | `#AAB5BE` 1.71 | `#414B57` 1.49 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `accent` | `#145C4C` 6.45 | `#9CDBC9` 8.40 | `#0037B3` 8.02 | `#FFFF00` 17.01 |
| `onaccent` (on `accent`) | `#FFFFFF` 7.88 | `#102C24` 9.51 | `#FFFFFF` 9.56 | `#000000` 19.56 |
| `select` (`text` on it) | `#D6EBE4` 11.95 | `#304C47` 7.92 | `#C9DAF8` 14.87 | `#253F60` 10.72 |
| `focus` (`text`) | `#202832` 12.19 | `#E8EDF2` 11.18 | `#000000` 17.62 | `#FFFFFF` 18.26 |
| `danger` | `#A51F31` 6.06 | `#FFADAD` 7.42 | `#A00000` 7.06 | `#FFADAD` 10.29 |
| `warning` | `#8A5A00` 4.85 | `#E0A030` 5.79 | `#6B4500` 7.11 | `#FFD54A` 12.93 |
| `success` | `#008000` 4.21 | `#008000` 2.56 | `#005A00` 7.15 | `#7CFC7C` 13.98 |
| `disabled` | `#74808B` 3.31 | `#75818D` 3.31 | `#6E6E6E` 4.28 | `#8C8C8C` 5.43 |

`success` in Light and Dark is legacy's fixed green, below 4.5:1 (2.56:1 on Dark); a readable dark-theme green is a proposed departure, not applied. `line` in Light and Dark is the spec's boundary token (1.71:1 and 1.49:1 at its lowest), below WCAG 1.4.11's 3:1 for a control's boundary; fields keep their own fill, and a stronger control boundary for those two themes is a proposed change, not applied. In high contrast `line` is the border pick, the control outlines included.

**Keyboard focus.** A rewrite design decision (user, 2026-10-05, with the wave-5 batch-2 review), not a legacy departure: keyboard focus follows MuseScore 4's convention ([research](../../research/musescore-appearance.md): its `focusColor` is vestigial; the real ring, `NavigationFocusBorder`, is `fontPrimaryColor`, `navCtrlBorderWidth` 2 px), a ring 2 wide in the theme's text colour. The `focus` role is therefore `text` in every theme and with every accent, and follows a high-contrast text pick; the spec's focus tokens and the presets' own focus colours are withdrawn. The accent stays for selection, the default button and primary actions, the focused field's border, the tab underline and the current Line's outline, so keyboard focus (a text-coloured ring) and selection (an accent fill, marker or outline) are told apart by colour and by shape; on a selected background the ring reads as text does (at least 4.5:1, 7:1 in high contrast).

| Where | The ring |
| --- | --- |
| HikariStyle controls (`src/ui/style`, `FocusFrame.qml`): Button, ToolButton (and the icon buttons on them), ComboBox, CheckBox, RadioButton, TextField, TextArea, SpinBox, Slider | 3 beyond the control (a check box's or radio button's whole control, a slider's handle), while it has keyboard focus: `visualFocus`, or for a field focus by Tab, Backtab or a shortcut. A click's focus draws none. The control's own outline stays `line`; Fusion's accent tint for keyboard focus (combo box, spin box buttons, slider handle) is gone. A focused field keeps the accent's border, with the ring beside it for keyboard focus. |
| TabButton (the dialogs' tab bars, the panel tabs) | Just inside the tab, where the tab bar would clip it. |
| Shell-drawn controls (`FocusRing.qml`: the Appearance page's theme cards and accent swatches) | 3 beyond the control, while it has keyboard focus. |
| The Grid's focused cell (`line_grid.cpp`) | While the Grid has focus, the current Line's row: 2 wide just inside its 1-wide accent outline; a selected row's 3-wide accent marker stays over the ring's left side. |
| Dock headers (`DockTitleBar.qml`) | The panel holding the focus (the engine's `isFocused`; a floating window while active): 2 wide just inside its title bar, moving with F6 and Tab. The panel's own boundary stays `line` (D1 drew a focused panel's border 2 wide in the accent). |

Validation: `hikari_ui_theme_tests` focusIsTheTextColour (every theme, every preset, the high-contrast text picks; focus never the accent or the selected background), keyboardFocusRingInTheTextColour (each control above in all four themes: the ring shown for keyboard focus only, 2 wide, where it is, drawn in the text colour, the control's outline in `line`, the default button and a focused field in the accent; a high-contrast text pick; without a profile the palette's text colour); `hikari_ui_line_grid_tests` focusRingOnTheCurrentRow; `hikari_ui_shell_tests` keyboardFocusRingsThePanelHeaderAndTheGridRow, appearancePagePreviewsSavesAndRestores (the cards' and swatches' ring). Not covered: the controls the style leaves to Fusion (lists' delegates, menu items, scroll bars) keep Fusion's focus drawing; a ring 3 beyond a control at a scrolled view's edge can be clipped.

**Content colours** are fixed per theme, not settings: the audio display (waveform, spectrum, cursor, boundaries, keyframe and second marks, the timescale), the Grid's comparison colours (GRID_COMPARISON_*), the spelling marks (GRID_SPELLCHECKER / EDITOR_SPELLCHECKER) and the Grid's alternate-row and warning cells. Dark keeps legacy's dark theme (config.cpp:405-472, `LoadDefaultColors(true)`; the spectrum's `#000000`, `#674FD7`, `#F4F4F4` among them); Light keeps legacy's light comparison and spelling colours and gets matched audio colours (legacy's light theme kept the dark audio display); both high-contrast themes get their own. Selection-type marks take the accent: the audio selection (AUDIO_SELECTION_BACKGROUND and _MODIFIED at legacy's 0x37 alpha, AUDIO_WAVEFORM_SELECTED) and GRID_SELECTION's blend over a compared row (at legacy's alpha 75). Document colours (ASS styles, rendered subtitles, the video) never change with the appearance.

**Appearance page.** The Options dialog's Appearance page (in legacy's Themes page's place; theme files stay excluded) shows the four themes as sample cards, Follow system theme, the shown mode's seven swatches (each with its contrast in the tooltip) and, in high contrast, the three pickers with their reset. Every choice previews live; OK or Apply saves, Cancel takes the preview back; "Set default" leaves the appearance.

The panel header in D uses `field`, although the generic study header uses `raised`. Ordinary controls use `raised`; editable multiline text uses `field`. Secondary text remains meaningful content, not a licence to reduce essential text to unreadable contrast. The grid's subtle row divider is `line` at 48% alpha; primary boundaries are solid `line`.

Interface appearance must not rewrite a document's ASS styles or recolor rendered subtitle output. The preview's white sample subtitle and illustrated scene are media content, not UI tokens. Likewise, an application appearance does not select a document, change a workspace's ownership, or import legacy Kainote themes.

These are accepted color references; the theme layer above measures their contrast, which is not assistive-technology certification. Native disabled states, translucent separators, selection/hover combinations and operating-system contrast integration still require verification. A contrast-mode implementation must preserve meaningful boundaries even when shadows or transparency are suppressed.

## Density, typography and geometry

All measurements in this section are **reference logical units from CSS**, not physical pixels or instructions to assign literal `font.pixelSize` values. Qt implementation should retain the compact relationships while respecting operating-system scaling, the user's text size, font metrics and fallback. Derive control implicit sizes from text and padding; do not clip larger text to a fixed study height. The reference 13-unit body is a comparison anchor, not a mandatory cross-platform font size.

| Element | Compact Studio reference |
| --- | --- |
| Workspace gutter and outer padding | 3 each |
| Application bar | 36 high; horizontal padding 12; item gap 12 |
| Command bar | Minimum 33 high; vertical/horizontal padding 2 / 7; gap 3 |
| Panel header (the dock title bar) | Minimum 32 high; padding 5 / 8; gap 8 |
| Ordinary button / single-line field | Minimum 30 high; button padding 5 / 10; field padding 4 / 8 |
| Command-bar button | Minimum 27 high; padding 3 / 8 |
| Panel-header action (dock title-bar button) | Minimum 21 high; padding 1 / 6 |
| Transport action | Minimum 24 high; padding 3 / 8; group gap 6 |
| Editor content | Padding 9 / 12; major gaps 8; action-footer gap 6 |
| Text area | Padding 10; line-height reference 1.6; editor minimum 60 |
| Grid cell / column header | Cell padding 4 / 7; header padding 5 / 7 |
| Grid filter | Width reference 180; minimum 22 high; padding 1 / 7 |
| Status strip | Padding 7 / 12; gap 14 |
| Ordinary boundary | 1 |
| Keyboard focus ring | 2 wide, offset 3 beyond the control (just inside a tab, the Grid's row and a panel header); the text colour |
| Selected row marker | 3-wide inset leading-edge accent |

The gallery's 22-unit example row illustrates compact density; the working grid uses content plus padding rather than a universal fixed 22-unit row. Small toolbar sizes are references for dense desktop controls, not permission to remove accessible names, keyboard operation or usable pointer targets.

Typography follows these roles:

| Role | Reference size / treatment |
| --- | --- |
| Body, ordinary controls and subtitle text editor | 13; platform sans serif |
| Field label, panel title and grid data | 11; labels secondary; title weight 650 |
| Table header, status, badge and compact auxiliary action | 10; table-header weight 500 |
| Application name | 14; weight 650 |
| Dialog title | 18 |
| Timecodes and Compact Studio table cells | Monospace; timecodes use tabular numerals |

The HTML uses Segoe UI / Arial / sans-serif and Consolas / monospace. These are reference families, not mandatory bundled fonts. Use suitable system sans-serif and fixed-width roles, preserving shaping and font fallback for Thai, Arabic, CJK and mixed-script content. The raw subtitle editor remains readable body text; D's monospaced table does not require every editing surface to be monospaced. Scale secondary roles relative to the user's base font, and allow them to grow for legibility.

Panel headings are uppercase in D; the rule applies to the dock title, the only panel heading since 2026-10-05. The heading itself has `0.02em` letter spacing; its enclosing header declares `0.08em` for other inherited text. Preserve the restrained visual hierarchy without applying uppercase transformations to user-authored subtitle text, filenames or scripts without case. The sample preview's 19-unit subtitle size is not an application typography rule or an ASS rendering requirement.

The effective D control and panel radius is **1** in all appearances. The contrast palette declares radius 0, but the later C/D rule overrides it to 1; this specification records the actual approved D cascade. Application frame and dialog also use 1. Exceptions in the reference are the menu popup at 5, a compact state badge at 3 and a status capsule at 20. The rounded H sample mark is provisional artwork, not a settled brand geometry.

Panels are flat, separated by boundaries. The menu popup has a `0 10 30` shadow in black at approximately 33% alpha. The dialog has no explicit shadow in the study; its dimming backdrop is `#050B14A6` (approximately 65% alpha), with padding 24 and a width reference of at most 460. The comparison switcher's stronger shadow and radius belong only to the review harness. No production motion duration or easing system was established by this study.

## Components and state presentation

| Component / state | Accepted reference and implementation responsibility |
| --- | --- |
| Primary action | `accent` fill, `onaccent` text, accent boundary, emphasized weight. Preserve legibility through interaction states. |
| Secondary action | `raised` surface and `line` boundary. Enabled hover uses accent boundary and selected background. |
| Quiet action | Transparent surface and boundary at rest; retains a visible keyboard focus treatment. |
| Pressed toggle | Accent boundary and 2-unit inset bottom accent; label/state communicate on/off as well as color. |
| Disabled control | Study opacity 0.46 and no enabled hover. Native semantics must make it unavailable; verify final contrast and discoverability rather than treating opacity as a complete disabled specification. |
| Editable field | Clear label, boundary and value; multiline text uses the recessed surface. A field's local editing keys remain local. |
| Invalid value | Danger boundary and explanatory text; associate the explanation with the field and disable an action whose prerequisite is invalid. Do not communicate failure by color alone. |
| Mixed value | Muted italic “Multiple values” in the gallery. Preserve mixed/indeterminate semantics; do not replace an actual value with this string in the model. |
| Checkbox / slider | Accent indicates state; label, value and keyboard operation remain available. |
| Selected row | Selected background plus leading accent marker; selection remains distinguishable from keyboard focus. |
| Row warning / confirmation | Warning emphasis and textual context; confirmation appears as text/check/state badge. These are presentation examples, not a decision to persist new document fields. |
| Document tab | Explicit editing target, document name and dirty indication. Dirty dot is accompanied by an accessible textual state. Selection and focus are separate concepts. |

State composition needs an explicit native implementation. In the HTML, the generic hover rule can replace a selected row's fill while its leading marker survives, and generic button hover also applies to primary buttons. These CSS precedence effects do not establish a new selected-hover or primary-hover palette. Preserve visible selection, action hierarchy and text contrast when states overlap. The tab example implements selection semantics but does not add a bespoke `aria-selected` CSS treatment; native tabs must visibly identify the editing target using the shared theme rather than copying an indistinguishable pair of buttons.

The sample validates centisecond-formatted timestamps and end-after-start, highlights CPS above 18, and bounds a sample shift dialog to 0–60 seconds. Those are demonstration rules, not approved production timing, reading-speed or validation policy. Display actual domain validation and state from the application model. Similarly, per-keystroke snapshot undo, illustrative layer storage and regex-based tag hiding are not production editing algorithms.

## Keyboard, focus and tabs

The accepted navigation direction provides a route into every workspace region and keeps focus visible. HikariSub owns its native realization; HTML event handlers are not the implementation contract.

* **F6 / Shift+F6:** cycle forward/backward through the visible workspace regions. In the revised default D arrangement the order is preview, audio, editor, grid. Start from the currently focused region and maintain a meaningful order when layout or visibility changes. The revised study follows actual focus; native Qt must implement the same explicit navigation responsibility.
* **Tab / Shift+Tab:** navigate controls within their normal scope. A focused region must provide a useful next entry into its controls. Keep the focus ring visible at panel edges and in scrolled content.
* **Grid:** provide a clear keyboard entry and visible focus, retained selection when filtering hides a row, and an explicit empty state. The [painted-grid contract](subtitle-grid.md) governs native displayed-order navigation and the distinct selected set, current line and keyboard focus. The HTML study has a single-selection example in which Enter selects its focused row and enters the editor; this is illustrative, not an accepted production activation rule. Define activation and its effects on multi-selection in the native follow-up rather than copying the twelve-row HTML array.
* **Text inputs:** cursor movement, selection, IME composition and field-specific keys take precedence over surrounding navigation. The study's global Left/Right A–D comparison shortcut is harness-only and must not become an application shortcut.
* **Document tabs:** click selects the editing target. Left/Right navigate and activate neighboring tabs; Home/End select the first/last. The study wraps its two tabs. Use one active tab entry in the tab sequence, retain visible focus, and name the associated content. Tab activation changes document context, not interface appearance. Closing, reordering, overflow, protected-reference placement and dirty-document prompts need their document/workspace contracts; the gallery does not demonstrate them.
* **Menus:** opening the example menu enters its first item; Escape dismisses it and restores focus to its opener. Use complete native menu navigation rather than copying the sample's incomplete menu key handling.
* **Dialogs:** a modal operation contains focus, has an explicit title and cancellation path, and returns focus sensibly on dismissal. Cancel must not apply its pending edit. The HTML relies on browser dialog behavior; verify focus containment, Escape and restoration in Qt.

Focus is a ring in the text colour (Hikari theme layer, "Keyboard focus"), distinct from selection (the accent) and error (`danger`). Do not reduce focus to a subtle text-color change. Confirm that status updates can be announced without stealing focus or flooding assistive technology during typing.

## Icons, content and adaptive layout

The H mark, Unicode transport symbols, arrows, checkmarks and other glyphs are placeholders. They neither select a third-party icon pack nor establish redistribution rights for brand assets. Implement a coherent vector-icon set with consistent optical size, stroke weight and alignment. Record provenance and licence for any adopted assets; an icon-only action needs an accessible name and appropriate tooltip. Use directional mirroring where it conveys navigation, while retaining the meaning of media controls and data symbols. Font glyph availability must not decide whether an essential action is understandable. The set is specified in [icons.md](icons.md) (K1: drawn in house, UI icons only, following the appearance live in the theme layer's colours, K2).

The study stacks preview, editor, audio and grid below a 900-unit width and simplifies some content below 590. Those browser breakpoints and fixed stacked heights are exploratory references, not supported minimum-window sizes. In particular, hiding the layer field or an action at a narrow width is not permission to make that capability unreachable. Use resizing, overflow, scrolling and appropriate panel arrangements while preserving access to fields and commands. Check long translations, larger system text and mixed-direction text rather than assuming English widths.

## Qt Quick responsibilities and remaining verification

| Owner | Responsibility |
| --- | --- |
| Hikari theme and metrics layer | Expose the semantic palette, typography roles, spacing and radii once; resolve appearance and system/user scaling; keep subtitle-rendering styles separate. The palette half is K2's theme layer (`src/ui/theme.h`, above); typography, spacing and radii are not in it yet. |
| Shared Qt Quick Controls styling | Apply tokens to ordinary buttons, fields, selectors, toggles, sliders, menus, dialogs and tabs, including overlapping states and accessible labels/values. Avoid divergent per-screen colors. |
| Workspace and action layer | Own region order, focus restoration, shortcut scope, editing-target indication and modal command routing. Styling must not infer the editing target from the focused panel. |
| Grid model and presentation | Keep stable row identity, selection and editing state independent of delegates; render compact columns, current/selected/focused states and filtered-selection feedback. |
| Media surfaces | Present actual video, subtitle overlays, waveform and time state through their adapters. UI palette changes must not alter document rendering. |

These responsibilities implement the [architecture contract](../architecture.md); they do not select a docking dependency or a third-party component library. Figma remains on Starter for occasional handoff. Agent-built QML/HTML prototype tickets remain the mechanism for exploring unresolved interactions; acceptance of D is already recorded.

The HTML approval establishes visual direction and the demonstrated interaction vocabulary. It does **not** establish that the native application has passed any of the following:

* Native rendering and text readability in all four themes, including combined states and operating-system contrast behavior (the role contrast is measured above).
* System font changes, fallback/shaping, IME input, mixed-direction content, text enlargement, high-DPI scaling and movement between monitors with different scales.
* Complete keyboard reachability, visible focus, tab/menu/dialog semantics, screen-reader names/roles/values and announcements on the supported platforms.
* Large-document grid behavior, latency, memory, scrolling or frame-time targets; real audio/video synchronization and rendering performance.
* Production persistence, undo, docking, window restoration or the full document/tab lifecycle.

Those are implementation and verification obligations, not reasons to reopen the approved visual direction. Native findings should produce specific fixes or focused prototype evidence while preserving the accepted Compact Studio baseline.
