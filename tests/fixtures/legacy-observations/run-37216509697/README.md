# Legacy observations: run 37216509697 (F1 and F3 ui cases)

Captured by [`ui_capture.py`](../../../../tools/legacy-capture/ui_capture.py) (the plan's `ui` section, run by `drive.py`) in [legacy-capture](../../../../.github/workflows/legacy-capture.yml) [run 37216509697](https://github.com/altqx/hikari/actions/runs/37216509697) (workflow_dispatch on `capture-test`, ubuntu-24.04 runner, Xvfb 1280x800, openbox), from the legacy Linux app built there at `20d647c4`, `LANG=C.UTF-8` unless a case sets it. Each case runs a fresh copy of the app with its own `Config.txt` (FIND_REPLACE_OPTIONS, FIND_REPLACE_STYLES, DICTIONARY_LANGUAGE), the case's document from [`tools/legacy-capture/inputs`](../../../../tools/legacy-capture/inputs) and the `en_TEST` fixture dictionary ([`tests/fixtures/spelling`](../../spelling)). States are read by the `Hikari state dump` macro ([`state-dump.lua`](../../../../tools/legacy-capture/automation/state-dump.lua)): the Lines, the selected rows, the active row and the Line editor's selection (`aegisub.gui.get_selection`, 1-based, end exclusive). `observations.json` is the complete record. `screens/` keeps the message boxes, the Search results lists, the field and translation-mode screenshots and the Grid's spelling marks; full-screen final shots and the per-word Spellchecker crops were not kept.

Every step of all 40 cases is identical to the reviewed local reproduction (the `hikari-legacy-env` container with run 37192275602's package, sha256 `e8d901b3…`): the same dumps, message-box titles and Spellchecker walks. The two hang cases end as ui_capture.py's memory watchdog kills the app (F1-find-all-end-of-text at 1045 MB after 12.4 s, F1-replace-all-empty-match at 1605 MB after 31.8 s): legacy grows its memory without end there, which took the runner in runs 37211617526 and 37213840510.

## F1 Find and replace: compared with the rewrite

`FindReplaceCapture.ReplaysTheLegacyCaptures` replays every F1 case through `FindReplace` and compares each dump and each message box's title; [`compare_ui.py`](../../../../tools/legacy-capture/compare_ui.py) prints the side-by-side report from its artifact. Of 71 dumps, 68 are the same. The other 3 are approved departures (R3-hang-crash-loss), and the captures confirm each premise:

| Case | Legacy | Rewrite |
| --- | --- | --- |
| F1-replace-next-stale (`cat dog cat x`, cat to c) | the third Replace next, after "Reached end" No, replaced the stale match again: `c dog c` | `c dog c x` (F1-stale-replace) |
| F1-find-all-end-of-text (plain End of text, Find all) | the app stops answering | results `Line 1 [6,11)`, `Line 4 [8,13)` (F1-end-of-text) |
| F1-replace-all-empty-match (`x*` to `-` on `ab`, regex) | the app stops answering, with no message | `-ab`, "Replaced 1 times." (F1-empty-match) |

Find next, by case. Rows count events, comments included; the selection is the legacy editor's (0-based, end exclusive):

- Text, All lines, `HELLO`: (0, 0-5), (0, 6-11), (2, 5-10; the Comment on row 1 skipped), (3, 8-13), "Reached end. Search from the beginning?", No, then (0, 0-5).
- From selected, row 2, `hello`: (2, 5-10), (3, 8-13), Reached end, Yes: (3, 8-13) again (the first selected Line is now row 3), then "Could not find the specified phrase "hello"."
- Beginning of text, plain: (0, 0-5), (2, 5-10), (3, 8-13), one per Line, matches anywhere; with a regular expression: (0, 0-5), then Reached end.
- End of text, `HELLO`: (0, 6-11), (3, 8-13) (row 2 ends with a tag).
- Regex `^l` on `lll`: (0, 0-1), (0, 1-2), (0, 2-3): `^` matches again on the rest of the text.
- Skip tags, `b`, from row 2: Reached end; Skip text: (2, 12-13).
- Selected lines with style `Sign`, row 0 selected: (0, 0-5), (0, 6-11), then row 2 active with row 0 still the only selected row (styles OR selected).
- Missing styles `Sign,Nope,Gone`: the question "Styles named "Nope, Gone," do not exist…" (Remove nonexistent styles, Remove styles, Ignore, Cancel); Cancel searches nothing. No style found (`Nope`): "None of the selected styles exist…" (Remove styles, Cancel); Remove styles, then (0, 0-5).
- Regex `(`: "Invalid regular expression '(': missing closing parenthesis" (wxLogError, "Hikarisub Error"), nothing found.
- Regex `(?<=hel)LO`: (0, 3-5). Regex `łó.ź` on `żółw ŁÓDŹ`: (0, 5-9) (wxRE_ICASE folds every letter).
- Plain `łódź`, Match case off, on `żółw ŁÓDŹ`: Reached end, under `LANG=C.UTF-8` and `LANG=C` alike. With the English interface legacy never sets the C library's locale (wxGTK 3.3 calls `gtk_disable_setlocale`; `hikarisubApp::OnInit` only creates a `wxLocale` for another language), so `wxString::Lower` (towlower) folds A-Z only. The rewrite folds every letter here by the approved departure U1-unicode-case (the replay keeps FindReplace's own A-Z fold to compare with legacy).
- Styles field `SIGN`: row 2 (the Comment skipped); Actor `n`: row 0 twice (`Anna`), then Reached end; Effect `X`: row 2 (`fx`); the editor's Text selection stays empty.
- Translation mode (`aaa orig` / `translated aa`), `aa`: the original's 0-2 in the original editor (screenshot), then the translation's 11-13; Replace next with `b`: `translated b`, then Reached end.
- Replace next `hello` to `bye`: `bye hello` with the next match (0, 4-9), `bye bye` with (2, 5-10); after selecting row 0, Replace next searches again: Reached end. Regex `(h)ello` to `\1i`: `Hi hello`, then `Hi hi`.

Replace all ("Find and Replace" box): `hello` to `bye` "Replaced 4 times." (`bye bye`, Comment untouched, `{\i1}bye{\b1}`, `the end bye`), `zzz` "Replaced 0 times."; styles `Sign` AND selected rows 0 and 2, comments included: 1, only row 2; from selected row 1 with comments: 3 (rows 1-3); regex with End of text: 0; plain Beginning of text `the`: 1 (`X end hello`); empty search with End of text and `!`: 3 (the Comment skipped); regex `(l)` to `L` on row 0: 2 (`HeLlo heLlo`, the pattern-length step).

Find all in current subtitles (screenshots): `hello` lists the header and Line 1 twice, Line 3, Line 4, and Replace checked `Bye!` changes all four; `l` with comments lists 10 matches over Lines 1-4 and Replace checked `LL` gives `HeLLLLo heLLLLo` (offsets shifted by earlier replacements, the Comment included); translation mode `aa` lists `aaa orig` at 0 and 1 and `translated aa`; Actor `bo` lists "Line 3: Bob  ->  {\i1}hello{\b1}" and Replace checked logs "Line 3 cannot be replaced,\ncause it was edited." in the Log window; regex `l(?=o)|d` lists five matches and Replace checked `Y` changes only `the enY hello`. The rewrite's results are the same (artifact `results`).

## F3 Spell checker: compared with the rewrite

`LegacySpellingCapture.WindowWalkMatchesTheLinuxCapture` runs CheckText with Hunspell over `en_TEST` and boost::locale's segmentation on the same documents and matches the whole walk:

- `spell-walk.ass`, from row 0 by Ignore (0-based inclusive): `wrold` 6-10 and `times` 16-20; `wo{\i1}rd` 0-8 (one word across the tag) and `ok`; drawing skipped, `gud` 23-25; `\N`/`\h`/`\n` split `good\Nbda\hgood\nxx` into `bda` 6-8 and `xx` 17-18; `go\iod` is `go` 0-1 and `iod` 3-5; `dont` and `4ever` misspelled, `don't`, `3.14` and `abc123` not checked; `Zażółć`, `世界` (one word) and `gęślą`; `a`, `𐌰𐌱` and `b` (the Linux build counts code points: `𐌰𐌱` is 2-3; the rewrite's UTF-16 offsets are compared as code points); row 8 `word` 0-8 and `gud` 17-19 (stray braces are not words); row 9 (`Hello world worlds text texts The the`) has none; `Teh` and lowercase `hello` (only `Hello` is in the dictionary; `HELLO WORLD` are accepted); the Comment `wrold comment` is checked; `it's`, `l'amour`, `quoted` (the quotes outside), `text's` misspelled and `don't.` not; `wrold`, `12px` and `2nd` on row 13, the tags' `fnArial` skipped; row 14's punctuation splits correctly-spelled words; `good_good` is one word, `e-mail` is `e` and `mail`, `good—good` and `good-good` are correct; `gud` after a `{comment text}` block; `Teh` before a trailing tag. Then "No spelling errors were found".
- `spell-walk.srt`: `gud` 3-5 inside `<i>…</i>`, `x` 12 (braces are text in SRT), `wrold` 15-19; `Hello|wrold\Ngood`: `wrold` 6-10 (`|` is not a wrap mark in SRT).
- The Grid's marks (`screens/F3-window-walk-01-grid-marks.png`) show the same words and the bracket errors (`}` and `{` on row 8).

## Not observed here

The Actor field's selection after an Actor match (the dialog covers it), the Windows build, Find in all open subtitles, Replace in all open subtitles, the files tab, the spell checker's Replace, Replace all, Add and the editor's context menu.
