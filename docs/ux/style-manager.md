# Style manager

How the Style manager looks in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). Its fields, the catalogues and what each command does are unchanged; this document says how they look and where its messages go. While it is open, Discord shows Styling ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q735–Q739; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The window

A window titled "Style manager", radius 6. The left column is 340 wide on `bg` with a 1-unit `line` boundary; the editor fills the rest.

## The lists

Q735. From the top of the left column:

1. the filter field, "Filter styles";
2. **This file** in semibold, with New style, Duplicate, Delete and ⋯ as icon buttons named on hover, then its Styles;
3. **Copy to catalogue** and **Copy to this file** (quiet, small, with the `move-up` and `move-down` glyphs);
4. **Catalogue** with its picker and the same icon buttons, then its Styles.

Both lists use [the Styles panel's rows](typesetting-layout.md#the-styles-panel): a 64 × 34 "Aa" sample on the checkered ground in the Style's font and colours, the name in semibold, the font and size in `muted`. This file's rows end with how many Lines use the Style. The chosen row has the `select` fill.

With no Styles in this file, the list says in `muted`: "No styles in this file. Make one with + or copy one from the catalogue."

## The editor

- **Header.** The Style's name in semibold, "9 lines use it" in `muted`, and at the trailing end **Revert style** (quiet, `undo` glyph) (Q739).
- **Preview** (Q736). 116 high, radius 6: the Style drawn on the current video frame, dimmed to 80 %, with the Line and its time at the bottom right ("Line 14 · 0:01:02.75") in a dark chip. With no video, the checkered ground. The sample text is the **Text sample** field beside Name.
- **Fields.** Sections Font, Colours and transparency, Shape and position, in semibold, each a four-column grid of labelled fields: Name (three columns) and Text sample; Family (two columns), Size, Emphasis (four toggle buttons); each colour as a swatch, its hex field and its alpha field; Border, Shadow, Scale X and Y, Angle, Spacing, Border type, Alignment (a 3 × 3 grid, the chosen cell in `accent`), the three margins. Text encoding and Font catalogue close the editor in one row.
- With no Style chosen: the `styles` glyph, "No style chosen", **New style** and **From the catalogue…** (quiet).

## Messages

- **Errors** (Q737). A field that can't be saved has a 1-unit `danger` edge and, under it, the `notice-warning` glyph and the reason in `danger` ("A style named Default already exists."). OK and Apply are disabled until it's fixed. This replaces the message boxes A7 lists ([notices inventory](notices-inventory.md)).
- **Missing font** (Q738). A Family that isn't installed adds, under the field and on the Style's row, the `notice-warning` glyph and "Font not installed: shown in Liberation Sans" (the fallback that renders) in `warning`, as [the Styles panel](typesetting-layout.md#the-styles-panel) does.
- **Finding fonts.** While HikariSub reads the system's fonts, the editor's top shows "Finding fonts · reading 214 font files · 61 %" over a 2-unit `accent` progress line. Everything works meanwhile.

## The buttons

Q739. At the bottom right, **Cancel**, **Apply** and **OK** (primary), as today. Revert moves from the bottom left to the editor's header as Revert style.

## Discord

Presence shows **Styling** (`styling`) while the Style manager is open. Its art is the `styles` glyph on #FFD1A8 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6941093771)):

| Q | Decision |
| --- | --- |
| Q735 | This file above the catalogue; both lists use the Styles panel's rows |
| Q736 | The preview draws the Style on the current video frame, naming the Line; checkered with no video |
| Q737 | Errors show under the field, with OK and Apply disabled, instead of message boxes |
| Q738 | A font that isn't installed says so under Family and on the row, naming the fallback |
| Q739 | Cancel, Apply and OK stay at the bottom right; Revert style moves beside the Style's name |
