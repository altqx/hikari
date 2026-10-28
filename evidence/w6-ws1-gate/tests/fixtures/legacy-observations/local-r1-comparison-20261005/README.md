# Legacy observations: R1 subtitle comparison (local probe, 2026-10-05)

Captured by the `legacy_comparison_capture` probe ([`comparison_capture.cpp`](../../../../tools/legacy-capture/comparison_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](../../../../tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/comparison-cases.txt`](../../../../tools/legacy-capture/inputs/comparison-cases.txt). The probe compiles the legacy `SubsGrid::SubsComparison` and `SubsGrid::CompareTexts` (`HikariSub/SubsGridBase.cpp:1737-1884` at `20d647c4`), `compareData` (`SubsGrid.h:67-78`) and the Line's `StoreTextHelper` and `Visibility` (`SubsDialogue.h:122-252`, `300-321`) unchanged, copied out of their files by [`extract_functions.py`](../../../../tools/legacy-capture/extract_functions.py), with the legacy `SubsTime.cpp` and `LogHandler.h`. The grid, its file and the option are stood in for by [`comparison/standins.h`](../../../../tools/legacy-capture/comparison/standins.h): the grid's `file`, `Comparison`, `hasTLMode` and the statics `CG1`, `CG2`, `compareStyles`; the file's dialogues and selection set with legacy's `GetCount`, `GetDialogue` and `IsSelected`; `Options.GetInt(SUBS_COMPARISON_TYPE)`.

Built locally with GCC 16.2.1 and the distribution's wxWidgets 3.2.11 (wxGTK, `wx-config --cxxflags base,core`). Run:

```
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<checkout at 20d647c4> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_comparison_capture
<dir>/legacy_comparison_capture < tools/legacy-capture/inputs/comparison-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case (14 cases, 207 runs, 4239 table rows). Each run is a SUBS_COMPARISON_TYPE with a compareStyles list (and the two grids' hasTLMode); it records both grids' Comparison tables, a row being `[secondComparedLine, differences, lineCompare...]`, whether SubsComparison left both files as they were (`unchanged`: every Line's Text, TextTl, Style, times, visibility and the selection) and whether a second SubsComparison on the same grids (the tables cleared and refilled, as an edit's SetModified runs it) gave the same tables (`repeatable`). Two runs again give the same file. sha256 `69f4d53d…bc71b1`.

Every run is made twice by the same compiled code: `linux` gives the texts as wxGTK's wxString stores them (a wchar_t per code point, the legacy Linux build), `windows` as wxMSW's wxString stores them (a wchar_t per UTF-16 code unit: wchar_t is 16 bits there, so a character outside the BMP is its two surrogates). `windows` is `"same"` when the tables are those of `linux`.

The cases:

- `matrix`: 7 Lines against 11 that differ in times (an End 500 ms later, Lines 10 ms apart), style, visibility (NOT_VISIBLE, VISIBLE, VISIBLE_BLOCK on either side) and selection; every SUBS_COMPARISON_TYPE 0-31 with no chosen styles and with `Sign`, and the types 0, 1, 2, 4, 8, 16 and 31 with `Default,Sign`, `Other`, `sign`, `Sign,Sign`, `Missing` and `Default,Missing`. `matrix-swapped` is the same pair with CG1 and CG2 exchanged.
- `last-j`, `times`, `style-names`, `visibility`, `selections`: one criterion each; `empty-first`, `empty-second`, `empty-both`.
- `translation`: hasTLMode on neither grid, CG1, CG2 and both, with Lines whose translation is empty, equal to the other text, or different.
- `texts`: 25 text pairs with no criterion (equal, contained, empty, reordered, repeated letters, accents and a combining mark, CJK, tags and `\N`, characters outside the BMP); `rand-ascii` and `rand-astral`: 200 random pairs each over `ab {` and over `a`, U+1F600, U+1F601 and a space (drawn once, fixed in the case file).

## Compared with the rewrite

`LegacyComparisonCapture.ReplaysTheLegacyObservations` (`hikari_application_subtitle_comparison_tests`) builds each case's two Documents (times through the ASS loader, the style, text, translation and visibility as the probe set them, the selection as the selected Line ids) and runs every run through `compareSubtitles` with the run's SUBS_COMPARISON_TYPE, compareStyles and translation modes, once counting UTF-16 units and once counting code points. Both tables are legacy's `windows` tables in every run of the first and its `linux` tables in every run of the second, on whichever platform the test runs: the pairs (secondComparedLine, -1 for none), `differences`, the leading 1 and every range; the Grid's colour state per row follows (`mismatch()` where the legacy array is not empty, `match()` where `differences` is false; SubsGridWindow.cpp:419-426). A second call gives the same tables, and the Documents are unchanged. No approved departure changes these results: R1-stale-table and R1-session-off concern which grids keep a table, not what SubsComparison computes.

The capture confirms, as the R1 tests had worked out by hand:

- The ChosenStyles bit (4) is never read: every type gives the pairs of the type without it. The list alone decides: with `Sign`, only Sign Lines pair, each with a Sign Line; `Sign,Sign` pairs as `Sign`; a list naming only a style neither file uses (`Missing`) pairs nothing; style names compare exactly (`sign`, `default`, `Song Top ` with a trailing space pair with nothing).
- A Line without a partner leaves the search where it was (lastJ): in `last-j` by times, `c` finds no partner after `b` took the second's last-but-one Line.
- Times compare Start and End in milliseconds (10 ms apart is no match).
- With "Compare by visible lines" only NOT_VISIBLE is passed over; VISIBLE_BLOCK counts as visible.
- Selections need both Lines selected; translation mode on a grid compares that grid's translations where they are not empty.
- Comparing writes neither file, and comparing again gives the same tables.

### Where the two legacy builds differ

Only in the cases with characters outside the BMP (`texts` rows 18-23 and 192 of the 200 `rand-astral` pairs) do `linux` and `windows` differ. The legacy Linux build counts each such character as one position, the Windows build as two, and it compares surrogates on their own. For example, U+1F600 against U+1F601 gives `[1,0,0]` on Linux and `[1,1,1]` on Windows: the high surrogate is shared and only the low one is outlined. Under R5-per-platform the rewrite counts as each build did: code points on Linux and UTF-16 code units on Windows (`kLegacyTextUnits`), and the Grid outlines a range of the shown text in the same units.

## Not observed here

These are not observed: the running legacy app (no screenshots of its Grid, so the colours are checked only through the per-row state that selects them); the tab menu (Notebook.cpp), RemoveComparison and GetCommonStyles; and a Windows-built probe (the `windows` tables come from the Linux-built legacy code given UTF-16 units, as wxMSW's wxString holds them).
