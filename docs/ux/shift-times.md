# Shift times and Timing postprocessor

The Timing dock's Shift / Post processor switch becomes two panels, **Shift times** and **Timing postprocessor**. The owner approved the mockup and its ten recommendations on 2026-11-02 ([#598](https://github.com/altqx/hikari/issues/598#issuecomment-6570140512)). The panels are `src/ui/ShiftTimesPanel.qml` and `src/ui/TimingPostprocessorPanel.qml`. Both run `application::shiftTimes` in `src/application/shift_times.h`, and their previews run `application::planShift`, the same function without the edit. Results are unchanged: the core shift and postprocessor tests are the same as before.

## Where they open

- **Shift times** opens with Subtitles › Shift times… (5030, Ctrl+I). Its dock keeps the id `Timing`, so saved layouts keep its place.
- **Timing postprocessor** opens with Subtitles › Timing postprocessor… (5214, no default key). Its dock id is `Postprocessor` (panel registry 11). It is docked beside the Audio panel and closed in every layout except Timing, where it opens and the Video panel closes. There is no Timing menu yet, so the command sits under Subtitles next to Shift times….
- **Shift times with the last values** (5300) shifts with the remembered values without opening the panel. **Timing postprocessor with the last values** (5215) does the same for the postprocessor. Neither adds an undo step when no line would change.

## Shift times

- **Preset**, then:
  - **Shift by:** Amount · To video · To audio mark. To video needs a video, and To audio mark needs an audio mark. With either, **Move** chooses the active line's Start or End, and the panel shows the time it moves to ("To the video at 0:12:03.45"). The amount field is then labelled **Plus**, because the core adds it to the worked-out amount.
  - **Amount:** a time or a number of frames (Time · Frames).
  - **Direction:** Later · Earlier (legacy's Forward / Backward).
  - **Lines:** All lines · Selected lines · Selected line and below · Starting at or after the selection · Starting at or before the selection · Lines in chosen styles…. The last opens the Choose styles popover; the chosen styles show beside a Choose styles… button.
  - **Times:** Both · Start · End. There is no "Shift only the start times?" question any more.
  - **End times:** Leave as they are · Fix overlaps · Recalculate from text length.
  - **Move tag times too (\move, \t, \fad).**
  - **Centre times within frames** ([#573](https://github.com/altqx/hikari/issues/573)): after the shift and the end-time fix, each shifted time moves to the centisecond nearest the middle of its frame (`application::centeredTime`, the same as Video › Centre times within frames), so a later shift by time keeps it on whole frames. Times stays respected: Start centres only the starts. With Move tag times too, the tag times move with the line, so they stay on their frames. The postprocessor never centres. The switch is bit 64 of `shiftTimes.options`, and presets keep it as `centerTimes`.

## Timing postprocessor

- **Preset** and **Lines**. Lines is the postprocessor's own choice, with the same six options; it starts from Shift times' choice the first time.
- One row per rule, each with a switch:
  - **Lead-in:** Start earlier by N ms.
  - **Lead-out:** End later by N ms.
  - **Close small gaps:** Start back to the previous end if within N ms; End on to the next start if within N ms. The two limits are legacy's start and end thresholds; a gap up to their sum closes, split in their proportion.
  - **Snap to keyframes:** Start within N ms before / N ms after; End within N ms before / N ms after.

## Preview and Apply

- Each panel previews the run on the right when it is wide (640 pixels for Shift times, 560 for the postprocessor) and below its choices when it is narrow. The preview follows every change of a value, the document, the selection, the video frame and the audio mark.
- **The status** reads "Changes N lines", or for the postprocessor "Changes N of M lines · N lead-in · N lead-out · N gaps closed · N snapped". It says "No line changes" when nothing would move, and the refusal ("No styles selected for time shifting") when the run cannot start. Screen readers hear it announced.
- **The rows** are the selected lines in the run, or the first lines that change when none are selected, up to eight, with "N more lines change." under them. Each row shows the line number, the old and new start and end, and the text (Shift times) or the rules that moved it ("lead-in · snapped end", or "no change"). Its accessible name reads all of that.
- **Apply** names its effect: "Shift 214 lines", "Apply to 31 lines". It is disabled when nothing would change or the run is refused. The note beside it names the undo step: "One undo step: “Shift times”" or "“Timing postprocessor”".
- **Keys:** Ctrl+Enter applies from any field. Esc goes back to the Grid.

## A choice that can't run

Each says why and offers one command:

- Frames without a video: "Frames need a video." Open video….
- Fix overlaps or Recalculate from text length without a video: "Needs a video." Open video…. Without a video the core shifts and leaves the end times, and logs a warning.
- Centre times within frames without a video: "Needs a video." Open video…. Without a video the core shifts without centring, and logs "Times were not centred within frames: no video is open".
- Every postprocessor rule without a video: "The rules need a video." Open video….
- Snap to keyframes with a video but no keyframes: "Needs keyframes." Open keyframes….
- Close small gaps with both limits at 0 ms: "Both gap limits are 0 ms. Raise one to close gaps."

## Presets

- Each panel has its own presets: a combo box reading "Preset: name" ("Preset: last used" when the values have changed since a preset was applied), and a ⋯ menu with Save as preset…, Rename preset… and Delete preset…. Saving over a name replaces it after saying so.
- The postprocessor's menu also has **Use in the Timing layout**. The preset it marks shows "(Timing default)" and is applied whenever the Timing layout is chosen.
- Settings: `shiftTimes.presets` and `postprocessor.presets` hold one JSON object `{"name", "values"}` per preset; `shiftTimes.preset`, `postprocessor.preset` and `postprocessor.timingPreset` name the current and Timing presets; `postprocessor.whichLines` and `postprocessor.styles` are the postprocessor's Lines.
- **Migration:** the first start without `shiftTimes.presets` turns each legacy Shift times profile (`shiftTimes.profiles`) into a preset with the same name and values, then drops the profiles. Each panel still remembers its last values in the settings it used before; `shiftTimes.collapsedSections` is gone with the sections.
