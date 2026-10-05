# Legacy observations: V6 video timing and selection (local probe, 2026-10-05)

Captured by the `legacy_video_timing_capture` probe ([`video_timing_capture.cpp`](../../../../tools/legacy-capture/video_timing_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](../../../../tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/video-timing-cases.txt`](../../../../tools/legacy-capture/inputs/video-timing-cases.txt). The probe links the legacy `HikariSub/Timebase.cpp` and `SubsTime.cpp` at `20d647c4` unchanged, and [`extract_functions.py`](../../../../tools/legacy-capture/extract_functions.py) copies these definitions out of their files unchanged, each behind a `#line` naming its place:

- `VideoBox::GetFrameTime` and `GetVideoListsOptions` (`VideoBox.cpp`);
- `SubsGrid::SetStartTime`, `SetEndTime`, `SelectVisible`, `ShowEditOnVideo` and `GetKeyFromPosition` (`SubsGridBase.cpp`);
- `SubsGrid::SelVideoLine` and `SetVideoLineTime` (`SubsGridWindow.cpp`);
- `EditBox::SetLine` (`EditBox.cpp`);
- `HikariSubFrame::OnAudioSnap` (`HikariSubFrame.cpp`).

They run against the stand-ins in [`tools/legacy-capture/video_timing/`](../../../../tools/legacy-capture/video_timing): the Lines and the selection (`SubsFile`), the edit box's Line and time fields, the video (its state, `Tell`, `GetDuration`, the Timebase and the toolbar's two lists) and the audio box. What the GUI is asked to do is recorded: `Seek <ms> <startTime>`, `Pause`, `Play`, `PlayLine <start> <end>`, `OnPlaySelection`, `Send <history>`, `SetModified <history>`, and the redraws. Two call sites are single expressions inside functions too large to copy and are transcribed with their places: GLOBAL_SET_START_TIME / _END_TIME (`HikariSubFrame.cpp:750-760`) and the Grid press that seeks (`SubsGridWindow.cpp:1647-1648`).

The timelines are FFMS2's truncated frame starts: CFR 24000/1001 (`(i * 1001) / 24` ms, 72 frames, as the `cfr.mkv` media fixture indexes) and a VFR stretch at 24, 60 and 30 fps (`0, 42, 83, 125, 142, 158, 175, 192, 225, 258, 292`, the legacy `TimebaseTests.cpp` timeline).

Built locally with GCC 16.2.1 and the distribution's wxWidgets 3.2.11 (wxGTK, `wx-config --cxxflags base,core`). Run:

```
git archive 20d647c4 HikariSub cmake | tar -x -C <legacy>
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<legacy> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_video_timing_capture
<dir>/legacy_video_timing_capture < tools/legacy-capture/inputs/video-timing-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case; each op ran on a fresh copy of the case and records the Lines' times, the selection (keys), the edit box's Line (`current`) and time fields (`edit`), `GetFrameTime`'s start and end for `frametime`, and the calls. sha256 `aaff657070e4b68cb9ead549860c9d1fe13ecf1b235e900c4263aa798e5bbad8` (44 cases, 1197 ops; the last three cases, `insert-times-unchanged`, `select-from-video-all-hidden` and `snap-end-blocked-start`, were added on 2026-10-05 as old evidence for the V6 departures, and the earlier 41 cases' lines are unchanged).

## Compared with the rewrite

`VideoTimingCapture.ReplaysTheLegacyObservations` (`hikari_application_video_timing_tests`) replays every op through `hikari/application/video_timing.h` and compares the times, selections and the video and audio calls (`VideoFollow`). All are the same apart from the ops of four approved departures ([compatibility decisions](../../../../docs/qt/compatibility-decisions.md), user 2026-10-05), where the replay needs legacy's result with exactly that change and the legacy observation stays as the old evidence:

- V6-snap-next-line: `snap-first-line-mode-1` `snap end 1` snaps to the next Line's Start 950;
- V6-snap-both-boundaries: `snap-end-blocked-start` `snap end 1` snaps to the other Line's End 1990;
- V6-select-shown-fallback: `select-from-video-first-hidden` `at 200 selvideo` selects row 1, the first shown, and `select-from-video-all-hidden` selects nothing;
- V6-insert-time-no-op: `insert-times-unchanged` `setstart 0` and `setend 0` record no step.

The capture also confirms these legacy quirks, kept unless marked:

- Insert start/end time from video uses the frame's midpoint representative (StartTimeFor / EndTimeFor of the frame shown at Tell), then the offset, then ZEROIT: at 1001 ms (frame 24) the start is 980 and the end 1020; ZEROIT truncates towards zero, so a negative time becomes 0 only through SubsTime::NewTime.
- Every selected Line gets the time, hidden ones included; a start after the End takes the End along (`End < stime`), an end before the Start takes the Start. A step is recorded even when no time changes (not kept: V6-insert-time-no-op).
- Select line at current video position ignores Comments and Lines with neither text nor translation; [Start, End] includes both ends; between Lines it takes the later one only when strictly nearer; its fallbacks are Document row 0, even a hidden one (not kept: V6-select-shown-fallback).
- Select all lines visible on video reads `Start - 5 <= Tell < End - 5`, skips Comments and unparsed records always and hidden Lines unless "Ignore filtering in some actions" is on, and reads the active Line as the edit box holds it.
- Change start/end time to nearest keyframe needs the audio box; with AUDIO_INACTIVE_LINES_DISPLAY_MODE 1 it never reaches the next Line (the loop stops before `shadeTo`; not kept: V6-snap-next-line); a boundary that fails the order check skips that Line's other boundary too (`continue`; not kept: V6-snap-both-boundaries).
- EditBox::SetLine seeks only for "Every line change" and only when the play-after choice does not play the video; it pauses a playing video first. The play-after choice plays only for a Grid click or SubsGrid::NextLine (`autoPlay`), also on the same Line (the `goto done` path), and the video choices end at the frame before the Line's end (or the next shown Line's start, when later).
- SetVideoLineTime plays then pauses a Stopped video, pauses a playing one unless the choice is "Double-clicking a line" (then the video plays on from there), seeks to the end for the End column (not in TMPlayer), and a Ctrl double click a second earlier (not below 0); the audio box then shows the Line's start or end (AudioDisplay::Update).
- ShowEditOnVideo seeks to the active Line's start for choices 2-5 while paused, 3 and 5 also while playing (the video plays on), and only without a visual tool past the crosshair.
