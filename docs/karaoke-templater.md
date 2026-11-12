# Karaoke templater

KE1 ([#283](https://github.com/altqx/hikari/issues/283)) improves the karaoke templater. Since TH5b ([#833](https://github.com/altqx/hikari/issues/833)) it is built in: the Lua helper runs the template language natively, and Lua runs only the templates' `code` Lines and `!expressions!`. The Script Manager lists it as Karaoke Templater, built-in. Kara Effector (`Automation/automation/Include/effector-auto4.lua`, reached as `ke4` from templates) ships unchanged under its own licence.

## The behaviour matched

The templater HikariSub shipped before KE1 is Karaoke Templater 2.1.7 as Aegisub v3.2.2 released it. It differs from that release only in CRLF line ends, a byte order mark and trailing whitespace. KE1 keeps 3.2.2's output: a template with an error is logged and the run carries on, leaving the failed `!expression!` in the generated Line. Aegisub 3.3 and later (upstream commit `7a8fda007d`) cancel the whole run at the first error instead; HikariSub does not.

The lock is a corpus, `tests/backends/karaoke-corpus/`:

- the templates of Aegisub's templater documentation (both tutorials, the modifiers, the code environment, furigana, inline-fx);
- Karaoke Mugen's minimal template;
- Kara Effector's `ke4` functions;
- template errors, including TT#141's 3.2-era template;
- a template after the first 50 Lines.

`karaoke_templater_tests` applies the templates to each Document with the built-in templater and saves it. The saved bytes must equal the committed golden. The unchanged 3.2.2 script (`kara-templater-aegisub-3.2.2.lua` in the corpus) and the Lua script HikariSub bundled before TH5b (`kara-templater-hikari.lua`, the oracle) must give the same golden in the same host. The corpus README lists the sources.

## What HikariSub changes

Each change is a named departure in the [compatibility decisions](compatibility-decisions.md).

| Departure | What changes |
| --- | --- |
| KE1-no-50-line-gate | "Apply karaoke template" is offered when any Line is a template Line. 2.1.7 looked at the first 50 dialogue lines only. As in 2.1.7, code Lines alone do not offer it: without a template they generate nothing. |
| KE1-template-problems | Each problem with a template or code Line ends with `Template Line: N` or `Code Line: N`, N being the Line's number in the Grid. A named line template made of several Lines names the Line whose text the variable or expression is in. A run that had problems selects those Lines, the first active, and its progress window stays open so the log can be read. |
| KE1-regenerate-selected | "Apply karaoke template to selected lines" replaces only the selected karaoke Lines' generated Lines, as one undo step. |
| KE1-template-timing | A run reports its time and, per template or code Line, the time it took and the Lines it generated. The report goes to the status bar and the Log. |
| KE1-render-cost-overlay | "Render cost" in the Video menu and the video's context menu (GLOBAL_VIDEO_RENDER_COST) draws over the video what the shown frame's subtitles cost to draw. |
| TH1-template-char-graphemes | `template char` gives each grapheme cluster its own Line, so `กิน` gives `กิ` and `น` and `น้ำ` stays whole; a combining accent, an emoji ZWJ sequence and a flag are one char. 2.1.7 splits by code point. `graphemes = false` in a `code once` Line splits as 2.1.7 does. Thai support by Jibrill. |
| TH5b-builtin-templater | The templater is built in rather than a script in Autoload. It can be reloaded but not edited or unloaded. Its macros and their shortcuts keep their names. |
| TH5b-line-template-order | Named line templates run after the unnamed ones, in the order their names first appear. |
| TH5b-no-leaked-globals | Evaluating an `!expression!` no longer sets the globals `f` and `err`. |
| TH5b-no-trace-lines | The level 5 trace lines are gone. |
| TH5b-unmeasured-text | Text the host cannot measure is a problem and counts as 0 wide; 3.2.2 stopped the run. |
| TH5b-native-layout | Karaoke Lines are laid out from the Styles as they were when the run started; `code` Lines that change Style tables no longer move later syllables. |
| TH5c-karaok-mode | `Karaoke Templater: karaOK` in `[Script Info]`, or karaOK-only syntax, runs the templates as upstream karaOK does. |
| TH5c-karaok-word-positions | karaOK words count characters, not bytes. |
| TH5c-karaok-syl-char | `template syl char` is a char template; upstream ran it twice. |
| TH5c-karaok-named-classes | A named line template keeps its first unit class. |
| TH5c-karaok-k-retime | `k_retime` shifts this loop iteration's syllable only. |
| TH5c-karaok-style-text | `style X` matches X as plain text, not as a Lua pattern. |
| TH5c-karaok-furichar-tags | Each `furichar` character takes its own syllable's tags, without `\k`; upstream took tags by index. |
| TH5c-karaok-no-leaked-globals | A karaOK run leaves no `textbuffer` or `w` global. |
| TH5c-karaok-graphemes | karaOK's `char`, `lchar` and `$ci`/`$slen`/`$llen`/`$len` count grapheme clusters. Thai support by Jibrill. |
| TH5d1-stock-additions | In 3.2.2 mode, karaOK's `style`, `lsyl`/`lword`/`lchar`/`word`/`furichar`, `code word`/`char`/`furichar`, `k_retime` and `table`/`pairs`/`ipairs`/`tonumber`/`tostring`/`type` work in place of 3.2.2's warnings. `$ci`/`$slen`/`$llen`/`$len` stay text, as in 3.2.2 (Q346). |
| TH5d1-furigana-styles-false | `furigana_styles = false` in a `code once` Line adds no "-furigana" Styles, as upstream's "(modded)" macro does. |
| TH5d3-new-documents-karaok | A Document made with File › New is in karaOK mode until a mode line or a template or code Line decides. The first template inserted from the Template editor (New, a preset) or exported from a node graph writes `Karaoke Templater: karaOK` in the same undo step; saving writes nothing. |
| TH5d3-stock-presets-translated | A stock preset inserted into a karaOK Document has its Effects translated to karaOK's words (`pre-line` → `line`, `line` → `lsyl`, `syl char` → `char`). |
| TH5d3-karaok-preset-departures | The karaOK presets give upstream karaOK's Lines except where they reach TH5c-karaok-word-positions or TH5c-karaok-graphemes. |
| TH5d3-editor-follows-mode | In karaOK mode, the Template editor's New and Effect picker use karaOK's words. |
| TH5d2-ln-kara | `require "ln.kara"` loads the [ln.kara](#lnkara) HikariSub ships, before a copy in the user's Include. |
| TH5d2-ln-kara-tags | `ln.line.tags`, `ln.syl.tags`, `gc` and `ga` read the Line's tags; upstream always gave the defaults. |
| TH5d2-ln-kara-string-defaults | A string default (`ln.line.tag("bord", "\\bord3")`) works; upstream stopped with an error. |
| TH5d2-ln-kara-pos-table | The table forms of `ln.tag.pos` and `ln.tag.kanjipos` work; upstream stopped with an error. |
| TH5d2-ln-kara-move-offsets | `ln.tag.move` with its start offsets left out starts at the anchor; upstream stopped with an error. |
| TH5d2-ln-kara-syldur | `ln.syldur()` and `sd()` with no argument give the whole duration; upstream gave 0. |
| TH5d2-ln-kara-shift | `ln.shapes.shift` and `ln.shapes.circle` write whole coordinates without decimals; upstream wrote `%.2f` for most. |
| TH5d2-ln-kara-lerp | `ln.math.lerp` between a string and a number is an error; upstream read a numeric string as a number. |
| TH5d2-ln-kara-kanjipos | `ln.tag.kanjipos` in 3.2.2 mode is a problem that says it needs karaOK mode. |

The generated Lines are byte for byte 3.2.2's in every case, apart from `char` templates over clusters (TH1, locked by `departures/thai.ass`), the order of several named line templates (`departures/named-line-templates.ass`) and the marks below, which a save adds to `[Aegisub Extradata]`. The macro returns a selection only when there were problems; otherwise the selection stays as it was.

### Regenerating the selected Lines

The templater marks each generated Line with the karaoke Line it came from. The mark (`LineRecord::generatedFrom`) is saved with the file as a token on the karaoke Line and the same token on each Line generated from it (`_hikari_kara_source` and `_hikari_kara_from`, in the [document model's key table](document-model.md)). It goes in with the run's undo step and is read back when the file opens, so it survives a save and a reopen, in Aegisub 3.4 and later too. Selecting a karaoke Line, or one of its generated Lines, and running "Apply karaoke template to selected lines" does the following:

1. Run the templates over every karaoke Line up to the last selected one, in order, as a whole run does. Templates can carry state from one Line to the next (code-line counters, `remember`/`recall`), so a selected Line gets what a whole run would give it.
2. Keep only the selected Lines' output. The other Lines are not changed and their problems are not reported.
3. Replace each selected Line's old generated Lines with its new ones, where the first old one was. A Line that had none gets them at the end.
4. Report problems with the numbers the Lines have after the run.

A file saved without the marks, by an older Hikari or by another editor's templater, has none. Its generated Lines are then matched against what the templates give the other unmarked karaoke Lines now, compared as the saved file would read them back, with a TLMode Line's original text as well as its translation. A run generates in karaoke Line order, so identical generated Lines go to the karaoke Lines in file order: each takes the first one still free. The generated Lines left unmatched are the selected Lines' output, but only where they can be. Each must have the style of a selected Line or of one of its generated Lines now. When the generated Lines are in karaoke Line order, it must also lie between those of the karaoke Lines around that Line. Otherwise it may be left over from a karaoke Line that now generates fewer Lines, and the run is cancelled. Lines left over from a karaoke Line of the same style, next to a selected one, cannot be told apart. If another karaoke Line's output is not there as the templates give it now, the run is cancelled with an explanation and nothing changes. That happens when the template or those Lines changed, or when the template uses random values. A whole run marks every generated Line, and after that selected Lines can be regenerated on their own.

Selecting every Line gives the whole run's Document. The one difference is that generated Lines already in the file are replaced where they were instead of at the end.

### The run report

The templater attaches a JSON report to its result (version 1):

```json
{"report":"karaoke-templater","version":1,"mode":"all","seconds":0.84,"prepare_seconds":0.12,
 "karaoke_lines":12,"generated":214,"problems":0,
 "templates":[{"line":3,"effect":"template syl noblank","style":"Romaji","seconds":0.51,"generated":144}]}
```

- `mode` is `all` or `selected`.
- `prepare_seconds` is karaskel's measuring of the karaoke Lines.
- `line` is the template or code Line's number in the Grid after the run. A named line template made of several Lines is one template, listed under its first Line.
- `issues` (KE2) lists each problem once, however many syllables or Lines ran into it, up to 100: `{"line":3,"kind":"template","message":"Unknown variable name: …","start":10,"length":10}`. `message` is 2.1.7's, without the Line's number. `start` and `length` give the variable or expression in that Line's own text, in bytes from 1; `start` is 0 for a problem without a place there, such as a code Line's, an unknown modifier or an expression a variable's value brought in (an Actor of `!…!` put in by `$actor`). A named line template's problem is placed in the Line it is in.

Times come from a steady clock in the helper, so they include waits for the host's text measurements. A selected-Lines run counts the selected Lines' work only.

The app shows the summary in the status bar for 10 seconds. It writes the templates to the Log, slowest first, without a toast.

### The render-cost overlay

Over the video's top left corner the overlay shows:

- how long the frame's subtitles took to draw;
- with libass, the events shown, the bitmaps it drew and their area;
- how many of the shown events use `\blur` or `\be` above 0, or a drawing `\clip` or `\iclip`, in an override block.

The renderer selection times every render. Counting walks every event of the track, so libass counts only while the overlay is shown, and the time shown leaves the counting out. A CSRI renderer on Windows reports its time only. The setting is `video.renderCost`, off by default. The command has no key by default.

## For scripts: `aegisub.hikari`

The helper adds a table to `aegisub`. Stock scripts never see it unless they look for it.

| Function | Meaning |
| --- | --- |
| `sources()` | `{[i] = source}` for each dialogue line generated from another Line still in the subtitles object, by index |
| `set_source(i, source)` | Line `i`, which the macro added, was generated from Line `source`, a Line of the Document |
| `report(text)` | Attaches `text` to the run's result, replacing an earlier report |
| `clock()` | Seconds on a steady clock |

The Lua protocol carries these as version 6: `MacroDialogueLine::generatedFrom` and `MacroResult::report`.

## karaskel in scripts

Since AU17 ([#851](https://github.com/altqx/hikari/issues/851)), the bundled `karaskel.lua` hands `karaskel.collect_head` and `karaskel.preproc_line` to the helper (`aegisub.__karaskel_impl`). The helper builds the same tables as karaskel's Lua, field for field and in the same `pairs()` order, and logs the same lines. `preproc_line` lays the Line out from the meta, styles and line tables the script passes, not from the Document. The sub-stages (`preproc_line_text`, `preproc_line_size`, `preproc_line_pos`, `do_basic_layout`, `do_furigana_layout`) stay Lua.

karaskel's own Lua runs instead when the helper could give a different result:

- `collect_head`: `subs` is not the host's, `table.copy` or `aegisub.debug.out` was replaced, `karaskel.furigana_scale` is not a number, there is no Style, or a Style or info line would make the Lua raise;
- `preproc_line`: a sub-stage or `unicode.charwidth` was replaced; `aegisub.text_extents`, `aegisub.parse_karaoke_data` or `aegisub.debug.out` is not the host's at the call; the trace level is 4 or more; the line already has `kara` or `styleref`; a table has a metatable; or a field karaskel reads is missing, has another type, or disagrees with the layout (margins apart from `margin_v`, an alignment outside 1 to 9, fractional times).

So an error a script catches with `pcall` has karaskel's own message.

## The template editor

KE2 ([#284](https://github.com/altqx/hikari/issues/284)) adds the Template editor, a panel tabbed with the Line editor (View › Panels › Template editor; closed by default). It lists the Document's template and code Lines in Grid order and edits one at a time: its Effect, Style, Layer and text. The text is coloured as the Line editor colours a template or code Line.

- **Edits go into the template Line.** Each pause in typing (800 ms), leaving the field, choosing another template, Apply, any macro starting (from a shortcut or the Automation menu as well), any save (Save, Save As, Save all), choosing another tab and closing the tab put the edit in as one undo step, "Edit template", in the Document it was typed for; typing that ends where it began makes no step. In TLMode the text field and the list hold the translation, which is the text the templater reads, and the preview lists the translations the run writes. The Layer takes the Line editor's range, negative Layers included. The Line editor's draft is not used: it belongs to the active Line, and moving the active Line would move the video off the karaoke Line being previewed. Only the generated Lines wait for Apply. A template Line changed since the last Apply to every karaoke Line is marked in the list, per Document (Apply to the selected Lines leaves the others' output as it was, so the marks stay): a visit to another tab keeps the marks.
- **The preview runs on the current karaoke Line.** That is the active Line when it is a karaoke Line (as the Line editor's draft makes it), a generated Line's karaoke Line (in a file opened without KE1's marks, found first as KE1's selected-Lines run finds it: one run gives every karaoke Line's Lines, and each takes the first free generated Line that reads back the same, in file order (a karaoke Line the file holds none of the Lines of, its template added since, takes none); when the other karaoke Lines' generated Lines are no longer what the templates give them, that run refuses as Apply would, the refusal is the preview's problem, and the next change runs it again; a generated Line clicked that no karaoke Line gives with the templates previews nothing, and the panel says to apply them to the whole file), or the last one when a template or code Line is active. Each edit runs "Apply karaoke template to selected lines" on a reduced snapshot: the script info, the styles, the template and code Lines with the edit, and that Line alone, selected, with the Line editor's draft in place of its Line (Apply commits it before it runs; typing in the Line editor previews again, after asking first when the templates hold Lua). Every other Line before it, but the generated ones a whole run deletes first, is an empty Comment there, so the Line keeps the index Apply gives it (`$li`, `line.i`); the `$` values' probe and the run that tells the current template's Lines apart keep it too. The run uses its own helper on the loaded `kara-templater.lua`, so it takes no part in the Automation menu's runs and writes nothing to their log; a newer edit cancels a run in flight, and a template that never ends loses its helper, which loads again. Previews run only while the panel is the tab shown, and only on a Document whose templates the user has chosen to run. A file's template and code Lines are Lua with the user's rights, and the Automation menu runs them only when asked. So a Document with a template shows "This file's templates run Lua code with your rights. Preview them if you trust the file." with a Preview button, and nothing of it runs (no preview, matching run or `$` values) until the user clicks it or applies the templates. A file without templates asks too once the user writes one: its Actor and Style values reach the templates and the `$` values. Templates New adds in a Document the user chose to run are covered. The panel's own edits keep the choice, and so does an Undo of them. Template or code Lines that come or change any other way (a paste, an import, a translation pasted over one, an Effect, text or Style typed in the Line editor: the Style picks the karaoke Lines a template runs on), or that are moved, sorted or deleted into another order (a named template joins its Lines' text in file order, and code Lines run in it), bring the notice back, and so does a karaoke Line (a `template line karaoke` too, which the stock templater runs as one) whose Style and Actor no karaoke Line of the file had together when the user chose (the templater puts them into templates as `$actor` and `$style` before it runs their `!expressions!`; with a `!` in either, the whole Line as it was, since its text, Effect and times decide whether the templates reach it). When the file holds Lua (a code Line, a `!` expression in a template, or a `!` in a karaoke Line's Style or Actor, which `$actor` or `$style` makes an expression), which can read every karaoke Line (`orgline.text`, `$actor` in `!$actor!`) and the Styles and Script Info (`orgline.styleref`, `meta`), a karaoke Line's text, translation or Effect changed any other way than the panel's (a paste, a split, typing in the Line editor) asks again too, and so does any change to a Style or Script Info property (an import, the Style manager, a resolution set); a retime does not. Templates with no Lua leave the karaoke text to the user. Each choice is the whole state, templates and pairings together: a template from one choice and an Actor from another ask again. The choice lasts while the Document is open; a reload from the file asks again. Script Info, the styles, the Lines, the current template and the host state the run reads (the video and its frame times, the frame shown once paused, keyframes, the audio selection, read from the Line editor's draft as Apply reads it, and the file's path) decide whether an edit or a change of them needs a new run, and a templater reloaded from the Automation panel runs it again; one unloaded takes its preview and problems away. The current Line follows Grid clicks and the Line editor's own moves (Enter, Ctrl+D). The run reads the host as Apply does (text measurement, frames, video size, keyframes, the project), and nothing else. Its Lines are what Apply writes for the Line, byte for byte, on the corpus. The cost is about 1.2 ms on `stock-tutorials` (`hikari_backends_karaoke_preview_tests` APreviewFitsInAFrame), well inside a 24 fps frame.
- **"Preview on Line N"** lists those Lines: layer, times and text, the current template's own Lines marked (a second run without it tells them apart). Each template's row gives its Lines and time from the last preview, or from the whole run after Apply.
- **The video draws the preview** in place of the Line's generated Lines, as Apply will: the Lines generated from it go, the preview standing in their place in the file's order (a Line that made none before has its preview stand before the Lines of the karaoke Lines after it, as Apply writes them; in a file without KE1's marks those are found first), and so does the karaoke Line itself when its templates make Lines (Apply turns it into a Comment). A file opened without KE1's marks has its Lines found by one matching run per karaoke Line, on the templates as they are before the pending edit; it runs again when those Lines change (an Undo, a deletion, an Apply). If that run fails (the templates no longer make the file's Lines), nothing is left out. A badge in the picture's corner says "Preview · Line N · not applied"; the render cost moves below it. With the last template gone the preview goes too, and so do a run still going and its problems. A `template line karaoke`, which the stock templater runs as a template and as a karaoke Line, previews as the source with its pending text and is matched to its own generated Lines. The eye in the panel's header turns the preview off on the video. Loop the Line, beside it, plays the karaoke Line's time again and again until a pause, a stop or a seek; another Line previewed, or new times for it, loops that span instead.
- **Problems are placed.** The run report's `issues` give the problem's Line and place: the template's row says Problem, the variable or expression has a wavy line under it, and the message (2.1.7's first line) is under the text. A first preview that fails says so in place of the Lines ("No preview of Line N: the templates stopped with a problem."). The preview keeps the last run that worked, dimmed and marked "Kept from the last run that worked", and Apply is off until the problem is fixed. Apply also waits for the last edit's preview, so a problem typed just before it is known first.
- **`$` and `!`** open a list of what fits at the cursor: after `$` the stock variables, each with its value on the current Line's first syllable (one probe run), and inside an expression the template environment's functions. ↑ and ↓ choose, Enter or Tab inserts, Esc closes. After `ln.` (TH5d2, owner decision Q164), in an expression or a code Line, the list holds [ln.kara](#lnkara)'s namespaces, and after `ln.tag.` and the like its functions, each inserted with example arguments. `ln` itself is offered once a code Line loads `ln.kara`, and its shorthands (`st`, `sd`, `gc`, `ga`, `rgb`, `hsl`, `hsy`, `rnd`, `set`, `rset`, `clamp`, `fl`) once one calls `ln.init(tenv)`.
- **The Effect's picker** writes stock syntax: what the template runs for (Line before its syllables, Line, syllable, character, furigana syllable; for a code Line once, Line, syllable, furigana syllable) and the modifiers in plain words. The field takes stock syntax as typed too, the class after modifiers included (`template noblank line`), as the stock parser reads it.
- **The template language** (TH5d1, owner decision Q157) shows beside New: karaOK or Aegisub 3.2.2, the Document's mode as the templater picks it (TH5c-karaok-mode). Choosing one writes `Karaoke Templater: karaOK` or `Karaoke Templater: Aegisub 3.2.2` in `[Script Info]` as one undo step, "Set template mode", and the preview runs again. Its tooltip says whether the file sets the mode or its templates' syntax picks it. A Document made with File › New is in karaOK mode until a mode line or a template or code Line decides (TH5d3-new-documents-karaok); a file opened without either is in 3.2.2 mode. In karaOK mode, New inserts the karaOK form of what it lists ("Template for each line" writes `template lsyl`, "Template before each line" writes `template line`) and the Effect picker shows and writes karaOK's words (TH5d3-editor-follows-mode).
- **New** inserts a template or code Line of the stock kinds (furigana ones included) above the first karaoke Line, with that Line's Style, as one undo step. Its "From a preset" (KE3) inserts one of the built-in [karaoke effect presets](karaoke-presets.md) there the same way: all its template and code Lines, in order and with their Layers, as one undo step, the first current. They are ordinary template Lines from then on, previewed and edited like the user's own.
- **Apply** runs "Apply karaoke template" as the Automation menu does: the same macro, undo step, report and log. Its arrow offers "Apply to the selected Lines" (KE1's regenerate-selected), on only when the selection holds a karaoke Line or a Line marked as generated from one; a file's unmarked Lines are not. A shortcut for either is the macro's own, set in the Automation panel. After Apply the footer says how many Lines it wrote from how many karaoke Lines and in what time, until an Undo or another edit changes the Document.
- A Line's fields (template text, Effect, Style, Actor values, the preview's text, the templater's messages) show as plain text, never markup: an `<img>` in a file fetches nothing.
- At its minimum width (360 px) the fields stack and the preview folds into one row that unfolds on a click.

The reduced snapshot leaves out the karaoke Lines before the current one, so state a code Line carries from Line to Line (a counter, a `remember`) starts afresh in the preview. Apply runs every Line and is unaffected.

## ln.kara

HikariSub ships `ln.kara`, karaOK's template library, by logarithm with petzku, as `Automation/automation/Include/ln/kara.lua` (TH5d2, owner decisions Q164 and Q165). It comes from [lost-logarithm/karaOK](https://github.com/lost-logarithm/karaOK) at commit `f749c4fa64e7`. Upstream states no licence; HikariSub's changes, marked `HikariSub (TH5d2)` in the file, are under the MIT licence. About › Credits credits logarithm and petzku. The TH5d2-ln-kara rows above list what HikariSub fixes.

Load it from a `code once` Line:

```
ln = _G.require "ln.kara"; ln.init(tenv)
```

It reads only `orgline`, `line` and the current unit (`syl`, then `char`, then `word`), so it works in both template languages. `ln.tag.kanjipos` is the exception: it needs karaOK mode.

`ln.init(tenv)` adds these shorthands to the template environment and clears the library's random values and lead-in cache:

| Shorthand | Same as |
| --- | --- |
| `st(p)`, `sd(p)` | `ln.syltime(p)`, `ln.syldur(p)` |
| `gc(n)`, `ga(n)` | `ln.line.c(n)`, `ln.line.a(n)` |
| `rgb`, `hsl`, `hsy` | `ln.color.byRGB`, `ln.color.byHSL`, `ln.color.lumaHSL` |
| `rnd`, `rset`, `clamp`, `fl` | `ln.math.random`, `ln.randomize`, `ln.math.clamp`, `math.floor` |
| `set(name, value)` | sets a template variable and gives `""` |

| Function | Gives |
| --- | --- |
| `ln.syltime(p)` | The unit's start plus `p` of its duration, from the Line's start (ms). `p` defaults to 0. |
| `ln.syldur(p)` | `p` of the unit's duration (ms). `p` defaults to 1. |
| `ln.randomize(name, min, max, override)` | Sets the template variable `name` to a random whole number from `min` to `max`, unless it is already set and `override` is false. Gives `""`. |
| `ln.line.tag(tags, defaults)` | The first of each tag named in `tags` (a name or a list of names, without `\`) in the karaoke Line, joined. `defaults` is a string for one tag, or a table by name or by position. |
| `ln.line.tags(tags, defaults, as_list)` | Every one of each named tag, joined, or as a list when `as_list` is true. |
| `ln.line.c(n)`, `ln.line.a(n)` | The karaoke Line's `\Nc` or `\Na`, or else its Style's colour or alpha N. |
| `ln.line.style_c(n)`, `ln.line.style_a(n)` | The Style's colour or alpha N. |
| `ln.line.buffers(min_start, min_end, min_fad_in, min_fad_out)` | Lead-in and lead-out (ms): the Line's `\fad`, or the length of an empty first and last syllable, at least the minimums given. |
| `ln.line.len_stripped()`, `ln.line.len()`, `ln.line.len_raw()` | The character count of the karaoke Line's text without tags, with tags, and as written. |
| `ln.syl.tag`, `ln.syl.tags`, `ln.syl.c`, `ln.syl.a`, `ln.syl.len_stripped`, `ln.syl.len` | As the `ln.line` ones, on the current unit's text. |
| `ln.tag.pos(an, anchor, dx, dy, once)` | `\anN\pos(x,y)` at the unit's anchor point (the Line's outside a unit), moved by `dx`, `dy`. `an` and `anchor` default to the Style's alignment. With `once`, only the first call per generated Line writes it. A table with `alignment`, `anchorpoint`, `offset_x`, `offset_y` and `line_kara_mode` works too. |
| `ln.tag.move(x0, y0, x1, y1, t0, t1, an, anchor, once)` | `\anN\move(...)` from offsets to the anchor point, with `t0`, `t1` when both are given. Left-out offsets are 0. A table with `x0`/`x_start`, `y0`, `x1`, `y1`, `t0`/`time_start`, `t1`, `alignment`, `anchorpoint` and `lsyl_mode` works too. |
| `ln.tag.kanjipos(dx, dy, once)` | Vertical placement for kanji: `\fn@font\frz-90\an5\pos(x,y)`. karaOK mode only. |
| `ln.tag.t(t0, t1, accel, tags)` | `\t(...)` with whole-millisecond times; `ln.tag.t(tags)`, `ln.tag.t(accel, tags)` and `ln.tag.t(t0, t1, tags)` work too. |
| `ln.tag.parse_transform(s)`, `ln.tag.parse_move(s)` | A `\t` or `\move` read into a table. |
| `ln.tag.mod_transform(s, t0, t1, accel, tags)` | `s`'s `\t` with each part changed: `"+N"`, `"-N"` or `"=N"`; for its tags `"+tags"`, `"-tags"` or `"=tags"`. |
| `ln.wave.new(form, wavelength, amplitude, phase)` | A wave: `sine`, `square`, `triangle`, `sawtooth` or `noise`. `addWave`, `addFunction`, `addConstant`, `clear`, `setWave`, `setFunction`, `setConstant` and `getValue(t)` build and read it. `noise` reseeds Lua's random numbers. |
| `ln.wave.transform(wave, tags, t0, t1, delay, framestep)` | `\t` steps every `framestep` frames, at 23.976 fps, that follow the wave on `tags`. |
| `ln.color.byRGB(r, g, b)`, `ln.color.byHSL(h, s, l)`, `ln.color.lumaHSL(h, s, y)` | `&HBBGGRR&` from 0–255 values; `lumaHSL` keeps Rec. 709 luma. `ln.color.rgb.get/add` and `ln.color.hsl.get/add` read a colour and add to it. |
| `ln.math.clamp`, `modloop`, `modbounce`, `log(base, n)` | As named. |
| `ln.math.sgn(n)` | -1 for a negative `n`, else 1. |
| `ln.math.round(n, places)`, `ln.math.round_fast(n)` | Round half to even. |
| `ln.math.random()`, `random(max)`, `random(min, max)` | A random number to 4 decimals, from 0 to 1, to `max`, or from `min` to `max`. |
| `ln.math.lerp(t, a, b)`, `ln.math.xerp(t, a, b, accel)` | Between two numbers, `&H…&` colours or alphas; `xerp` raises `t` to `accel` first. |
| `ln.shapes.shift(drawing, x, y)` | The drawing moved. |
| `ln.shapes.rectangle`, `roundedRectangle`, `circle(r, segments)`, `triangle`, `polygon(sides, r)`, `gear` | Drawings for `\p1`; negative `segments` reverse a circle's winding. |
| `ln.shapes.star`, `heart`, `twinkle1`–`3`, `snowflake`, `stolengleam` | Ready-made drawings. |
| `ln.debug.prog(p)` | Sets the progress bar to `p` (0–1). Gives `""`. |
