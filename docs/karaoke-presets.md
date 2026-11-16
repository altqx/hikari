# Karaoke effect presets

KE3 ([#285](https://github.com/altqx/hikari/issues/285)) ships a small library of karaoke effects that the Template editor (KE2, [#284](https://github.com/altqx/hikari/issues/284)) inserts as editable template Lines.

## Original work

Every preset was written for HikariSub and is under HikariSub's licence. None is copied or adapted from Kara Effector (which has no licence), Aegisub's samples or anyone else's scripts. Presets 1–10 use only the stock templater: `template` and `code` Lines, inline variables, `!expressions!` and the functions of the template environment (`retime`, `j`), with no includes. Presets 11–20 (TH5d3, owner decision Q167) use karaOK's template syntax, and three of them load [ln.kara](karaoke-templater.md#lnkara).

## The format

Each preset is one ASS file in `src/application/karaoke-presets/`. The number in the file name sets the order of the list.

- Script Info `Title` is the preset's name, and `Description` says in one sentence what it does.
- Every Line is a Comment whose Effect is a `template ...` or `code ...` Effect. A Line's Layer is the generated Lines' layer.
- A karaOK preset has `Karaoke Templater: karaOK` in its Script Info.
- The Lines' Style in the file is `Default`. When the preset is inserted, the Lines take the karaoke Lines' Style.

So a preset can be opened in Aegisub, or its Lines pasted into any script, as they are. `hikari::application::readKaraokePreset` (`karaoke_presets.h`) reads a file into its title, description and Lines.

The presets use no random values. Their sparks and staggers come from the syllable index and the loop counter, in integer arithmetic, so the output is the same on every platform. Nothing carries over from one karaoke Line to the next either: a `code line` counter would count only the Lines a run reaches, and the preview runs one Line. What differs from Line to Line comes from the Line itself, such as its place in the file (`$li`). KE1 can therefore regenerate one karaoke Line alone, and the preview of any Line gives what Apply writes.

## Inserting a preset

In the Template editor, New › From a preset lists the presets by name; hovering one shows its description, in the menu's tooltip and the status bar. Choosing one inserts its Lines above the first karaoke Line, as New inserts one template: in the preset's order and with its Layers, with the karaoke Line's Style, as one undo step ("New template from preset"), the first Line made current. From then on they are ordinary template and code Lines, edited, previewed and applied like any other. New's choice covers them as it covers a template the user writes: in a Document the user chose to run they preview at once; in one that still asks, they preview once the user chooses Preview.

A preset's Lines are written in the Document's template language ([the mode](karaoke-templater.md#the-template-editor)):

- In a karaOK Document, a stock preset's Effects are translated as it is inserted (owner decision Q158): `pre-line` becomes `line`, `line` becomes `lsyl`, and `syl` with `char` becomes `char`. It then gives what upstream karaOK gives for it, which places syllables on whole pixels and counts the mode line in `$li`.
- In a 3.2.2 Document that has templates, the karaOK presets are off, marked with an (i), and say "Uses karaOK template syntax. Switch Template language to karaOK to use it."
- In a Document with no templates yet, a karaOK preset inserts and writes `Karaoke Templater: karaOK` in the same undo step.

The files ship inside the app as the resource `:/hikari/ui/karaoke-presets/`. The Template editor's controller lists each file's Title and Description again, in the `HikariSub.TemplateEditor` context, so the names and descriptions are translated; `hikari_ui_template_editor_tests` presetsAreTheirFilesInOrder fails when the two drift apart.

## The presets

| File | Name | What it does |
| --- | --- | --- |
| `01-classic-fill.ass` | Classic fill | The karaoke fill from left to right (`\kf`), fading in with the Line and out after it. |
| `02-staggered-fade.ass` | Staggered fade | Syllables fade in one after another, light up as they are sung and fade out together. |
| `03-colour-sweep.ass` | Colour sweep | A second colour wipes across each syllable from left to right while it is sung (an animated `\clip`). |
| `04-scale-pop.ass` | Scale pop | Each syllable swells to 130 % of its Style's scale as it is sung and settles back to it in the sung colour. |
| `05-bounce.ass` | Bounce | Each syllable hops up and lands again while it is sung. |
| `06-glow.ass` | Glow | A soft halo flares behind each syllable as it is sung, then fades. |
| `07-sparkles.ass` | Sparkles | Small sparks scatter from each syllable as it is sung (`loop 8`, `notext`, a drawing). |
| `08-echo.ass` | Echo | A copy of each syllable grows to 180 % of its Style's scale and fades away as it is sung. |
| `09-falling-letters.ass` | Falling letters | Letters drop into place one by one (`char`), then light up syllable by syllable. |
| `10-palette.ass` | Colour per line | Lines take turns through a short palette of fill colours (`code once`), by their place in the file (`$li`). |
| `11-word-pop.ass` | Word pop | Each word swells as it starts and settles back while it is sung (`template word`). |
| `12-word-wipe.ass` | Word wipe | The line shows whole, and a second colour wipes across each word while it is sung (`line`, `lword`, `word`). |
| `13-letter-cascade.ass` | Letter cascade | Letters appear one after another along the line before it is sung (`lchar`, `$ci`). |
| `14-letter-shimmer.ass` | Letter shimmer | Each letter tilts and brightens as it is sung, then straightens (`char`). |
| `15-furigana-rise.ass` | Furigana rise | Furigana rises into place over each syllable as it is sung (`furichar`). |
| `16-early-lead-in.ass` | Early lead-in | Each syllable lights up a moment before it is sung (`k_retime`). |
| `17-wave-bounce.ass` | Wave bounce | Each syllable bobs up and down on a sine wave while it is sung (`ln.wave`). |
| `18-shape-burst.ass` | Shape burst | Six stars burst out of each syllable as it is sung (`loop 6`, `ln.shapes.star`). |
| `19-line-gradient.ass` | Line gradient | The line's letters run through a band of colours from left to right (`ln.color.byHSL`, `$llen`). |
| `20-thai-fade.ass` | Thai fade | Each character cluster fades in as it is sung, so Thai vowels and tone marks stay on their letters (`char`). Thai support by Jibrill. |

## Tests

`hikari_backends_karaoke_presets_tests` inserts each preset's Lines above the first karaoke Line of `tests/backends/karaoke-presets/fixture.ass`, with that Line's Style, and runs "Apply karaoke template":

- The saved Document must equal the preset's golden (`tests/backends/karaoke-presets/<file>.out.ass`). It must also equal what the unchanged 2.1.7 script, as Aegisub 3.2.2 shipped it, writes in the same host, so the presets behave the same in Aegisub.
- The run reports no problems. Every generated Line has the karaoke Lines' Style, and no `$variable` or `!expression!` is left unfilled.
- A karaoke Line of another Style in the fixture gets nothing.
- After a whole run, regenerating each karaoke Line alone ("Apply karaoke template to selected lines") changes nothing.

`HIKARI_UPDATE_KARAOKE_GOLDENS=1` rewrites the goldens from the unchanged script.

The karaOK presets run on `fixture-karaok.ass` (the same romaji Lines and a furigana Line, in karaOK mode) and on `fixture-thai.ass`. Their goldens (`<file>.out.ass`, `<file>.thai.out.ass`) come from upstream karaOK (`karaoke-corpus/karaok/kara-templater-karaok.lua`, with upstream's own `ln/kara.lua`), and the built-in must give the same bytes. A preset that reaches a defect HikariSub fixed is a departure: its golden comes from the built-in and upstream must differ. Word pop and Word wipe on the furigana and Thai Lines reach TH5c-karaok-word-positions, and Letter cascade, Letter shimmer, Line gradient and Thai fade on the Thai Lines reach TH5c-karaok-graphemes (TH5d3-karaok-preset-departures). Each stock preset, translated as Q158 inserts it, must give upstream karaOK's bytes.

`hikari_ui_template_editor_tests` inserts each preset through the controller into the same fixture and checks:

- the Lines, their Style, Layers, order and place;
- the current Line;
- the preview, which equals the Lines Apply writes for that karaoke Line;
- Apply, which gives the same golden;
- Undo, which takes Apply and then the insertion off in one step each.

It also checks that a preset's Line edits like any template (aPresetLineEditsLikeAnyTemplate) and that New lists every preset by name with its description (newOffersThePresets).
