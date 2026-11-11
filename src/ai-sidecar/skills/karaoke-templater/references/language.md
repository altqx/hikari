# The karaoke template language

HikariSub's templater is Karaoke Templater 2.1.7, as Aegisub 3.2.2 ships it, with a few named changes: the `char` modifier splits by grapheme cluster, and a problem names its Template or Code Line. Generated Lines get the Effect `fx`.

## Classes

The Effect field of a Comment Line starts with `template` or `code`, then a class:

| Class | Runs |
| --- | --- |
| `code once` | Once, before every other template. |
| `code line` | Once per karaoke Line. |
| `code syl` | Once per syllable. |
| `code furi` | Once per furigana syllable. |
| `template line` | Once per karaoke Line, giving one Line: the template text is put before each syllable's text, so it can hold per-syllable tags. A name after `line` (`template line glow`) joins several Lines into one template. |
| `template pre-line` | Once per karaoke Line; the template text goes once before the Line's text. |
| `template syl` | Once per syllable, giving one Line per syllable. |
| `template furi` | Once per furigana syllable. |

## Modifiers

Modifiers follow the class in the Effect field, such as `template syl noblank loop 3`. Code Lines take `all`, `noblank` and `loop` only.

| Modifier | Does |
| --- | --- |
| `all` | Runs for karaoke Lines of every Style, not only the template's. |
| `noblank` | Skips syllables with no text, or only spaces. |
| `notext` | Leaves the syllable's text out; the template's own text is the whole Line, such as a drawing. |
| `keeptags` | Keeps the karaoke Line's own override tags in the syllable text. |
| `loop N` (or `repeat N`) | Runs N times; `j` is the pass, from 1, and `maxj` is N. |
| `fx <name>` | Runs only for syllables with the inline-fx `<name>`, set with `\-name` in the karaoke. |
| `fxgroup <name>` | Runs only while the code-environment value `fxgroup.<name>` is not false. |
| `multi` | Runs for each highlight of a syllable timed more than once. |
| `char` | With `template syl`, runs once per character (grapheme cluster) instead; `graphemes = false` in a `code once` Line splits by code point. |

## Inline variables

`$name` in template text is replaced by a number or a string. Line variables begin with `l`, syllable ones with `s`; the plain names (`$x`, `$start`, …) take the syllable's value in a syllable template and the Line's in a line template.

| Variables | Give |
| --- | --- |
| `$layer`, `$style`, `$actor` | The karaoke Line's layer, Style and Actor. |
| `$lstart`, `$lend`, `$ldur`, `$lmid` | The Line's start, end, duration and middle in ms. |
| `$sstart`, `$send`, `$sdur`, `$smid`, `$skdur` | The syllable's start and end, from the Line's start, its duration and middle in ms, and its duration in centiseconds. |
| `$start`, `$end`, `$dur`, `$mid`, `$kdur` | The same for the current Line or syllable. |
| `$li`, `$si`, `$i` | The Line's number, the syllable's index from 1, and the current one. |
| `$lleft`, `$lcenter`, `$lright`, `$lwidth` | The Line's horizontal edges, centre and width. |
| `$ltop`, `$lmiddle`, `$lbottom`, `$lheight` | Its vertical edges, middle and height. |
| `$lx`, `$ly` | The Line's position for its alignment. |
| `$sleft`, `$scenter`, `$sright`, `$swidth`, `$stop`, `$smiddle`, `$sbottom`, `$sheight`, `$sx`, `$sy` | The same for the syllable. |
| `$left`, `$center`, `$right`, `$width`, `$top`, `$middle`, `$bottom`, `$height`, `$x`, `$y` | The same for the current one. |
| `$syln` | The number of syllables in the Line. |

## Inline expressions

`!expr!` in template text is replaced by the value of a Lua expression in the code environment, such as `!syl.duration/2!` or `!string.format("%.2f", $sx * 1.5)!`. Variables are replaced first, so an expression may use them.

## The code environment

| Name | Is |
| --- | --- |
| `line` | The Line being generated; set `line.layer`, `line.style` and the like to change it. |
| `orgline` | The karaoke Line it comes from, with `orgline.kara`, its syllables. |
| `syl` | The current syllable: `text`, `text_stripped`, `start_time`, `end_time`, `duration`, `left`, `center`, `right`, `width`, `height`, `inline_fx`, `i`. |
| `basesyl` | In a `char` or `furi` template, the syllable the character belongs to. |
| `meta` | Script Info values such as `meta.res_x` and `meta.res_y`. |
| `j`, `maxj` | The loop pass and the number of passes. |
| `string`, `math`, `_G` | Lua's string and math libraries, and its globals for the rest. |

| Function | Does |
| --- | --- |
| `retime(mode, addstart, addend)` | Sets the generated Line's times. Modes: `syl`, `presyl`, `postsyl`, `line`, `preline`, `postline`, `start2syl`, `syl2end`, `set` (or `abs`), `sylpct`. Returns "" so it can stand in `!…!`. |
| `relayer(n)` | Sets the generated Line's layer. |
| `restyle(name)` | Gives the generated Line another Style. |
| `maxloop(n)` | Changes the number of loop passes from inside the template. |
| `remember(name, value)`, `recall(name, default)` | Keeps a value from one template run to the next. |

Variables a `code` Line sets with plain assignment, such as `glow = "&HFFFFFF&"`, stay in the environment for the templates after it.
