# Karaoke template corpus (KE1)

`karaoke_templater_tests` templates each `*.ass` here with the bundled
`Automation/automation/Autoload/kara-templater.lua` and compares the saved
Document byte for byte with `<name>.out.ass`. The same run with
`kara-templater-aegisub-3.2.2.lua` must give the same bytes.

## The behaviour matched

`kara-templater-aegisub-3.2.2.lua` is the script HikariSub shipped before KE1,
unchanged. It is Karaoke Templater 2.1.7 exactly as Aegisub v3.2.2 released it
(`TypesettingTools/Aegisub` tag `v3.2.2`, `automation/autoload/kara-templater.lua`;
it differs only in CRLF line ends, a BOM and trailing whitespace).

Later Aegisub releases (3.3 onwards, upstream commit `7a8fda007d`, 2015-03-01)
cancel the whole run at the first template error. 3.2.2, and HikariSub, log
the error and carry on, leaving the failed `!expression!` in the output. The
`errors` Document locks that.

## Documents

| Document | Templates from |
|---|---|
| `stock-tutorials` | Aegisub's karaoke templater tutorials 1 and 2 |
| `stock-modifiers` | Aegisub's template modifiers reference: `line`/`pre-line` (named), `syl`, `furi`, `all`, `char`, `fx`, `fxgroup`, `keeptags`, `multi`, `notext`, `noblank`, `loop` |
| `stock-environment` | Aegisub's code execution environment and code lines pages: every `retime` mode, `relayer`, `restyle`, `maxloop`, `remember`/`recall`, `fxgroup`, every inline variable |
| `stock-furigana` | Aegisub's furigana karaoke page, with `<` and `!` breaks and full-width markers |
| `stock-inline-fx` | Aegisub's karaoke inline-fx page |
| `km-minimal` | Karaoke Mugen's minimal template; `\kf`, `\K`, `\ko`, zero-length syllables, a full-width space, existing `fx` and `karaoke` Lines |
| `ke4` | Kara Effector 3.6's `ke4` library (`Include/effector-auto4.lua`), as its own usage comments call it. `R()` is left out: it mixes `os.time()` into its result |
| `errors` | Parse and runtime errors in code and expressions, unknown variables and modifiers, and TT#141's 3.2-era template |
| `late-template` | A template after the first 50 Lines |

The templates are verbatim from those pages; the karaoke text is authored
for these tests (romaji and kana phrases, not lyrics), apart from the
modifiers page's own short examples.

Text extents come from a deterministic fake (0.55 em per character, 1 em for
CJK), so the goldens do not depend on installed fonts. To rewrite the goldens
from the 3.2.2 script after adding a Document, run the test with
`HIKARI_UPDATE_KARAOKE_GOLDENS=1`.
