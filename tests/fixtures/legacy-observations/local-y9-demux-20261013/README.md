# Legacy observations: Y9 MKV extraction (local probe, 2026-10-13)

Captured by the `legacy_demux_capture` probe ([`demux_capture.cpp`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/demux_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/demux-cases.txt`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/inputs/demux-cases.txt). The probe compiles the legacy `HikariSub/Demux.cpp` at `20d647c4` unchanged and links FFMS2 as the legacy Linux build does (root `CMakeLists.txt`): the distribution's `libffms2` with `Thirdparty/Build/FFMS2/indexing_additional.cpp` compiled against the ffms2 fork's headers (`Thirdparty/ffms2` at `45d5f721`). The grid, the track chooser, the progress dialog and the message box are the stand-ins in [`tools/legacy-capture/demux/standins.h`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/demux/standins.h); `SubsFile::AddSInfo`, `GetSInfo` and `GetSInfoP` are copied unchanged, so the Script Info list is legacy's own. The Style and Line stand-ins also read their line with legacy's own `Styles::parseStyle` and `Dialogue::SetRaw` (the `AssColor`, `Styles` and `Dialogue` classes and those definitions copied unchanged out of `styles.h`, `styles.cpp`, `SubsDialogue.h` and `SubsDialogue.cpp`, [`demux/legacy_parse.h`](https://github.com/altqx/hikari/blob/350c1152a3560230913ce89d163ebd52766cc5ce/tools/legacy-capture/demux/legacy_parse.h)) and record what `GetRaw` writes back.

The files are [`tests/fixtures/matroska-muxed`](../../../matroska-muxed): written by mkvmerge v102.0 (MKVToolNix) from the sources beside them, with a 16x16 FFV1 video track from FFmpeg where noted ([`make-fixtures.sh`](../../../matroska-muxed/make-fixtures.sh); `--deterministic`, so reruns are byte-identical).

| File | Tracks |
|---|---|
| `ass-sync7.mkv` | one ASS track and nothing else, delayed 7 ms (`--sync 0:7`), Polish, named "Dialogi" |
| `ass-stretch.mkv` | video, ASS stretched by 1001/1000 |
| `multi.mkv` | video, ASS (jpn, "Signs"), SSA (eng, "Old SSA"), SubRip (pol, "Napisy"), WebVTT (fre); five attachments: `font/ttf` "a.ttf", `font/otf` "fonts/b.otf", `application/x-truetype-font` "dir\c.TTF", `application/vnd.ms-opentype` "..d.otf", `text/plain` "notes.txt" |
| `webvtt-only.mkv` | video, WebVTT |
| `not-matroska.mkv` | text, not a media file |

Built locally with GCC 16.2.1 against the distribution's wxWidgets 3.2.12 (`wx-config --cxxflags base,core`), ffms2 5.0 and FFmpeg 9.0.2:

```
git archive 20d647c4 HikariSub cmake Thirdparty/Build/FFMS2 | tar -x -C <legacy>
git -C <legacy checkout>/Thirdparty/ffms2 archive --prefix=Thirdparty/ffms2/ 45d5f721 include src | tar -x -C <legacy>
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<legacy> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_demux_capture
<dir>/legacy_demux_capture tests/fixtures/matroska-muxed < tools/legacy-capture/inputs/demux-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case (12). A subtitle case records whether `Demux::Open` succeeded, `GetSubtitles`' result, the grid's `originalFormat`, every packet's callback arguments (`start`, `duration`, `total`, the line's bytes), every call Demux made in order (`ops`: the chooser and its items, the progress dialog's title and each `Progress` value, the track read, `Clearing`, each Style with the layout it is read with and legacy's `GetRaw` of it (`parsed`), each Line's raw text and legacy's `GetRaw` of it (`parsed`), `EndLoad`, messages, the log) and the file's Script Info at the end. A font case records each font's name, whether `SaveFont` wrote it, and its size and FNV-1a hash. Two runs give the same file. sha256 `2c413777…e3ce3`.

## What legacy did

- **Times.** ASS and SSA packets: start and end (start + duration) each get 5 ms and are truncated to centiseconds. The 7 ms delay gives 507 → 0:00:00.51, 1237 → 0:00:01.24, end 1237 + 1980 = 3217 → 0:00:03.22; the stretch gives 1231 → 1.23, 3727173 → 1:02:07.17. SubRip packets keep their milliseconds (`00:00:01,001 --> 00:00:02,500`).
- **Order.** mkvmerge stores the events sorted by start time (it sorts SubRip cues and warns, and orders the ASS events the same way), and Demux adds the Lines in packet order: the ASS file's second event (0:00:00.50) comes first. The ReadOrder field mkvmerge keeps is dropped, so the source order is not restored; two events at the same start stay in ReadOrder.
- **Comments.** mkvmerge keeps Comment events in the codec-private header, not as packets, so legacy adds them before every Dialogue line.
- **SSA.** FFmpeg reads `S_TEXT/SSA` as ASS, so the label says "ass" and Demux treats the track as ASS: its Style is read with the ASS layout (`Styles(next, 1)`), and "YCbCr Matrix: TV.601" is added. mkvmerge writes the SSA "Marked=0" field as an empty Layer field, read as layer 0.
- **Script Info.** The header's keys in order; "YCbCr Matrix: None" becomes "TV.601" in its place.
- **A Line with an empty Style field.** The ASS event `1,0:00:04.00,0:00:05.00,,Actor,0,0,0,,empty style field` reaches Demux as `3,1,,Actor,0,0,0,,empty style field`; with ReadOrder and Layer taken off, the rest already starts with a comma, so Demux adds none and the empty Style field is lost. Legacy reads the line as Style "Actor", Name "0", MarginV 0, Effect "empty style field" and empty text.
- **SSA Style in the ASS layout.** `Style: Default,Arial,20,16777215,65535,65535,-2147483640,-1,0,1,3,0,2,30,30,30,0,0` read with the ASS layout runs out of fields before Alignment: GetRaw gives `Style: Default,Arial,20,&H00FFFFFF,&H0000FFFF,&H0000FFFF,&H80000008,-1,0,-1,-1,0,2,30,30,1,0,0,,,,,`.
- **Labels.** "<stream index> <name> (<language>, <codec>)": "1 Signs (jpn, ass)", "2 Old SSA (eng, ass)", "3 Napisy (pol, srt)". SubRip's codec is "srt"; WebVTT is not offered. A file with one offered track asks nothing.
- **Progress.** One `Progress` per packet. With a video track the subtitle stream's duration is unknown (`total` is INT64_MIN) and every percent is 0; in the file without video it is the track's duration (3724060 ms) and the percents run 0 … 99.
- **Cancel.** The chooser's Cancel reads nothing. The progress dialog's Cancel during the second packet stops the read there; nothing is added, but `originalFormat` is already -1.
- **Failures.** WebVTT alone: "The file does not contain any subtitle tracks." A file FFMS2 cannot index: "Indexing error occurred: Can't open 'not-matroska.mkv'" in the log, for the subtitles and for the fonts alike.
- **Fonts.** The four font MIME types in order, named without their folder ("fonts/b.otf" is "b.otf", "dir\c.TTF" is "c.TTF"), "..d.otf" kept; `text/plain` left out; the bytes as attached.

## Compared with the rewrite

`MatroskaCapture.ReplaysTheLegacyDemuxProbe` and `MatroskaCapture.ReplaysTheLegacyFontList` (`hikari_backends_matroska_capture_tests`) run every case through the real media helper (the rewrite's FFMS2 port, which builds the same `indexing_additional.cpp`, and its FFmpeg): the packets' start, duration, total and bytes; each packet's Line (`matroskaLine`) against legacy's raw text; then the load as the application runs it (`MatroskaSubtitleLoad`): the chooser and its labels, the chooser's and the progress dialog's Cancel, the progress percents, the no-track message and the log line, and the staged Document's Script Info (values and order), Styles (the Document's Style line and its decoded values against legacy's `GetRaw` of the Style it read) and Lines (Comment or Dialogue, layer, start, end, style, name, margins, effect and text against legacy's `GetRaw` of the Line it read; for SubRip, the times and text). The fonts: names, sizes and bytes.

## Not observed here

The running legacy app (the chooser and the progress dialog on screen, `OnMkvSubs`' part after Demux: the format, the file name, the video); a Windows build (the legacy Windows release bundles its own FFmpeg); OGM files and the `text` codec (`S_TEXT/ASCII`), which neither mkvmerge nor FFmpeg writes (the rewrite's byte-written `hikari_mkv_fixture` files cover that codec); a file large enough to watch the progress gauge move (the Y9 progress observation stays a separate item).
