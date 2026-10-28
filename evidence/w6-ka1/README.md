# KA1 (#276): language-aware syllable splitting and karaoke timing fixes, evidence

PR [#318](https://github.com/altqx/hikari/pull/318), branch `w6-ka1`, head `dc4e1e3a4` (qt merged up to a4891c2f5, PF1's Settings). Built with the owner's choices on the card (2026-10-21):
- an empty lead syllable, in Line mode too;
- K / Shift+K;
- Auto split divides time by character;
- an arrow on Auto split, 9 px wide;
- in the Karaoke row, the arrow folds into ⋯ first;
- a Line of more than 4096 visible characters is split as with Auto split off.

## What was built

- **Auto split reads the Line's language** (KA1-auto-split-language):
  - Romaji and kana split by mora, with options for the double consonant and n.
  - Kanji and Hangul (with the compatibility and halfwidth jamo) split per character.
  - English splits by Liang hyphenation over the shipped hyph_en_US.dic.
  - A Line is read as romaji or English by a weighted vote over its Latin words.
- **New syllables share the Line's time by character** (KA1-time-by-character).
- **`\k` Lines round-trip** (KA1-k-round-trip, KA1-kt-tag): each tag goes back where it was read, and `\kt` is no syllable.
- **A letter is a grapheme** (KA1-grapheme-letters).
- **Moving the start keeps the syllables at their audio times** (KA1-lead-in-keeps-syllables), measured from the start the commit writes:
  - an earlier start puts the extra time into an empty lead syllable;
  - a later start cuts the syllables it passes.
- **A split keeps the view** (KA1-split-keeps-view).
- **Two new audio commands** (KA1-syllable-commands):
  - Split syllable at the cursor: AUDIO_KARAOKE_SPLIT, id 1028, K.
  - Switch long syllables between \k and \kf: AUDIO_KARAOKE_KF, id 1029, Shift+K, with `audio.karaokeKfThreshold`.
  - Both are in Auto split's arrow menu and in ⋯.
  - Both measure the syllables from the start the commit writes, and both write the active Line alone: other selected Lines keep their texts.
  - With Auto commit off the text waits in the editor, and a later Commit from the panel or the editor still sends it to the active Line alone. A pending edit's other fields, such as an Actor typed for every selected Line, still go to every selected Line.
  - The settings are on PF1's Audio page, in the Karaoke section.
- **Several selected Lines keep their own `\k` text** when Add lead-in or a Line-mode start moves the active one.
- **A right drag of the start carries the syllables** by the start the commit writes, keeping their lengths off the 10 ms grid (MicroDVD), and a drag the window loses commits them to the active Line as its release would.
- **In Karaoke mode the panel takes the start a commit wrote**, so the syllables are drawn, played and hit from the start their text is read from: after a 105 ms lead-in from 200 ms in ASS the selection starts at 90, not 95.
- **Bounds on crafted Lines**, from the Codex security reviews:
  - a letter reads across 16 blocks at most;
  - a single block of more than 16384 `\k` tags keeps its text when the start moves;
  - Auto split stops at 4096 visible characters (KA1-auto-split-most);
  - an unclosed last block holding `\k` leaves the text as it is;
  - reading a Line with a huge prefix before one block is linear (a 2M prefix and 16384 `\k0` in under 2 s).

## Screenshots (Linux, xcb under a private Xvfb)

`shots-linux/` holds three states in Light, Dark, High contrast black and High contrast white. The Line is `kimi no hikari さくら 光る` with Auto split on:
- `ka1-auto-split-*`: the default panel width. The Line splits as ki|mi|no|hi|ka|ri|さ|く|ら|光|る. The arrow has folded, and Auto split and Tap are still shown.
- `ka1-more-menu-*`: the same width with ⋯ open. Its first two items are the syllable commands, with their keys.
- `ka1-arrow-menu-*`: a wider panel, where the arrow is shown, with its menu open.

The images came from a local, uncommitted screenshot test on `634bac623`; rounds 16–20 changed no panel layout.

## CI on dc4e1e3a4

- Ubuntu: [39838038234](https://github.com/altqx/hikari/actions/runs/39838038234), green.
- Windows: [39838042468](https://github.com/altqx/hikari/actions/runs/39838042468). The MSVC empty-cache job is green, 100% of tests passed. The warm-SDK job failed at `actions/cache/restore`: its key `qt-windows-sdk-39838042468-1` was missing (`fail-on-cache-miss`). That's the known cache eviction, not KA1.
- With #340 (KA7's lazy window) merged in, the suites that hit the 60 s limit on `634bac623` passed on MSVC: `translation_preset` 46.84 s, `saved_layouts.layouts` 57.71 s, `file_layouts.tree` 31.77 s, `file_layouts.places` 23.86 s. On `634bac623` the first three took 60.07, 60.07 and 60.01 s, and `places` took 40.5 s. HY1's green run had 47.14 and 50.83 s for the first two, and AD1's 41.76 and 47.87 s.

## Codex

Both reviews are clean on `dc4e1e3a4`:
- code review: "Didn't find any major issues" ([issuecomment-6349205388](https://github.com/altqx/hikari/pull/318#issuecomment-6349205388));
- security review: "No security issues were found" ([issuecomment-6349298530](https://github.com/altqx/hikari/pull/318#issuecomment-6349298530)).

Twenty rounds before that were fixed with tests, and every inline comment has an inline reply. Rounds 8–20:
- 8: ligatures (ﬁ, ﬃ) are letters; symbols such as `$` open the next syllable.
- 9: a new reading has no hovered letter; CJK Extension H; punctuation and symbols by code point; an unclosed last tag.
- 10: small ゕ, ゖ, ヵ and ヶ join the kana before them.
- 11: Add lead-in with several Lines selected no longer writes the active Line's text into all of them; supplementary kana.
- 12: the panel follows a several-Line lead-in; one block after a long text reads in one pass.
- 13: the start's right drag keeps the syllables' lengths off the 10 ms grid.
- 14: K and Shift+K write the active Line alone, measured from the start it keeps.
- 15: a right drag the window loses commits its carried syllables.
- 16: a letter click and a lead-in measure from the start the commit writes.
- 17: the panel takes the committed start in Karaoke mode; a lost right drag commits the active Line alone; IPA and Latin Extended-C to G letters; the new settings' strings in the catalogs.
- 18: a karaoke text waiting in the editor (Auto commit off) stays the active Line's on a later Commit.
- 19: only the command's own fields stay the active Line's; a pending edit's others go to every selected Line.
- 20: the same with Auto commit on; MicroDVD's start and end frames apart; halfwidth Hangul.

One finding was declined as an owner decision ([r4340779092](https://github.com/altqx/hikari/pull/318#discussion_r4340779092)): treating `\kt` as an absolute offset. KA1-kt-tag keeps it as a tag that is no syllable, as the card asks.

## Local runs on dc4e1e3a4

- Unit suites: `karaoke_split_tests` 24/24, `audio_karaoke_tests` 43/43, `audio_timing_tests` 34/34, `karaoke_tap_tests` 13/13, `edit_tests` 27/27, `editor_fields_tests` 22/22, `options_dialog_tests` 14/14, and the i18n suites.
- `shell_tests`: the hotkeys and settings partitions offscreen (59 + 51 passed) on round 17, and the karaoke several-Lines tests offscreen and under xcb on every round since. Under xcb only `audioTapToTime` and `audioPanelCustomiseShowsTheFoldedControls` failed at times, known local xcb flakes that pass offscreen and in CI.
- Each round's new tests fail without its fix.
- After the PK1 merge: the panels partition under xcb, with PK1's `popupsAreOnTheKit` (KA1 adds no dialog).

## Tests

- `karaoke_split_tests` (24):
  - romaji, kana, kanji, Hangul, English and mixed Lines, with both options;
  - the bound;
  - time weights and graphemes;
  - 21 tagged Lines round-tripping;
  - the split at a time and the start moves, including a differential test of the piecewise read against the whole-Line read;
  - the `\kf` switch.
- `audio_karaoke_tests` and `audio_timing_tests`:
  - the legacy replay checked against the KA1 outcomes;
  - the start drags (left, right, by the committed start, off the 10 ms grid, lost capture);
  - an active-only commit's fields against a pending edit for every selected Line, saved or waiting, from the panel or the editor, MicroDVD frames included;
  - the letter click keeping the view.
- `shell_tests`:
  - audioKaraokeSyllableCommands: K and Shift+K, the menus, the fallback without a cursor, ⋯, undo and the lead syllable;
  - audioKaraokeCommandsSeveralLines, audioKaraokeLeadInSeveralLines, audioKaraokeLostCaptureSeveralLines: several selected Lines keep their texts, with Auto commit off too;
  - audioKaraokeLostCaptureKeepsTheCarriedSyllables: a right drag of the start cut short;
  - audioTapToTime: the commands wait while tapping;
  - audioKaraokeMode and audioPanelRowFoldsItsControls.
- `hotkeys_tests`, `settings_import_tests` and `options_dialog_tests`: the new commands and settings, and the pinned counts.

Ledger rows: KA1-auto-split-language, KA1-auto-split-most, KA1-time-by-character, KA1-k-round-trip, KA1-kt-tag, KA1-grapheme-letters, KA1-lead-in-keeps-syllables, KA1-split-keeps-view and KA1-syllable-commands in `docs/qt/compatibility-decisions.md`, plus the KA1 row in `docs/qt/coverage.md`.
