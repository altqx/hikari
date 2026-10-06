# Icon set

HikariSub's UI icons are one in-house vector set ([K1](https://github.com/altqx/hikari/issues/204)), replacing legacy's PNG bitmaps (`HikariSub/Bitmaps`) and the rewrite's placeholder glyphs and text labels ([visual language](visual-language.md), "Icons, content and adaptive layout"). The user decided on 2026-10-05 that the set is drawn in house, covers UI icons only, follows the light or dark appearance live and has user-customizable colours per appearance.

Out of scope: the application icon and the file-type icons (kept as they are), and controls Qt draws itself (check boxes, radio buttons, menu marks, list arrows, sliders, grippers). The manifest names every legacy bitmap the set does not replace and why. Legacy also drew its list arrows `arrow_list.png` and `arrowListDouble.png` as action images (the Style manager's move and transfer buttons, stylestore.cpp:56-88; the Line editor's Manage tag buttons MenuButton, MenuButton.cpp:21), so the set replaces them there (move-up, move-down, move-to-top, move-to-bottom, menu-more) while Qt keeps drawing the list arrows themselves.

## Files

| Path | What |
| --- | --- |
| `src/ui/icons/<role>.svg` | One icon per role, the shipped source. |
| `src/ui/icons/manifest.json` | Per role: file, label (the legacy tooltip or menu text, the default accessible name), whether it has an accent layer, whether it mirrors, the legacy bitmaps it replaces, the surfaces using it, and `pending` (the later card that places it) for a role whose surface does not exist yet; and `notReplaced`, the legacy bitmaps left out with the reason (retired commands among them: GLOBAL_VIDEO_INDEXING's FFMS2 Indexing.png). |
| `tools/icons/draw_icons.py` | How the icons are drawn: shared motifs (page, floppy, film frame, note, badges) and the rules below, enforced when it writes the SVGs and the manifest. Change an icon here and run it. |
| `tools/icons/contact_sheet.py` | The HTML contact sheet for review (`out/k1/index.html`), in the theme layer's four themes (it reads their colours from `src/ui/theme.cpp`). |
| `src/ui/Icon.qml`, `IconButton.qml`, `IconToolButton.qml` | The Icon item and the icon-only buttons. |
| `src/ui/IconTextButton.qml` | A push button with an icon of the set beside its text (legacy buttons with a bitmap beside the label). |
| `src/ui/ShellMenuItem.qml`, `ShellMenu.qml` (their `iconRole`), `IconTabButton.qml` | Menu items, submenus and tab buttons with an icon (every shell menu is a ShellMenu, D1; a submenu's item shows the submenu's role): the style draws the control, the icon is its image source from `IconTheme`'s image provider (`image://hikari-icon/<role>/<colour>/<accent>/<mirrored>`, drawn at the device's pixels). |
| `src/ui/IconDialogHeader.qml` | A dialog's title with its icon (legacy dialogs' SetIcon); windows take theirs with `IconTheme.setWindowIcon`. |
| `src/ui/icon_theme.*` | The tint, the appearance, the `IconTheme` singleton and the image provider. |

Roles are lowercase, hyphenated and name the action or symbol, not the picture (`frame-previous`, `tag-bold`, `media-play`). A surface built by a later card places the roles drawn for it here.

## Where icons go

The UI polish of 2026-10-05 (the user's rule that HikariSub look like professional software) placed the set by these rules; [visual-language.md](visual-language.md#tool-strips-option-rows-and-labels) has the layout rules around them.

- **A glyph never stands in for an icon.** No button's text is "+", "-", "...", an arrow or a glyph prefix ("↑ Add to storage"): it is an icon of the set (`hikari_ui_icon_tests` noGlyphStandsInForAnIcon reads every QML file; AutomationDialog, whose controls a script defines, is the exception).
- **Tool strips are icon-only.** The visual tool rail, the tool strip under the video, the Line editor's tag row, the Style manager's list columns, the font catalog bar, the reference tray's bar and an automation script's row use `IconToolButton`: the icon only, the name as the tooltip and the accessible name, the longer help text as `tip`.
- **A dialog verb keeps its text.** OK, Cancel, Apply, Find, Replace and the other labelled verbs of a dialog or a panel stay text; a list's transfer or rule button that needs its words takes an icon beside them (`IconTextButton`: the Style manager's Add to storage / Add to ASS / Add to all open ASS files, the Multireplacer's Add / Edit / Delete rule).
- **One command, one icon.** A command shows the same icon on every surface: the menus, the context menus (video, Grid, text fields), the tab menu and the transport (Save and Save all in the tab menu, Play / Pause and Stop in the video menu, Font collector in the Grid menu, the Spellchecker switch in a text field's menu). The converse holds where it can: a frame saved to a file takes frame-snapshot and a frame copied to the clipboard edit-copy (the clipboard's Copy), Duplicate lines takes duplicate; the variants with and without subtitles share their verb's icon and differ in their words.
- **One icon, one meaning.** An arrow means a move within a list; the Style manager's transfers between its two lists take copy-to-storage, copy-to-ass and copy-to-all-ass (the two lists as boxes, the direction between them in the accent), so a transfer never wears a move's arrow (legacy drew both with arrow_list.png).
- **Menus line up.** Every menu item keeps the check gutter and the icon gutter, with or without an icon (`ShellMenuItem`), and shows its key binding right-aligned in the muted colour.
- **Generic roles are shared.** add, remove, edit, duplicate, delete, import, refresh, clear and filter serve every list (Style manager, font catalogs, Multireplacer, automation scripts, the Timing panel's profiles); folder-open every folder chooser (Find in subtitles, Font collector, Import legacy settings).

The roles the polish added: pick-lines and clear (the batch picker); move-to-top, move-up, move-down, move-to-bottom and menu-more; add, remove, edit, duplicate, delete, import, refresh, filter; edit-copy, edit-cut, edit-paste; folder-open, find-in-files, show-in-folder; compare, reference, extract-subtitles, multireplace (Fix minor errors had borrowed select-lines); close-video, frame-snapshot, zoom-reset, volume; match-previous and match-next (the reference tray had borrowed the audio box's line steps); unload and run-script; settings-video, settings-audio, appearance and hotkeys (the Options pages); copy-to-storage, copy-to-ass and copy-to-all-ass (the Style manager's transfers, after the polish review); fullscreen and fullscreen-exit, drawn for V5 ([#184](https://github.com/altqx/hikari/issues/184)) and pending until it builds full screen. D3 ([#207](https://github.com/altqx/hikari/issues/207)) added panel-menu, three dots in a row, for the panel headers' "⋯" button (MuseScore's menu button), so that menu-more's double chevron keeps its one meaning, a list that drops open. clip-invert was redrawn in the outline style (a frame, the shape and the kept outside hatched in the accent) in place of the first draft's solid square. Their contact sheets for review: `out/polish/new-icons-light.png` and `new-icons-dark.png` (the icon test's sheet with `HIKARI_ICON_SHEET_ROLES`).

## Geometry

- **Grid.** A 16 × 16 `viewBox` (`0 0 16 16`). Every coordinate, length and radius is a multiple of 0.25 unit.
- **Optical size.** Drawn for 16 px at 100% scale, the size of menu items, panel buttons and tool rails. At 32 px (large buttons) and at 150% and 200% scale the vectors are drawn again at the device's pixels, never scaled up from a bitmap.
- **Live area.** A main shape's ink stays within 1 to 15 (stroke centres from 1.5 to 14.5). Badges, corner handles, an action's arrow and the pixel-letter format labels may reach the box's edge; nothing crosses it.
- **Stroke.** One weight for the whole set: 1 unit (1 px at 100%). Straight strokes run on half-unit coordinates so they cover whole pixels at 100% and 200%.
- **Caps and joins.** Round caps and round joins, set once on the root element.
- **Corner radius.** 1 unit on frames and panels (documents, windows, film frames, buttons drawn as rectangles); 0.5 on small filled marks (pause bars, colour swatches); 0 where a corner is the meaning (the folded page corner, selection handles).
- **Fills.** Outline icons by default; solid shapes where mass carries the symbol (play, pause, stop, the next/previous triangles, the bold B). A solid shape is also stroked so its corners keep the round join.
- **Optical balance.** A circle that stands alone has radius 6.5 (it touches the live area); a square stands at 13 × 11 or smaller, so circles and squares read the same size.
- **Badges.** Modifiers (add, recent, close, the format letters) sit in the lower right quadrant, from 9 to 15, after the base shape is cut back to leave them room.

## Colour

Each SVG paints only with `currentColor` or `none`: no colours of its own, opacity, style sheets, text, gradients or raster images. The icon is tinted at run time.

**Accent layer.** An icon may have one accent layer, the group `<g id="accent">`, drawn in the accent colour: the modifier (a badge's plus, cross or clock), the direction of a move, the part a tool acts on, a colour swatch. The accent never carries meaning alone: the icon reads in one colour, and the disabled and hover/pressed states paint the whole icon, accent included, in one colour.

**Theme colours.** The icons take their colours from the Hikari theme layer (K2, [visual-language.md](visual-language.md#hikari-theme-layer-k2)), live: the icon colour is the theme's text colour, the accent colour its accent, the hover/pressed colour the accent too, the disabled colour its disabled colour. There are no icon colour settings: K1's twelve `icons.<appearance>.<slot>` settings and their Themes page rows were withdrawn by K2 (the user's 2026-10-05 decision, no per-colour editing), and a profile that saved them drops them on load. In high contrast the "Text and icons" and accent pickers recolour the icons with the text.

**States.** Normal: the icon colour with the accent colour on the accent layer. Hover or pressed: the whole icon in the hover/pressed colour (legacy BitmapButton brightened a hovered bitmap and swapped in a pressed one; the set uses one colour for both). Disabled: the whole icon in the disabled colour (an item is disabled with its parent). A highlighted menu item paints the whole icon in the palette's highlighted text colour, as the highlight is the accent.

**Contrast.** In every theme, with every accent preset, the icon, accent and disabled colours meet WCAG 2.x success criterion 1.4.11's 3:1 for graphical objects against the theme's four surfaces (bg, panel, raised, field), and a disabled icon is at least 2:1 from an enabled one (`hikari_ui_icon_tests` themeIconColoursMeetContrast). The lowest are the disabled colours: 3.31 (Light, Dark), 4.28 (High contrast white), 5.43 (High contrast black). A high-contrast pick is the user's own and is not checked.

**Appearance.** The theme layer's: Light, Dark, High contrast white or High contrast black, Light or Dark following the platform's colour scheme while "Follow system theme" is on. A theme, accent or pick change repaints every icon without a restart.

## Direction and accessibility

Icons that show navigation or reading order mirror in right-to-left layouts (`mirror` in the manifest: undo, redo, list and text-line icons, the session and search arrows). Media transport, time, frame and data symbols do not.

An icon-only control keeps its text as its accessible name and shows a tooltip (`IconButton`, `IconToolButton` and the audio box's buttons: the text is not drawn; `tip` defaults to it). The Icon item itself is ignored by assistive technology.

## Evidence

`hikari_ui_icon_tests`: the manifest test (every role the QML names resolves, every other role is marked `pending` and a pending role is not named; each SVG valid for Qt SVG without warnings, single-colour with at most one accent layer, on the quarter-unit grid inside the box, without raster; every legacy bitmap replaced or listed as not replaced), the tint, the defaults' contrast, live palette colours, the image-sourced controls (menu items, submenus, tab buttons, dialog titles, window icons), and the rendering fixtures: the whole set drawn by the Icon item in the three themes' palettes at 100% (and at 150% and 200% in the `.scale150` / `.scale200` runs), each icon equal to the set rendered at the device's pixels. With `HIKARI_ICON_SHEET_DIR` set they write the contact sheets `k1-<appearance>-<percent>.png`; `HIKARI_ICON_SHEET_ROLES` (comma-separated) limits a sheet to those roles and `HIKARI_ICON_SHEET_PREFIX` renames it, for reviewing new icons. noGlyphStandsInForAnIcon: no QML button, menu item or check (every one in every file) has a text that is, or starts with, a glyph standing in for an icon, as a string, a qsTr() string or the first part of a concatenation; the checker is first run against snippets of each form to show it catches them.

## Provenance and licence

Drawn for HikariSub in this repository (by script, no third-party icon pack or traced artwork) and distributed under the project's licence (GPL-3.0).
