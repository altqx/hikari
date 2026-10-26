# Karaoke effect presets

KE3 ([#285](https://github.com/altqx/hikari/issues/285)) ships a small library of karaoke effects that the Template editor (KE2, [#284](https://github.com/altqx/hikari/issues/284)) inserts as editable template Lines.

## Original work

Every preset was written for HikariSub and is under HikariSub's licence. None is copied or adapted from Kara Effector (which has no licence), Aegisub's samples or anyone else's scripts. They use only the stock templater: `template` and `code` Lines, inline variables, `!expressions!` and the functions of the template environment (`retime`, `j`), with no includes.

## The format

Each preset is one ASS file in `src/application/karaoke-presets/`. The number in the file name sets the order of the list.

- Script Info `Title` is the preset's name, and `Description` says in one sentence what it does.
- Every Line is a Comment whose Effect is a stock `template ...` or `code ...` Effect. A Line's Layer is the generated Lines' layer.
- The Lines' Style in the file is `Default`. When the preset is inserted, the Lines take the karaoke Lines' Style.

So a preset can be opened in Aegisub, or its Lines pasted into any script, as they are. `hikari::application::readKaraokePreset` (`karaoke_presets.h`) reads a file into its title, description and Lines.

The presets use no random values. Their sparks and staggers come from the syllable index and the loop counter. KE1 can therefore regenerate one karaoke Line alone, and the preview gives what Apply writes.

## The presets

| File | Name | What it does |
| --- | --- | --- |
| `01-classic-fill.ass` | Classic fill | The karaoke fill from left to right (`\kf`), fading in with the Line and out after it. |
| `02-staggered-fade.ass` | Staggered fade | Syllables fade in one after another, light up as they are sung and fade out together. |
| `03-colour-sweep.ass` | Colour sweep | A second colour wipes across each syllable from left to right while it is sung (an animated `\clip`). |
| `04-scale-pop.ass` | Scale pop | Each syllable swells as it is sung and settles back in the sung colour. |
| `05-bounce.ass` | Bounce | Each syllable hops up and lands again while it is sung. |
| `06-glow.ass` | Glow | A soft halo flares behind each syllable as it is sung, then fades. |
| `07-sparkles.ass` | Sparkles | Small sparks scatter from each syllable as it is sung (`loop 8`, `notext`, a drawing). |
| `08-echo.ass` | Echo | A copy of each syllable grows and fades away as it is sung. |
| `09-falling-letters.ass` | Falling letters | Letters drop into place one by one (`char`), then light up syllable by syllable. |
| `10-palette.ass` | Colour per Line | Each Line fills in the next colour of a short palette; a `code line` keeps count. |

## Tests

`hikari_backends_karaoke_presets_tests` inserts each preset's Lines above the first karaoke Line of `tests/backends/karaoke-presets/fixture.ass`, with that Line's Style, and runs "Apply karaoke template":

- The saved Document must equal the preset's golden (`tests/backends/karaoke-presets/<file>.out.ass`). It must also equal what the unchanged 2.1.7 script, as Aegisub 3.2.2 shipped it, writes in the same host, so the presets behave the same in Aegisub.
- The run reports no problems. Every generated Line has the karaoke Lines' Style, and no `$variable` or `!expression!` is left unfilled.
- A karaoke Line of another Style in the fixture gets nothing.
- After a whole run, regenerating each karaoke Line alone ("Apply karaoke template to selected lines") changes nothing.

`HIKARI_UPDATE_KARAOKE_GOLDENS=1` rewrites the goldens from the unchanged script.
