# AU20 #854: behaviour record

Sources were read, not run (clones made 2026-11-11):
- unanimated/luaegisub `ua.LineBreaker.lua` 2.4 and `ua.ChangeCase.lua` 3.0
- petzku/Aegisub-Scripts `macros/petzku.Autowrapper.moon` 0.5.1
- TypesettingTools/line0-Aegisub-Scripts `l0.InsertLineBreaks.moon` 0.2.0
- TypesettingTools/CoffeeFlux-Aegisub-Scripts `macros/Flux.TitleCase.moon` 1.4.1 (author tophf) and `macros/Flux.DialogSwapper.moon` 1.4.1
- TypesettingTools/zeref-Aegisub-Scripts `macros/zf.allCharsTo.moon` 1.0.5
- bundled `Automation/automation/Autoload/macro-2-mkfullwitdh.lua`

## Line breaks

**ua.LineBreaker, Insert Linebreak.** If the Line already has `\N`, it removes every break instead (`\N` becomes a space, `{\i0}\N{\i1}` is merged, `word— word` is joined). Otherwise it uses English heuristics on the text without the start block and trailing comments:
1. It breaks after `. , ? ! : ;`, after `...`, and inside `word—word`, but not after Mr./Mrs./Ms./Dr.
2. If there are several candidates it keeps the one nearest the middle (by character count). A balance check uses byte counts and `text_extents` widths, with a maximum ratio of 2.2, and drops the break when either half is wider than the video's usable width.
3. Otherwise it tries before conjunctions and prepositions, in three rounds of word lists (" but ", " and "… " the ", " a ").
4. If there is still no break, it walks a `\N` one word at a time to the point of least character-count difference.
5. It shifts the break around fixed English pairs ("kind of", "have to"…), never after "a/the/of…".
6. Settings: minimum characters and words, middle-break fallback, allow a break between two words, don't break Lines that fit. A dialog for manual breaking is optional.

**ua.LineBreaker, Shift Linebreak (right).** If there is no `\N`, it puts one at the start. It moves the `\N` past the next word, and tag blocks move with it. A break that reaches the last word is removed.

**ua.LineBreaker, Shift Linebreak Back (left).** This mirrors the right shift. With no `\N`, it puts one at the end and moves it before the last word.

**l0.InsertLineBreaks.** This inserts a `\N` every N characters (default 35), at the last whitespace before the limit. It only touches text sections, as ASSFoundation splits them. It doesn't balance.

**petzku.Autowrapper.** This works on every uncommented Dialogue Line in the file, not on the selection:
- A Line with `\N` gets `{\q2}` prepended if it has no `\q2`.
- A Line without `\N` that has `\q2` and fits within the width loses `\q2`, along with any `{}` that leaves.
- An over-wide Line gets a note in Effect: `## AUTOMATIC LINEBREAK ##`, `## POSSIBLE THREE-LINER ##`, `## THREE-LINER ##` or `## OVERWIDTH WITH FORCED WRAP ##`.
- There are three macros: add and remove, remove only, and notes only.

## Case

**Flux.TitleCase.** This works on the selection.
- `\N`, `\n` and `\h` count as whitespace. An unclosed `{` is text. Tag blocks and drawings (`\p` > 0, which carries across blocks) are skipped.
- A word is a run of anything except ASCII punctuation and whitespace, so multibyte characters stay whole. Leading `' ‘ “ « ‹ „ ¿ ¡ …` and trailing `' ’ ” » › …` are peeled off.
- The small words stay lower case: a, an, the, at, by, for, in, of, on, to, up, and, as, but, or, nor, via, vs. The exceptions are the first word, the last word, and a word whose preceding text contains `. ! ? :`.
- If the whole Line is in capitals, the rest of each word is lowered. Otherwise, a word that starts lower case and has a capital later (iPhone, eBay) is left alone.

**ua.ChangeCase.** This works on the selection and offers Words, Lines, Sentences, lowercase and UPPERCASE, plus a "mod" checkbox.
- **lowercase and UPPERCASE:** they change text outside `{}`, and keep `\N`, `\n` and `\h`. With "mod", lowercase lowers only all-caps words.
- **Sentences:** capitalises the first letter of the Line and the letter after `. ? !` (also across `\N`), and turns a lone "i" into "I". It also capitalises a fixed list of languages, weekdays and months, and Japanese honorific stems (-san, -kun…).
- **Words:** title case with an 80-entry list of prepositions, Roman numerals (Iv → IV), "Ok" → "OK", and lower-case honorifics.

**zf.allCharsTo.** This offers **upper case, lower case and "Capitalized" only**. Capitalized upper-cases the first non-digit, non-blank character after the start of the text and after each `.`. The script keeps `\N` and `\h` and skips shapes. **It has no full-width or half-width conversion.**

**Bundled demo "Make fullwidth".** This converts each code point outside `{…}` with a fixed table:
- `!`–`~` go to U+FF01–FF5E, except `"` → `”` U+201D, `'` → `’` U+2019 and `` ` `` → `‘` U+2018.
- Space is unchanged.
- **`\N`, `\n` and `\h` are converted too** (to `＼Ｎ`), so the break is lost. **Drawing commands are converted too.**

## Dialog Swapper (Flux.DialogSwapper)

It is **not** a text/actor swap. It swaps inline alternatives marked with the delimiter `*` (configurable). It runs three Lua gsubs in order on each Line:
1. `{*}A{*B}` → `{*}B{*A}`, with `A` holding no `{`.
2. `{**B}` → `{*}B{*}`
3. `{*}{*` → `{**`

For example:
- `Tanaka{*}-san{*}` ⇄ `Tanaka{**-san}`
- `{*}Big brother{*Onii-chan}` ⇄ `{*}Onii-chan{*Big brother}`

Other behaviour:
- It runs on **every Dialogue Line in the file**, not on the selection.
- With "Verify Style" (on by default), it only runs on Lines whose style contains `-` or `_` followed by Main, Alt or Overlap.
- A Line whose Effect is exactly `***` has its comment state toggled.
