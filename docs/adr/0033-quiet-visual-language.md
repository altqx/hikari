---
status: accepted
amends: 0032
---

# Quiet replaces Compact Studio

On 2026-11-17 altqx chose the visual direction for the design refresh of every window and panel ([#1205](https://github.com/altqx/hikari/issues/1205), Q581–Q583 on tracking [#1209](https://github.com/altqx/hikari/issues/1209)). The choice was between three Gate B boards drawn over a Gate A inventory of main 344efa574 in all four themes:

- **A · Quiet:** VS Code and Zed chrome.
- **B · Layered:** Zed's tonal tiles.
- **C · Studio:** DaVinci Resolve.

The owner answered "yes to all and i like the A". The baseline is A, Quiet, with C's 20-unit transport timecode and C's full-screen strip. The specification is [visual-language.md](../ux/visual-language.md).

## Why change the look

Compact Studio (approved on [#32](https://github.com/altqx/hikari/issues/32), 26 September 2026) grew one rule at a time, and the inventory found 25 places where the result looks dated or inconsistent:

- three stacked header rows at the top of the main window;
- an empty strip above the Grid's headings;
- a log-like time readout under the video;
- four control idioms in one toolbar;
- Fusion's check boxes and spin boxes beside a flat icon set;
- buttons from 22 to 30 high on one page;
- an icon on every Settings row;
- boxed panels in Light, and a lattice of boxes in high contrast.

Quiet answers these with fewer, flatter parts, and stays at least as dense.

## Decisions

- **Look (Q581).**
  - One flat surface. Panels are split by 1-unit `line` with no gutter and no radius.
  - Controls have radius 4. Ordinary controls are 24 high and panel headers 30.
  - Panel titles are in sentence case, and the selected panel tab has a 2-unit accent underline.
  - The Document tabs move into a 36-unit title bar, so one row of chrome goes.
  - Light and Dark get new surface, text, `line`, `control` and `disabled` values, and every contrast floor of K2 holds.
  - New `hover` and `pressed` overlay roles mean hover is never the accent.
  - HikariStyle draws the check box, radio button, spin box and combo box itself.
  - High contrast black and white keep K2's fixed values, and the live Omarchy theme following stays.
- **From C (Q581).**
  - The transport timecode is 20-unit monospace, and the second time readout goes.
  - Full screen draws its controls as one strip in the bottom letterbox, or over the picture's bottom on a dark gradient when there is no letterbox. It is always on black.
- **Density.** Compact Studio's density rule carries over: as dense or denser everywhere.
- **Restyle only.** The menus (CX7, [#810](https://github.com/altqx/hikari/issues/810)), the Agent panel ([#1189](https://github.com/altqx/hikari/issues/1189)) and the icon set take the new tokens and are not redesigned.
- **The resolution question.** It becomes a window titled "Open video". It states the case plainly ("same shape, N % bigger" or "wider"), shows a card with a preview frame for each choice, and recommends and preselects Resample, using A's board wording. Two behaviour changes come with it:
  - **Q582.** When the file has no layout resolution, a check box ("Also set the layout resolution to the video’s (W × H)", ticked by default) replaces the separate `missingLayoutRes` question that followed. One question shows instead of two.
  - **Q583.** Sizes whose ratios differ by less than 1 % are the same shape, so "Resample and stretch" isn't offered. Today any difference counts.

## Amends ADR 0032

A side bar's icon strip is the header of the panel it shows: its ⋯ sits at the strip's trailing end, and the panel's second title row goes. Everything else in [ADR 0032](0032-side-bars.md) stands.

## Consequences

- **Rollout.** Nothing changes in the app until the beta.4 tag. The code cards are cut from the spec's "Fixes by area", theme layer and HikariStyle first.
  - The theme layer gains typography, spacing and radius tokens, the metrics half that K2 left open.
  - `hikari_ui_theme_tests` takes the new expected values and keeps its floors.
- **Proof in each card.** Each card shows its surfaces in all four themes, at 1920 × 1080 and 1366 × 768, under vx11.
- **Not in this decision.** [#1208](https://github.com/altqx/hikari/issues/1208) (full-screen typesetting mode) and [#1206](https://github.com/altqx/hikari/issues/1206) (transport row) are drawn in this direction in their own boards.
- **No version bumps.** The API, plugin, Lua and media protocol versions are unchanged. Settings keys are unchanged: `video.dontAskForBadResolution` and `video.missingLayoutRes` keep their meaning, and the latter now decides whether the check box shows.
- **The Compact Studio record stays.** Its approval on #32 and the immutable study links stay as history; they no longer set the look.
