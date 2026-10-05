# Icon set

HikariSub's UI icons are one in-house vector set ([K1](https://github.com/altqx/hikari/issues/204)), replacing legacy's PNG bitmaps (`HikariSub/Bitmaps`) and the rewrite's placeholder glyphs and text labels ([visual language](visual-language.md), "Icons, content and adaptive layout"). The user decided on 2026-10-05 that the set is drawn in house, covers UI icons only, follows the light or dark appearance live and has user-customizable colours per appearance.

Out of scope: the application icon and the file-type icons (kept as they are), and controls Qt draws itself (check boxes, radio buttons, menu marks, list arrows, sliders, grippers). The manifest names every legacy bitmap the set does not replace and why.

## Files

| Path | What |
| --- | --- |
| `src/ui/icons/<role>.svg` | One icon per role, the shipped source. |
| `src/ui/icons/manifest.json` | Per role: file, label (the legacy tooltip or menu text, the default accessible name), whether it has an accent layer, whether it mirrors, the legacy bitmaps it replaces, the surfaces using it, and `pending` (the later card that places it) for a role whose surface does not exist yet; and `notReplaced`, the legacy bitmaps left out with the reason (retired commands among them: GLOBAL_VIDEO_INDEXING's FFMS2 Indexing.png). |
| `tools/icons/draw_icons.py` | How the icons are drawn: shared motifs (page, floppy, film frame, note, badges) and the rules below, enforced when it writes the SVGs and the manifest. Change an icon here and run it. |
| `tools/icons/contact_sheet.py` | The HTML contact sheet for review (`out/k1/index.html`). |
| `src/ui/Icon.qml`, `IconButton.qml`, `IconToolButton.qml` | The Icon item and the icon-only buttons. |
| `src/ui/ShellMenuItem.qml`, `ShellMenu.qml` (their `iconRole`), `IconTabButton.qml` | Menu items, submenus and tab buttons with an icon (every shell menu is a ShellMenu, D1; a submenu's item shows the submenu's role): the style draws the control, the icon is its image source from `IconTheme`'s image provider (`image://hikari-icon/<role>/<colour>/<accent>/<mirrored>`, drawn at the device's pixels). |
| `src/ui/IconDialogHeader.qml` | A dialog's title with its icon (legacy dialogs' SetIcon); windows take theirs with `IconTheme.setWindowIcon`. |
| `src/ui/icon_theme.*` | The tint, the appearance, the `IconTheme` singleton and the image provider. |

Roles are lowercase, hyphenated and name the action or symbol, not the picture (`frame-previous`, `tag-bold`, `media-play`). A surface built by a later card places the roles drawn for it here.

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

**Theme colours.** The icons take their colours from the active theme's palette, live: the icon colour is its text colour (`WindowText`), the accent colour its accent (`Accent`), the hover/pressed colour the accent too, the disabled colour its disabled text colour. The user decided on 2026-10-05 to walk back per-colour editing for a MuseScore-style model (light, dark and high-contrast themes with one user-chosen accent), designed separately; the settings below stay until that model replaces them, and a colour saved in them still wins over the palette's.

**States.** Normal: the icon colour with the accent colour on the accent layer. Hover or pressed: the whole icon in the hover/pressed colour (legacy BitmapButton brightened a hovered bitmap and swapped in a pressed one; the set uses one colour for both). Disabled: the whole icon in the disabled colour (an item is disabled with its parent). A highlighted menu item paints the whole icon in the palette's highlighted text colour, as the highlight is the accent.

**Settings (to be replaced).** Twelve profile settings of the rewrite's own, "#RRGGBB" text, edited on the Options dialog's Themes page below the spectrum colours (a double click picks a colour, "Reset icon colours" stages the defaults, OK or Apply saves and every icon repaints at once; a colour saved at its default leaves the profile). Like the other theme colours, "Set default" leaves them. Their defaults, the visual-language tokens a theme's palette uses:

| Setting | Light | Dark | High contrast |
| --- | --- | --- | --- |
| `icons.<appearance>.normal` | `#202832` (text) | `#E8EDF2` (text) | `#FFFFFF` |
| `icons.<appearance>.accent` | `#145C4C` (accent) | `#9CDBC9` (accent) | `#FFFF00` (accent) |
| `icons.<appearance>.active` (hover/pressed) | `#145C4C` | `#9CDBC9` | `#00FFFF` (focus) |
| `icons.<appearance>.disabled` | `#74808B` | `#75818D` | `#8C8C8C` |
| Lowest contrast against bg, panel, raised and field | 3.31 (disabled) | 3.31 (disabled) | 5.43 (disabled) |

Every default, the disabled colour included, meets WCAG 2.x success criterion 1.4.11's 3:1 for graphical objects against the four surfaces of its appearance (`hikari_ui_icon_tests` defaultColoursMeetContrast). A user's own colours are not checked.

**Appearance.** High contrast when the platform asks for it (Qt's contrast preference); otherwise dark when the application palette's window colour is dark, else light. A palette, colour-scheme or contrast change repaints every icon without a restart. Until the Hikari theme layer exists the palette is Qt's; the icons follow whatever palette the controls use.

## Direction and accessibility

Icons that show navigation or reading order mirror in right-to-left layouts (`mirror` in the manifest: undo, redo, list and text-line icons, the session and search arrows). Media transport, time, frame and data symbols do not.

An icon-only control keeps its text as its accessible name and shows a tooltip (`IconButton`, `IconToolButton` and the audio box's buttons: the text is not drawn; `tip` defaults to it). The Icon item itself is ignored by assistive technology.

## Evidence

`hikari_ui_icon_tests`: the manifest test (every role the QML names resolves, every other role is marked `pending` and a pending role is not named; each SVG valid for Qt SVG without warnings, single-colour with at most one accent layer, on the quarter-unit grid inside the box, without raster; every legacy bitmap replaced or listed as not replaced), the tint, the defaults' contrast, live palette colours, the image-sourced controls (menu items, submenus, tab buttons, dialog titles, window icons), and the rendering fixtures: the whole set drawn by the Icon item in the three themes' palettes at 100% (and at 150% and 200% in the `.scale150` / `.scale200` runs), each icon equal to the set rendered at the device's pixels. With `HIKARI_ICON_SHEET_DIR` set they write the contact sheets `k1-<appearance>-<percent>.png`.

## Provenance and licence

Drawn for HikariSub in this repository (by script, no third-party icon pack or traced artwork) and distributed under the project's licence (GPL-3.0).
