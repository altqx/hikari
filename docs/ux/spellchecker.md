# Spellchecker

How the Spellchecker panel looks in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). Where it opens and what it walks are [docking.md](../docking.md)'s (PU6); this document says how it looks and moves its notices into the panel. While it is checking, Discord shows Spell-checking ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q740–Q744; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The panel

Along the bottom, about 200 high, the full width.

- **Header** (Q740). "Spellchecker", **Spelling | Grammar** as a segmented group, the dictionary's language as a picker (140 wide), then in `muted` where the check is: "Line 46 of 412 · 3 found so far".
- **Body**, three columns:
  - the word in 20-unit semibold with "Line 46" in `muted`; under it the Line's text in `muted` with the word in `text`; then **Replace with** and its field;
  - the suggestions, 260 wide, in a `field` list; the chosen one has the `select` fill;
  - the actions (Q742), 300 wide, two columns: **Replace** (primary) and **Replace all**, **Ignore** and **Ignore all**, then **Add to dictionary** (`dictionary-add` glyph) across both.

## Marks

Q741. Words the dictionary doesn't know keep their red wavy underline (GRID_SPELLCHECKER in the Grid's Text column, EDITOR_SPELLCHECKER in the Line editor), whether or not the panel is open (owner decision 8 on [#531](https://github.com/altqx/hikari/issues/531), [visual language](visual-language.md)). While the panel is checking, the current word is also tinted `danger` at 18 %, radius 2, in both ([`dangerFill`](visual-language.md#semantic-appearance-tokens); in high contrast a 1-unit `danger` ring instead). The tint goes when the check ends or the panel closes.

## Panel states

| State | Shown |
| --- | --- |
| Checking | The header reads "Checking · line 214 of 412" over a 2-unit `accent` progress line |
| No dictionary (Q743) | The `notice-warning` glyph in `danger`, "No dictionary for English (US) was found, so nothing can be checked.", **Choose a language…** and **Details** (quiet). Replaces the toast A1 ([notices inventory](notices-inventory.md)) |
| Done (Q744) | "No more spelling mistakes", under it in `muted` what was done ("412 lines checked · 3 words replaced, 2 ignored, 1 added to the dictionary"), and **Check again** (quiet). Replaces the toast A3 |

A word that can't be added (A2) and a Line that can't be committed (A4) stay toasts.

## Discord

Presence shows **Spell-checking** (`spelling`) while the panel is checking. Its art is the `spellchecker` glyph on #E6D6B8 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6941093771)):

| Q | Decision |
| --- | --- |
| Q740 | The header holds Spelling \| Grammar, the language picker and where the check is |
| Q741 | While checking, unknown words are underlined in the Grid and the Line editor; the current one is tinted. Changed on 2026-11-24 during the build: the always-on wavy marks stay, and only the current-word tint is added. |
| Q742 | Replace (primary), Replace all, Ignore and Ignore all in two columns, Add to dictionary under them |
| Q743 | No dictionary is the panel's error state with Choose a language… and Details, not a toast |
| Q744 | The end of a check is the panel's done state with a summary and Check again, not a toast |
