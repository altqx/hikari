# Karaoke templater

KE1 ([#283](https://github.com/altqx/hikari/issues/283)) improves the bundled karaoke templater, `Automation/automation/Autoload/kara-templater.lua`. Kara Effector (`Automation/automation/Include/effector-auto4.lua`, reached as `ke4` from templates) ships unchanged under its own licence.

## The behaviour matched

The templater HikariSub shipped before KE1 is Karaoke Templater 2.1.7 as Aegisub v3.2.2 released it. It differs from that release only in CRLF line ends, a byte order mark and trailing whitespace. KE1 keeps 3.2.2's output: a template with an error is logged and the run carries on, leaving the failed `!expression!` in the generated Line. Aegisub 3.3 and later (upstream commit `7a8fda007d`) cancel the whole run at the first error instead; HikariSub does not.

The lock is a corpus, `tests/backends/karaoke-corpus/`:

- the templates of Aegisub's templater documentation (both tutorials, the modifiers, the code environment, furigana, inline-fx);
- Karaoke Mugen's minimal template;
- Kara Effector's `ke4` functions;
- template errors, including TT#141's 3.2-era template;
- a template after the first 50 Lines.

`karaoke_templater_tests` applies the templates to each Document with the bundled script and saves it. The saved bytes must equal the committed golden. The unchanged 3.2.2 script (`kara-templater-aegisub-3.2.2.lua` in the corpus) must give the same golden in the same host. The corpus README lists the sources.

## What KE1 adds

Each change is a named departure in the [compatibility decisions](compatibility-decisions.md).

| Departure | What changes |
| --- | --- |
| KE1-no-50-line-gate | "Apply karaoke template" is offered when any Line is a template Line. 2.1.7 looked at the first 50 dialogue lines only. As in 2.1.7, code Lines alone do not offer it: without a template they generate nothing. |
| KE1-template-problems | Each problem with a template or code Line ends with `Template Line: N` or `Code Line: N`, N being the Line's number in the Grid. A named line template made of several Lines names the Line whose text the variable or expression is in. A run that had problems selects those Lines, the first active, and its progress window stays open so the log can be read. |
| KE1-regenerate-selected | "Apply karaoke template to selected lines" replaces only the selected karaoke Lines' generated Lines, as one undo step. |
| KE1-template-timing | A run reports its time and, per template or code Line, the time it took and the Lines it generated. The report goes to the status bar and the log window. |
| KE1-render-cost-overlay | "Show / hide render cost" in the Video menu and the video's context menu (GLOBAL_VIDEO_RENDER_COST) draws over the video what the shown frame's subtitles cost to draw. |

The generated Lines are byte for byte 3.2.2's in every case. The macro returns a selection only when there were problems; otherwise the selection stays as it was.

### Regenerating the selected Lines

The templater marks each generated Line with the karaoke Line it came from. The mark is the source Line's id in memory (`LineRecord::generatedFrom`); it is never saved. Selecting a karaoke Line, or one of its generated Lines, and running "Apply karaoke template to selected lines" does the following:

1. Run the templates over every karaoke Line up to the last selected one, in order, as a whole run does. Templates can carry state from one Line to the next (code-line counters, `remember`/`recall`), so a selected Line gets what a whole run would give it.
2. Keep only the selected Lines' output. The other Lines are not changed and their problems are not reported.
3. Replace each selected Line's old generated Lines with its new ones, where the first old one was. A Line that had none gets them at the end.
4. Report problems with the numbers the Lines have after the run.

A Document opened from a file has no marks. Its generated Lines are then matched against what the templates give the other unmarked karaoke Lines now, compared as the saved file would read them back, with a TLMode Line's original text as well as its translation. A run generates in karaoke Line order, so identical generated Lines go to the karaoke Lines in file order: each takes the first one still free. The generated Lines left unmatched are the selected Lines' output, but only where they can be. Each must have the style of a selected Line or of one of its generated Lines now. When the generated Lines are in karaoke Line order, it must also lie between those of the karaoke Lines around that Line. Otherwise it may be left over from a karaoke Line that now generates fewer Lines, and the run is cancelled. Lines left over from a karaoke Line of the same style, next to a selected one, cannot be told apart. If another karaoke Line's output is not there as the templates give it now, the run is cancelled with an explanation and nothing changes. That happens when the template or those Lines changed, or when the template uses random values. A whole run marks every generated Line, and after that selected Lines can be regenerated on their own.

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
- `issues` (KE2) lists each problem once, however many syllables or Lines ran into it, up to 100: `{"line":3,"kind":"template","message":"Unknown variable name: …","start":10,"length":10}`. `message` is 2.1.7's, without the Line's number. `start` and `length` give the variable or expression in that Line's own text, in bytes from 1; `start` is 0 for a problem without a place there, such as a code Line's or an unknown modifier. A named line template's problem is placed in the Line it is in.

Times come from a steady clock in the helper, so they include waits for the host's text measurements. A selected-Lines run counts the selected Lines' work only.

The app shows the summary in the status bar for 10 seconds. It writes the templates to the log window, slowest first, without popping the window up.

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

## For KE2: one Line's preview

KE2's template editor previews a template on one Line without changing the Document:

1. Take a snapshot with only that Line selected.
2. Run "Apply karaoke template to selected lines" on it.
3. Read the Lines with `generatedFrom` equal to that Line's id from the helper's last result, without applying it.

The cost follows the Line's position: the templates run over the karaoke Lines before it. A reduced snapshot (the templates and that Line) gives a fast preview, at the price of state carried from earlier Lines. The report's per-template times are the preview's cost.
