# Thai automation changes in the owner's Aegisub 9829 install

Research only, 2026-11-09. Nothing in any repo was changed. The downloaded folder was read as text (`tr -d '\r'`, `sed`, `diff`, `strings`); nothing in it was run, `require`d or `dofile`d. Scratch copies and fetched upstream files are in `/tmp/thai-research/` (outside the folder).

Abbreviations:

- **INST** = the `automation/` folder of Jibrill's Aegisub 9829 build. Line numbers are those of the original files. Only CR line ends and a first-line BOM were stripped, so the numbers are unchanged.
- **OURS** = HikariSub `origin/main` at `039305976`, path `Automation/automation/…`.
- **322** = `tests/backends/karaoke-corpus/kara-templater-aegisub-3.2.2.lua` on the same commit.
- "inferred" = worked out by reading the code, not by running it. Every worked example in this note is inferred, because nothing was executed.

## 0. What the install is

- **`aegisub.exe` is not a Thai build.** Its FileVersion/ProductVersion string is `9829-migration04-56a83402b` (`strings`). Commit `56a83402b93c` is in both TypesettingTools/Aegisub and arch1t3cht/Aegisub: 2026-07-10, "Enable CI on migration branch". So it is a CI build of the upstream `migration` branch. Every Thai-looking string in the binary is ICU locale data: `icudt67l/th_TH.res`, `th_Thai_TH`, "Thailand" and baht names, `$thai = [[:thai:]`. There is no Thai-specific code.
- **All Thai work is in automation scripts by one author, "jibrill"/"Jibrill",** plus `th2syl.lua` by "Illyaz". Files with Thai content (grep for Thai bytes `\xE0\xB8`/`\xE0\xB9`, "thai", "jibrill"):
  - autoload: `kara-templater-mod(th).lua`, `th2syl.lua`, `Fsp for thai char.lua`, `lyger.GradientByChar(th).lua`, `petzku.Typewriter(th).lua`, `FadeWorks(th).lua`, `BezierToText(th).lua`, `0x.KaraTemplater.moon`, `Multi_Replace_v2.lua`.
  - Jibrill is also named, with no Thai content, in `Position shifter.lua`, `SelectByStyle.lua` and `include/Effector-utils-lib-3.6.lua:2` ("mod by jibrill"; that file has no Thai bytes).
- **The include libraries carry no Thai changes.** Each was diffed against OURS:
  - `karaskel.lua`, `unicode.lua` and `unicode-monkeypatch.lua` are identical.
  - `karaskel-auto4.lua` differs only by two comment typo fixes (OURS `Include/karaskel-auto4.lua:167`, `:198`).
  - `utils.lua`/`utils-auto4.lua` are stock 3.2.2 plus the ke4 short-function block. OURS has the same block inlined into `utils.lua:30-74`, so there is no behaviour difference.
  - `utf8.lua` is Kyle Smith's pure-Lua UTF-8 library (BSD-3, `INST/include/utf8.lua:1-40`). It has no Thai code and only `Multi_Replace_v2.lua:36` requires it. OURS does not ship it.

### The root cause, which HikariSub shares

There are three different "units" for a Thai word such as `กิน` ("eat", U+0E01 U+0E34 U+0E19):

| Splitter | Units of `กิน` | Units of `น้ำ` ("water", U+0E19 U+0E49 U+0E33) |
|---|---|---|
| `unicode.chars` (code points) | `ก` `ิ` `น` | `น` `้` `ำ` |
| `aegisub.re` `\X` (boost `u32regex`) | `ก` `ิ` `น` (inferred, see below) | `น` `้` `ำ` |
| UAX #29 grapheme clusters (PCRE2 `\X`, `grep -oP '\X'` here) | `กิ` `น` | `น้ำ` |

- **Boost does not group Thai marks.** Its `\X` groups a base with "combining" characters from a hard-coded table, and that table has no Thai range. Boost 1.92 `/usr/include/boost/regex/v5/regex_traits_defaults.hpp:547-590` jumps from `0x0D57` to `0x0F7F`, so it covers nothing in U+0E31–U+0E4E. Every Thai above or below mark is therefore its own `\X` match.
- **HikariSub has the same problem in Lua.** OURS binds `aegisub.re` to `boost::u32regex` (`src/helpers/lua/lua_native.cpp:4`, `:117`, `:186`), so HikariSub's `re` behaves the same. (Which boost version the Aegisub 9829 build uses is not verified.)
- **Any script that walks text one code point at a time breaks Thai.** The 3.2.2 `char` modifier (322 `:746-770`), stock GradientByChar, Typewriter, FadeWorks "By Letter" and BezierToText all do this. Each one either puts a mark in its own generated Line with its own `\pos` (the mark is cut off its base, and HarfBuzz-based renderers draw it on a dotted circle, inferred), or puts an override block between a base and its mark. Every jibrill change below works around this.
- **HikariSub's C++ side already uses UAX #29.** `src/core/graphemes.cpp:31-52` uses PCRE2 `\X`, which the karaoke editor and auto-split use (§3). The Lua side does not.

## 1. Changes, one row per behaviour

### T1. kara-templater-mod(th): the base is logarithm's karaOK "Karaoke Templater mod" 2.1.7c, not 3.2.2

- **What the file is.** `INST/autoload/kara-templater-mod(th).lua` = logarithm's `ln.kara-templater-mod.lua` plus a small Thai patch (T2–T5).
  - `INST/un-use/kara-templater-mod.lua` is byte-identical (ignoring whitespace) to upstream `lost-logarithm/karaOK` commit `c36c3ba5f07229a7997ae402bb2ec29fafe6eaf2` (2021-03-14), `autoload/ln.kara-templater-mod.lua` (`diff -b` gives 0 lines). Upstream HEAD is `f749c4fa64e7` (2024-08-16; the redirect from logarithm/logarrhythmic/slackingway lands there). It differs only in a `k_retime` rewrite.
  - `INST/un-use/kara-templater.lua` is stock 3.2.2 (whitespace-only diff against 322).
- **Header:** `script_author = "Niels Martin Hansen, mods by logarithm,th char support by jibrill"`, version `2.1.7c` (`:38-39`). BSD-3 Aegisub header (`:1-27`). The karaOK repo has no LICENSE file that GitHub detects.
- **What karaOK changes from 3.2.2,** before any Thai work (`diff -b 322 un-use/kara-templater-mod.lua`, 727 lines; (th) line numbers below):
  - **Classes renamed.** `pre-line` no longer exists: karaOK's `line` means 3.2.2's `pre-line`, `lsyl` means 3.2.2's `line`, and `lword`/`lchar` are new (`:151-152`, `:181`, `:210-215`). Every 3.2.2 `template line` changes meaning. 3.2.2 has `pre-line` at 322 `:127`.
  - **`char` and `furi` are new classes.** `word`, `char` and `furichar` are classes with their own template tables (`:54`, `:228-235`). 3.2.2's `char` *modifier* on `syl` templates (322 `:746-770`) is removed (`:1228`).
  - **The `syl` loop starts at 1, not 0** (322 `for i = 0, line.kara.n`).
  - **Zero-duration syllables are templated.** 322 `:716-718`, `if syl.duration <= 0 then return true`, is removed.
  - **Dollar variables round to 0.1 instead of to integers.** For example `$sleft` and `$lleft` (`:569-582`, `:897-906`; 322 `:459-466`, `:519-528`).
  - **New variables and functions:** `$ci`, `$slen`, `$llen`, `$len`, `k_retime(...)`, `tenv.word/char/syll/basechar`, and `subs`, `table`, `pairs`, `ipairs`, `tonumber` and others in the environment (`:345-355`, `:405-451`).
  - **New `style` modifier** (`:275-283`). `maxloop(0)` runs zero times (`if j <= tenv.maxj`, `:1273`).
  - **The macro gate is 30 lines instead of 50** and also accepts `code` Lines. Menu names differ.
- **Thai relevance:** none by itself. It is the infrastructure the Thai patch hooks into: `additional_proc_line` (`:617-882`) builds `line.words`, `line.chars` and `line.fchars`.
- **HikariSub equivalent:** none. OURS `Autoload/kara-templater.lua` is 3.2.2 plus KE1 (problem reporting, selected-Lines rerun, run report; OURS `:33-44`). It has no `word`, `lword`, `lchar` or `furichar`, and no `char` class.
- **Compatibility:** **changes output for all text** and **changes the public API.** Taking karaOK as the base would break 3.2.2 byte compatibility for nearly every corpus Document: `line` versus `pre-line`, rounding to 0.1, zero-duration syllables, syllable 0. It cannot ride as the default.

### T2. kara-templater-mod(th): Thai marks merge into the preceding `char` unit

- **Where:** `additional_proc_line`, `kara-templater-mod(th).lua:718-723`, list at `:45-50`, `containsString` at `:1388-1397`.
- **Before** (karaOK, un-use `:703`): every code point becomes a `char` object:

  ```lua
  for c in unicode.chars(line.text_stripped) do
      ci = ci + 1
      ...
  ```

- **After:**

  ```lua
  for c in unicode.chars(line.text_stripped) do
      if containsString(exception_char,c) then
      char.text_stripped = char.text_stripped..c
      char.text_spacestripped = char.text_spacestripped..c
      else
      ci = ci + 1 ...
  ```

- **The mark list** (`:46-49`): U+0E34–0E39 (sara i, ii, ue, uee, u, uu), U+0E31 (mai han-akat), U+0E48–0E4B (four tone marks), U+0E47 (maitaikhu), U+0E4C (thanthakhat), U+0E4D (nikhahit). It leaves out U+0E3A phinthu and U+0E4E yamakkan. U+0E33 sara am is handled by T4 instead.
- **What it fixes.** `template char` on `{\k50}กิน{\k50}ข้าว` ("eat rice") with text `{\pos($scenter,$smiddle)}`:
  - karaOK (and 3.2.2 `template syl char`) makes 7 Lines: `ก`, `ิ`, `น`, `ข`, `้`, `า`, `ว`. The `ิ` and `้` Lines each get their own `\pos`, so the marks float on their own (inferred).
  - (th) makes 5 Lines: `กิ`, `น`, `ข้`, `า`, `ว`.
- **Defects the patch leaves, all inferred:**
  1. **Syllable timing drifts.** `ci` is not incremented for marks (`:724`), but `line.kara[i].ci` counts every code point (`:630-635`). So the char-to-syllable lookup (`:768-781`) drifts left by one per mark already seen. In the example, `kara[2].ci = 4` and `ข` gets `ci = 3`, so `ข` is timed with syllable 1 (0–500 ms) instead of syllable 2.
  2. **Characters overlap.** `า` is then the first char of syllable 2, so `lf` resets to `syl2.left` (`:794-799`) and `า` is drawn on top of `ข`.
  3. **`keeptags` drops the marks.** Only `text_stripped` and `text_spacestripped` gain the mark; `char.text` does not. `template char keeptags` outputs `char.text` (`:1026-1027`, `:1266-1267`), so the marks disappear.
  4. **`char.length`/`$slen` stays 1, and `width` is the base glyph's width only.** Usually harmless, because marks have zero advance.
  5. **A line starting with a mark crashes.** `char` is nil there, so the code indexes nil.
- **The same patch for furigana chars** is at `:813-817`. There `fchar` is not reset between furigana blocks, so a furigana that starts with a mark joins the previous block's last char.
- **HikariSub equivalent:** none in Lua. The C++ counterparts split the same way:
  - Node effects split chars by code point (`src/application/node_layout.cpp:578`, `utf8Chars`), matching 3.2.2's `char`.
  - Grid "Split lines into characters" splits by UTF-16 unit (`src/application/grid_split.cpp:242-275`).
- **Compatibility:** changes output only for Thai text, but only once there is a `char` class (T1). Ported to OURS, the same idea would live in the 3.2.2 `char` modifier recursion (OURS `kara-templater.lua:995-1018`, `for c in unicode.chars(syl.text_stripped)`). There it changes output only for text with Thai marks; with a full UAX #29 version, for any combining mark (T13).

### T3. kara-templater-mod(th): the menu is in Thai and under `>KFX/`

- **Where:** `:1398-1401`. Macros `>KFX/สร้างคาราโอเกะจากสคริปต์(th)` and `…_no furigana`, filters `ตัวสร้างคาราโอเกะ(th)…`. A commented-out dead block sits at `:1274-1321`.
- **What it fixes:** nothing functional.
- **Equivalent:** n/a. HikariSub's menus are translated with Qt.
- **Compatibility:** none.

### T4. Sara am is decomposed and nikhahit moved before the tone mark, and the source Line is rewritten

- **Where:** `kara-templater-mod(th).lua:538-545`, inside `apply_templates`, before `karaskel.preproc_line`:

  ```lua
  l.text = l.text:gsub("ำ","ํา")      -- U+0E33 -> U+0E4D U+0E32
  l.text = l.text:gsub("่ํ","ํ่")      -- tone + nikhahit -> nikhahit + tone (x4 tones)
  ```

- **The same five gsubs appear in:**
  - `th2syl.lua:41-46` (where they do nothing, T6)
  - `Fsp for thai char.lua:190`
  - `lyger.GradientByChar(th).lua:54-60`
  - `petzku.Typewriter(th).lua:281-287` and five more places
  - `FadeWorks(th).lua:175-181`
  - `BezierToText(th).lua:728-734`
  - `Multi_Replace_v2.lua:372`
- **What it fixes.** Sara am is a spacing letter (gc=Lo), so `unicode.chars` and boost `\X` leave it alone. That leaves `น้ำ` as `น` + `้` + `ำ`, and the nikhahit part of `ำ` can never join its base. After decomposition, `น้ำ` → `นํ้า` (U+0E19 U+0E4D U+0E49 U+0E32). Nikhahit and the tone are both in the mark list, so the unit becomes `นํ้` + `า` (PCRE2 `\X` gives the same split here). The reorder matches what HarfBuzz's Thai shaper does with sara am internally (inferred from HarfBuzz behaviour, not checked in its source).
- **Side effects:**
  - **Every generated Line, including `template line` (karaOK) output,** carries the decomposed text.
  - **The karaoke source Line is rewritten permanently.** It is written back with `subs[i] = l` at `:552`, so the decomposed `ํา` replaces `ำ` in the user's karaoke Line too.
  - **The change is lossy.** Nothing recomposes the text, so search and spellcheck on `ำ` stop matching.
- **Equivalent:** none. UAX #29 makes it unnecessary: `น้ำ` is one cluster because U+0E33 is Grapheme_Cluster_Break=SpacingMark (`grep -oP '\X'` gives `น้ำ` as one unit).
- **Compatibility:** changes output only for Thai text (only for text containing U+0E33 or tone+nikhahit). It also edits source Lines, which no stock templater does.

### T5. kara-templater-mod(th) and karaOK words: byte positions mixed with code-point positions (existing karaOK bug, not fixed)

- **Where:** `:641` `line.text_stripped:gmatch("()(%s*)(%S+)(%s*)")` gives `ci` in bytes. Chars compare `words[i].ci > ci` with `ci` in code points (`:753-761`). Tag positions at `:624-628` are byte position minus code-point length.
- **Thai effect.** Thai is 3 bytes per code point, so char-to-word assignment and tag placement go wrong as soon as a Line has Thai and a space. In `กิน ข้าว`, word 2's `ci` is 11 (bytes) while its first char has `ci` 4, so every char lands in word 1 (inferred).
- **Equivalent:** n/a (OURS has no `word` class).
- **Compatibility:** n/a. If karaOK's `word` class is ever ported, this has to be fixed.

### T6. th2syl.lua: new macro, "Convert to syl for thai karaoke"

- **Where:** `INST/autoload/th2syl.lua`, author "Illyaz" (`:6`), version 1.0, no licence stated. Menu `>KFX/…` (`:74`).
- **What it does:**
  1. Strips all override blocks (`:36`) and resets `line.text = ""` (`:39`).
  2. Applies the T4 gsubs to that empty string (`:41-46`), so they **do nothing.**
  3. Walks code points. A unit is a base plus up to three following "marks" from a list that includes the space `" "` (`:48-64`). The rule counts distinct list members found in a 2-, 3- or 4-code-point window with `re.find`.
  4. Writes `{\kN}` before each unit with an equal share of the Line's duration (`:66-68`). `%d` truncates, so the remainder is lost (inferred, LuaJIT `%d` behaviour).
- **Example, all inferred:**
  - `กินข้าว` over 1000 ms → `{\k20}กิ{\k20}น{\k20}ข้{\k20}า{\k20}ว`.
  - `น้ำ` over 900 ms → `{\k45}น้{\k45}ำ`. Sara am is split off because of the no-op in step 2.
  - `เก` → `เ`|`ก`: leading vowels are their own unit.
  - `กิน ข้าว` attaches the space to the previous unit: `น `.
- **What it "splits".** These units are grapheme-ish clusters, not Thai syllables. A Thai syllable would be, for example, `กิน`|`ข้าว` (two syllables) or `เก`. Doing that properly needs a dictionary or a Thai syllable-break algorithm (libthai, ICU `BreakIterator` word breaks).
- **HikariSub equivalent: partial.**
  - Manual karaoke splitting in the audio box already snaps to grapheme clusters. See `karaokeLetterEnd`/`letterEnd` (`src/application/audio_karaoke.cpp:97-135`, `:709-712`) and `karaokeContinuesGrapheme` (`src/application/karaoke_split.cpp:451-459`).
  - Auto-split (`karaokeAutoSplit`, `karaoke_split.cpp:471-517`) classifies Thai as `Kind::Other` (`:140-157`), and `splitWord` skips `Other` runs (`:340-343`). A space-less Thai phrase therefore stays one syllable and only spaces split it. Each unit is already a full grapheme (`:129`).
  - There is no Thai syllable rule, and no equal-time "one `\k` per cluster" macro.
- **Compatibility:** none (new macro). An auto-split rule for Thai in C++ would change only Thai auto-split results.

### T7. Fsp for thai char.lua: new macro that replaces `\fsp` with per-cluster `\pos` Lines

- **Where:** `INST/autoload/Fsp for thai char.lua`, author "Jibrill", version 1.2 (`:1-4`), no licence.
- **What it does:**
  - Splits visible text into clusters, each a base plus marks from the list at `:14-29` (`split_clusters`, `:37-64`), after the T4 gsubs (`:190`).
  - Measures each cluster with `aegisub.text_extents` and wraps lines at the margins unless WrapStyle is 2 (`:203-264`).
  - Emits one Line per cluster, `{\pos(x,y)…}cluster`, with each cluster advanced by its width + the fsp value (`:297-352`). The original Line becomes a comment (`:354-358`).
  - The fsp value comes from the dialog or from the Line/Style (`:130-173`).
- **What it fixes.** Renderers add `\fsp` letter spacing per glyph or code point, so `{\fsp10}กิน` pushes `ิ` away from `ก` (renderer-dependent; inferred, not tested with libass or xy-VSFilter). The macro turns it into `{\pos(x1,y)}กิ`, `{\pos(x1+w1+10,y)}น`, keeping each mark on its base.
- **Equivalent:** none in OURS scripts or the Qt app (no grep hit for fsp splitting).
- **Compatibility:** none (new macro). Limits: `\move` is ignored. Only the first `\fsp` is read when "from line" is chosen (`:171-172`). Wrapping is the script's own, not the renderer's.

### T8. lyger.GradientByChar(th): no gradient tag inside a Thai cluster

- **Where:** `INST/autoload/lyger.GradientByChar(th).lua`. Upstream is `TypesettingTools/lyger-Aegisub-Scripts` `macros/lyger.GradientByChar.lua` at `b359c86295077480b781dbd4d4e4c682e8aeca3e` (2015-10-16), v1.3.1, MIT licence (repo).
- **The Thai change:**
  - **Before** (upstream `:149-157`): every `re.find(ttext,'\\\\[Nh]|\\X')` match other than the first gets `{*\tags}`, unless it is a space.
  - **After:**

    ```lua
    elseif containsString(exception_char,ch) then
        rtext=rtext..ch
    ```

    The mark is appended with no tag (`(th) :161-179`), plus the T4 gsubs on the Line text (`:54-60`).
- **Example:** `{\c&HFF0000&}กินข้า{\c&H0000FF&}ว`.
  - Upstream puts a `{*\c…}` block before `ิ` and before `้`, because boost `\X` does not group them (§0).
  - (th) puts blocks only before `น`, `ข`, `า`. The marks stay attached and take their base's colour.
  - `idx` still counts marks, so the gradient steps are spread over code points (minor unevenness).
- **Other changes:**
  - DependencyControl is replaced by plain `require` and `register_macro` (`:26-39`, `:257-265`). `haveDepCtrl` is never set, so the DepCtrl branch is dead.
  - It requires `LibLyger` (top-level `include/LibLyger.moon`), not `lyger.LibLyger`.
  - It removes the original Line's `ำ` permanently (T4).
- **HikariSub equivalent:** none. OURS ships `Include/lyger/LibLyger.moon` but not GradientByChar. OURS `gradient-factory.lua` and `skew gradient.lua` were not traced for Thai.
- **Compatibility:** changes output only for Thai text, in a script OURS does not ship.
- **A cleaner fix that is not the (th) one:** a UAX #29 `\X` in `aegisub.re` (T13) would fix upstream GradientByChar unchanged.

### T9. petzku.Typewriter(th): never put the hide tag between a base and its marks

- **Where:** `INST/autoload/petzku.Typewriter(th).lua`. Upstream is `petzku/Aegisub-Scripts` `macros/petzku.Typewriter.lua` at `874fca585611bd4486696c7d9d4d65edc80f97b0` (2022-09-07), v0.7.0. ISC licence text in the file header (`:1-13`, petzku 2020).
- **The Thai changes:**
  - **Before** (upstream `generate_line`): `new.text = start .. SEPARATOR .. rest`, with `SEPARATOR = "{\alpha&HFF&}"`.
  - **After** (`(th) :280-329`): if `rest` begins with one or two Thai marks (fixed 3-byte slices `rest:sub(1,3)`, `rest:sub(4,6)`, `rest:sub(7,9)`), those bytes are **dropped from the hidden part**: `start .. SEPARATOR .. rest:sub(4)` or `rest:sub(7)`. The same applies in both unscramble generators (`:369-423`, `:584-638`).
  - **Unscramble-fade** (`:455-582`) withholds a mark (`newchar = ""`) until the static last frame and logs "Wrong spelling…" for impossible stacks. Sara am is again split into `ํ` + random `า`.
- **Example,** fbf on `กิน` (3 frames):
  - Upstream: `ก{\alpha&HFF&}ิน` → `กิ{\alpha&HFF&}น` → `กิน`.
  - (th): `ก{\alpha&HFF&}น` → `กิ{\alpha&HFF&}น` → `กิน`.
  - The hidden mark no longer sits after an override block, so the hidden text keeps its final layout (inferred). The mark still gets its own frame, because steps are still code points (`:200` loops `unicode.len(raw_text)`).
- **Defects, inferred:**
  - In the fade generator the `thup` flags persist across frames of the same character. With a fade of 2 or more frames, each mark logs a spurious "Wrong spelling" warning from the second frame on (`:482-489`).
  - The fixed 3-byte slices assume the next code point is Thai.
- **Equivalent:** none (OURS does not ship Typewriter).
- **Compatibility:** changes output only for Thai text (in a script we do not ship).
- **A cleaner port:** step by grapheme cluster instead of code point. That would also give one frame per cluster, which (th) does not.

### T10. FadeWorks(th) "By Letter": Thai marks take no fade tag of their own

- **Where:** `INST/autoload/FadeWorks(th).lua`. Upstream is `unanimated/luaegisub` `ua.FadeWorks.lua` at `68844131fe59f8dd11e10475c079b537a26491ec` (2019-08-18), v5.0, Unlicense (repo).
- **The Thai changes:**
  - **Before** (upstream `:189-206`): `re.find(text,"\\S\\s*")`, and every match gets `{\alpha&HFF&\t(fin1,fin2,\alpha&H..&)}`.
  - **After** (`(th) :220-276`): a match containing a Thai mark is appended untagged and `count` is incremented. This comes with the T4 gsubs at `:175-181`, only in "By Letter" mode (`:168`).
- **Example,** fade-in `กิน`:
  - Upstream: `{…\t(0,lf,…)}ก{…\t(t,…)}ิ{…\t(2t,…)}น`.
  - (th): `{…\t(0,lf,…)}กิ{…\t(2t,…)}น`.
  - The mark fades with its base. It still uses up a time slot, so there is a gap of one step before `น` (inferred).
- **Equivalent:** none (OURS does not ship FadeWorks).
- **Compatibility:** changes output only for Thai text.

### T11. BezierToText(th): a mark goes into its base character's Line; the script is also rewritten wholesale

- **Where:** `INST/autoload/BezierToText(th).lua`, "Bezier Curve text(th)" v2.3, authors "an eer? & Bakura & Jibrill" (`:16-19`), no licence. OURS `Autoload/BezierToText.lua` is "Bezier Curve Imposition" v1.0, "an eer? & Bakura" (OURS `:8-11`). No canonical upstream repo was found.
- **The Thai changes:**
  - **Before** (OURS `:58-111`): each non-space code point becomes its own Line with its own `\pos`/`\frz` on the curve.
  - **After** (`(th) :622-634`):

    ```lua
    elseif containsString(exception_char,char) then
        blockSplit = true; add = char; l.text = l.text..add
    ```

    The mark joins the previous character's Line. Marks are also skipped in the width pre-scan (`:381-401`), and the T4 gsubs run at `:728-734`.
- **Example:** `กิน` on a curve.
  - OURS makes 3 Lines: `ก`, `ิ`, `น`. The mark is positioned and rotated on its own.
  - (th) makes 2 Lines: `กิ`, `น`.
- **Rewritten beyond Thai, so this is not a patch (`diff -b` of 732 changed lines):**
  - multi-curve paths with arc-length placement (`:318-348`)
  - inside/outside text side (`:350-370`)
  - character spacing
  - collision avoidance (`:653-655`)
  - tag compaction (`:63+`)
  - a settings dialog (`:52-60`)
- **Defect,** inferred: `:726-737` loops over every selected index and does `subs[i] = line` with the *current* Line. With several Lines selected, each selected Line is overwritten with a copy of the one being processed. Do not port that block.
- **HikariSub equivalent:** OURS ships v1.0 with the bug the Thai change fixes.
- **Compatibility:**
  - The Thai fix alone, applied to OURS: changes output only for Thai text.
  - Taking v2.3 wholesale: changes output for all text (new placement maths) and the public UI (new dialog).

### T12. 0x.KaraTemplater.moon: Thai marks join the preceding char unit, plus kara-templater `$` aliases

- **Where:** `INST/autoload/0x.KaraTemplater.moon`. Closest upstream is `The0x539/Aegisub-Scripts` `src/0x.KaraTemplater.moon` at `329bcc126e1371766d811ba4698ddaa3ca738469` (2025-01-28), with 166 changed lines against it. Upstream HEAD `74c383c7354c` (2026-05-07) has since added `keeptags` and `multi`, which the local copy lacks. Author The0x539, v0.1.0. The repo has no licence GitHub detects. The local copy adds no author line.
- **12a, Thai char merge** (`:637-655`, `:659-691`):

  ```moon
  is_thai_combining_mark = (ch) ->
      code == 3633 or code == 3635 or (code >= 3636 and code <= 3642) or (code >= 3655 and code <= 3662)
  ...
  if last_char and not last_char.is_space and is_thai_combining_mark ch
      last_char.text ..= ch ...; update_char_extents last_char; left = last_char.right; continue
  ```

  - **Why it is the best of the set:**
    - Its set (U+0E31, U+0E33, U+0E34–0E3A, U+0E47–0E4E) is exactly the UAX #29 Extend + SpacingMark set for Thai. It includes sara am, phinthu and yamakkan, so no T4 decomposition is needed.
    - It re-measures the merged unit with `text_extents` and keeps `i` consistent.
    - It resets `last_char` per syllable (`:665`), so it never merges across a `\k` boundary.
  - **Example:** `mixin char` on `กิน` gives 2 char objects, `กิ` and `น`, instead of 3.
  - **Compatibility:** changes output only for Thai text.
- **12b, `$` aliases** (`:839-954`): `$start`, `$end`, `$dur`, `$mid`, `$i`, `$x`, `$y`, `$left` … `$sleft`, `$scenter`, `$syln`, `$style`, `$actor`, `$margin_*`.
  - They are unrounded floats, unlike stock kara-templater's integer `$sleft`/`$lleft` (322 `:459`, `:519`), so the same template does not give byte-identical output.
  - `$kdur` changes from `syl.duration/10` (an error outside syl/char) to the duration of the char's syllable, or of the Line (`:958`).
  - The variable pattern changes from `%$[a-z_]+` to `%$[a-z_][a-z0-9_]*` (`:1002`). `$loop_a1` used to parse as `$loop_a` followed by "1".
  - **Compatibility:** changes the script's public API (adds variables and changes the `$` grammar).
- **12c, line indices:** `line.i`/`line.li` are set (`:733-734`). API addition.
- **12d, menu** under `>KFX/` (`:1242-1243`). None.
- **HikariSub equivalent:** none (we do not ship 0x.KaraTemplater). Our node effects split chars by code point (`node_layout.cpp:578`).

### T13. Missing piece that no script fixes: grapheme-aware `\X` and `unicode.chars`

- **The cause.** Every row above re-implements "base + following marks" with a hand-made Thai list, because `aegisub.re`'s `\X` (boost) and `aegisub.unicode.chars` work on code points. HikariSub's C++ already has `core::graphemeEnd` (UAX #29 via PCRE2).
- **Fix 1, inferred:** make `\X` in `aegisub.re` mean a UAX #29 cluster (`src/helpers/lua/lua_native.cpp`). Unmodified upstream GradientByChar (`\X`) and every other `\X` user would then be correct for Thai, Devanagari, emoji and other scripts.
- **Fix 2:** add a new `unicode.graphemes(s)` iterator (an API addition) for templaters and scripts to opt in.
- **Compatibility:**
  - Changing `\X` changes output for any text with combining marks that boost treats differently. That is all Thai, plus some other scripts, though boost already groups U+0300–0361 for Latin. It is a `re` API behaviour change, so it would need a ledger departure.
  - A new iterator: none.

### T14. Multi_Replace_v2.lua: Thai-aware "keep order" replacement (byte-level heuristics)

- **Where:** `INST/autoload/Multi_Replace_v2.lua`, "Emmanuel Leblond, jibrill", v2.0 (`:29-33`). The original multiReplace 1.0 is © 2015 Emmanuel Leblond (`:2`); no licence text was found in the first 24 lines.
- **What it does.** When replacing text in a Line with inline tags, "keep order" re-anchors the tags by counting base characters and ignoring Thai marks (`:332-358`, `:390-413`). It pads the replacement with spaces when the pattern has more base characters (`:348-357`) and applies the T4 gsubs (`:372`).
- **How it detects Thai.** Bytes are tested with character classes such as `[\224\184]` (`:284-291`, `:405-409`). These match single bytes, not code points, so detection is approximate (inferred). It also strips `ุ`/`ู` from the replaced chunk as a "lazy fix" (`:396`), which deletes real below-vowels (inferred).
- **Equivalent:** none. Find and replace in HikariSub is in C++ and does not move tags this way.
- **Compatibility:** none (new script). Not recommended as is.

## 2. Summary table

| # | Change | File | Fixes for Thai | HikariSub has it? | Compat impact | Licence / author |
|---|---|---|---|---|---|---|
| T1 | karaOK templater base (`word`/`char`/`lword`/`lchar`/`furichar` classes, `line`=pre-line, 0.1 rounding, zero-duration syllables, `k_retime`) | kara-templater-mod(th) | nothing directly | no | **all text + public API**: breaks 3.2.2 byte compatibility | BSD-3 Aegisub header; logarithm (karaOK c36c3ba5f072) |
| T2 | Thai marks merge into the previous `char` | kara-templater-mod(th) `:718-723` | marks stay on base in `char` templates | no (OURS `char` modifier `:995-1018`, node_layout `:578`, grid_split `:242` all split per code point or unit) | Thai only; buggy (timing drift, overlap, `keeptags` loses marks) | jibrill |
| T3 | Thai menu names under `>KFX/` | kara-templater-mod(th) `:1398` | — | n/a | none | jibrill |
| T4 | `ำ`→`ํา` + nikhahit/tone reorder, written back to the source Line | 8 scripts | lets sara am's nikhahit join the base | not needed with UAX #29 | Thai only; rewrites source text, lossy | jibrill |
| T5 | karaOK words mix byte and code-point positions | kara-templater-mod(th) `:641`, `:753` | — (unfixed bug) | n/a | n/a | logarithm |
| T6 | th2syl: one `\k` per base+marks unit, equal time | th2syl.lua | quick karaoke timing for space-less Thai | partly: grapheme snapping in manual split; auto-split leaves Thai unsplit | none (new macro) | Illyaz, no licence |
| T7 | `\fsp` → per-cluster `\pos` Lines | Fsp for thai char.lua | letter spacing without detaching marks | no | none (new macro) | Jibrill, no licence |
| T8 | GradientByChar: no tag before a mark | lyger.GradientByChar(th) | gradient tags no longer split clusters | no (not shipped) | Thai only | MIT (lyger repo); lyger, jibrill |
| T9 | Typewriter: hidden marks dropped; marks withheld in fades | petzku.Typewriter(th) | no tag between base and mark | no (not shipped) | Thai only | ISC; petzku, jibrill |
| T10 | FadeWorks By Letter: marks untagged | FadeWorks(th) | marks fade with base | no (not shipped) | Thai only | Unlicense; unanimated, jibrill |
| T11a | BezierToText: mark joins its base's Line | BezierToText(th) `:631-634` | marks stay on base along the curve | **no: OURS v1.0 has the bug** | Thai only | no licence; an eer?, Bakura, Jibrill |
| T11b | BezierToText v2.3 rewrite (multi-curve, collisions, dialog) | BezierToText(th) | — | no | all text + UI | same; also a multi-select overwrite bug `:726-737` |
| T12a | 0x: Thai marks merge into the previous char (UAX #29 set) | 0x.KaraTemplater `:637-691` | marks stay on base | no (not shipped) | Thai only | The0x539 (no licence detected) |
| T12b | 0x: kara-templater-style `$` aliases, `$` grammar with digits, `$kdur` change | 0x.KaraTemplater `:839-1002` | — | n/a | public API of 0x | same |
| T13 | (missing) grapheme-aware `\X` / iterator | — | the common root cause | C++ has `core::graphemeEnd`; Lua does not | `\X` change: all text with marks; new iterator: none | — |
| T14 | Multi_Replace keep-order with Thai counting | Multi_Replace_v2 | tag re-anchoring ignores marks | no | none (new script) | Leblond, jibrill |

## 3. HikariSub features checked

- **Karaoke templater.** OURS `Autoload/kara-templater.lua` is 3.2.2 plus KE1 (`:33-44`). Per-char work is `apply_one_syllable_template`, `:995-1018`, `for c in unicode.chars(syl.text_stripped)`. It has no Thai handling.
- **The corpus** (`tests/backends/karaoke-corpus/README.md`) requires byte equality with 3.2.2 for the `stock-*`, `km-minimal`, `ke4`, `errors` and `late-template` Documents. None of them contains Thai, so a Thai-only change to `char` would keep every existing golden (inferred: the README's Document list has only romaji, kana and CJK).
- **Audio karaoke** splits and merges by grapheme with blocks skipped: `letterEnd`, `audio_karaoke.cpp:97-135`; `karaokeLetterEnd`, `:709-712`; `karaokeGraphemePrefix`, `:714-720`.
- **Kanji timer** uses `karaokeLetterEnd` (`kanji_timer.cpp:179`).
- **Auto-split** (`karaoke_split.cpp`): units are graphemes (`:129`). Thai is `Kind::Other` (`:156-157`) and is never split inside a word (`:340-343`). Kana, Han, Hangul, romaji and English (with hyphenation) have rules.
- **Grapheme core:** `src/core/graphemes.cpp:31-52` (PCRE2 `\X`).
- **Node effects** (ADR-0023) use code-point chars, `node_layout.cpp:578`, so their "Export as template Lines" stays 3.2.2-compatible. The KE3 presets (`src/application/karaoke-presets/09-falling-letters.ass` etc.) inherit the same split.
- **Grid** "Split lines into characters" goes per UTF-16 unit (`grid_split.cpp:242-275`, `legacy::`). That breaks Thai marks and surrogate pairs (inferred).
- **Spelling** knows the Thai script (`src/core/spelling_scripts.cpp:30`). Unrelated to karaoke.

## 4. Decisions for the owner

### D1. Which templater gets the Thai fix?

- **(a) Recommended.** Fix the 3.2.2 `char` modifier in OURS `kara-templater.lua` so it iterates grapheme clusters, not code points.
  - It only changes output for text whose code points and clusters differ, which is effectively Thai and other combining-mark scripts. No corpus golden contains such text, so 3.2.2 byte compatibility is kept for the corpus.
  - It is still a departure from 3.2.2 for Thai Lines and needs a ledger entry (`docs/compatibility-decisions.md`) plus a new Thai corpus Document whose golden is *not* compared against 3.2.2.
- **(b)** Port karaOK (T1) as a second, separate script, like the (th) install does: "Karaoke Templater mod", with T2 done correctly.
  - It is opt-in by choosing that script, so the default templater is untouched.
  - The cost: a second templater to maintain, with T2's three bugs and T5 to fix first.
- **(c)** Leave Thai to node effects and 0x-style mixins only.

Recommendation: (a), and (b) only if Thai users actually write karaOK-syntax templates (`lchar`, `lword`, `furichar`). Ask the Thai users, since jibrill's templates are karaOK syntax.

### D2. Default or opt-in for the `char` fix (if D1 = a)?

- **(a) Recommended: default.** A code-point split of Thai is never what a user wants, so an opt-in nobody finds keeps the bug for everyone. Non-Thai output does not move.
- **(b) Opt-in via a template modifier,** e.g. `template syl char grapheme`. 3.2.2 logs an unknown modifier ("Unknown modifier in template", 322 `:239`) and keeps parsing, so templates stay loadable in Aegisub. This keeps strict byte compatibility even for Thai.
- **(c) Opt-in via a code-line flag,** e.g. `code once` `tenv.graphemes = true`. Invisible in the Effect field, and harmless in Aegisub (an ignored global).

Recommendation: (a), with (c) as an escape hatch (`tenv.graphemes = false` restores the code-point split) for anyone matching old Aegisub output byte for byte.

### D3. Sara am decomposition and nikhahit reorder (T4)

- **(a) Recommended: do not adopt it.** UAX #29 already keeps `น้ำ` together. Decomposing rewrites the user's source Line, is lossy and breaks search.
- **(b)** Adopt it only in generated text, never in source Lines.

Recommendation: (a). Revisit only if a renderer is shown to mis-place the tone over a whole `น้ำ` cluster (test xy-VSFilter and libass first).

### D4. New `$` variables or `syl.*` fields?

- None of the Thai fixes needs a new `$` variable. `$slen`/`$llen`/`$ci` (karaOK) and 0x's aliases are unrelated additions. If the `char` units become clusters, keep `syl.width` and friends meaning the same thing (the measured cluster).
- **(a) Recommended:** add nothing.
- **(b)** Add `$clen`, a cluster count, alongside the fix.

Recommendation: (a).

### D5. `aegisub.re` `\X` semantics (T13)

- **(a)** Switch `\X` to UAX #29 in `lua_native.cpp`. This fixes upstream GradientByChar and others unchanged, but changes `re` behaviour for every script, so it needs a departure.
- **(b) Recommended:** keep boost `\X` and add `unicode.graphemes` (a new API) for our own scripts.
- **(c)** Do nothing.

Recommendation: (b) now. Consider (a) behind a ledger decision once we know which bundled or popular scripts rely on `\X`.

### D6. Which non-templater scripts to carry, and how

- **BezierToText (shipped):**
  - **(a) Recommended:** apply only the T11a mark-joining to our v1.0, preferably by grapheme iteration, not the Thai list.
  - **(b)** Adopt v2.3 wholesale. This changes all output and UI, and brings the `:726-737` overwrite bug.
- **Typewriter, FadeWorks, GradientByChar (not shipped):** carry nothing unless we start bundling them. If we do, send grapheme-based fixes upstream (petzku is ISC, lyger MIT, unanimated Unlicense) rather than shipping `(th)` forks.
- **th2syl:** **(a) Recommended:** implement it in C++ auto-split. A Thai option that splits one syllable per grapheme cluster would be better than th2syl, because it keeps sara am, and could later use real Thai syllable breaks (ICU/libthai). **(b)** Bundle th2syl after fixing the no-op normalization (`:39-46`) and the truncated `\k`.
- **Fsp for thai char:** **(a) Recommended:** bundle it as a new macro after review (author consent needed, since no licence is stated). **(b)** Skip it.
- **0x.KaraTemplater:** **(a) Recommended:** skip it. If it is wanted, take upstream `74c383c7354c` and offer 12a upstream; do not take 12b.
- **Multi_Replace_v2:** skip.

### D7. Licences

- No licence is stated for th2syl (Illyaz), Fsp, BezierToText (any version) or Multi_Replace_v2.
- No licence is detected for the karaOK and 0x repos.
- Before bundling or porting jibrill's or Illyaz's code verbatim, get permission or reimplement from the described behaviour. Reimplementing is easy, because every fix is a small rule.

## 5. Sources compared

- karaOK `lost-logarithm/karaOK` `c36c3ba5f07229a7997ae402bb2ec29fafe6eaf2` and HEAD `f749c4fa64e73a7c4fd38fe2c194c875b4e6c962`, `autoload/ln.kara-templater-mod.lua`.
- lyger `TypesettingTools/lyger-Aegisub-Scripts` `b359c86295077480b781dbd4d4e4c682e8aeca3e`, `macros/lyger.GradientByChar.lua`.
- petzku `petzku/Aegisub-Scripts` `874fca585611bd4486696c7d9d4d65edc80f97b0`, `macros/petzku.Typewriter.lua`.
- unanimated `unanimated/luaegisub` `68844131fe59f8dd11e10475c079b537a26491ec`, `ua.FadeWorks.lua`.
- The0x539 `The0x539/Aegisub-Scripts` `329bcc126e1371766d811ba4698ddaa3ca738469` (closest) and `74c383c7354c371c9f4d881491f5f9cbc6a7dc57` (HEAD), `src/0x.KaraTemplater.moon`.
- Aegisub build: `TypesettingTools/Aegisub` `56a83402b93cdb2347848b744f5fdfe89ee51d56`.
- Not found: a canonical upstream for BezierToText. Not checked: arch1t3cht and zf scripts, DependencyControl feeds. None of them has Thai content in this install.
- Background: karaOK README ([repo](https://github.com/logarrhythmic/karaOK)) and arch1t3cht's [templater guide](https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/blob/main/doc/templaters.md).
