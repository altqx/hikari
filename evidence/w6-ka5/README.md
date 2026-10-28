# KA5 (#280): snap syllable boundaries to vocal onsets, evidence

PR [#329](https://github.com/altqx/hikari/pull/329), branch `w6-ka5`, head `140fd841a`.

## What was built

- **The toggle.** "Snap to vocal onsets" is a checkable item in Karaoke mode's existing Tap menu. It is stored as `audio.onsetSnap` in the profile and is off by default, because detection is unreliable on smooth singing. It adds no other control and no hotkey.
- **Detection off the UI thread, cached per file.** Detection runs on a worker thread once the audio is loaded, and only while snapping is on in Karaoke mode.
  - Results are kept per audio file for the session, keyed by path, track, delay and length.
  - Closing the audio, turning snapping off or leaving Karaoke mode cancels a detection in progress.
  - At most 6 hours of audio is looked at.
- **Dragging.** Only a syllable boundary you actually drag snaps. It goes to the nearest onset within 100 ms (and 16 columns) that lies between its neighbours.
  - A click, or AUDIO_KARAOKE_MOVE_ON_CLICK, places the boundary exactly where you clicked.
  - Turning snapping on never moves a boundary that is already placed.
- **Tapping.** Each press snaps to the nearest onset at or after the last boundary. A press is a syllable's start, or the boundary in Press mode. Releases do not snap.
- **Detection (`vocal_onsets`, Qt-free).**
  - The input is the mono mixdown in 40 ms Hann windows every 10 ms.
  - Log magnitudes are taken in semitone bands from 150 to 4000 Hz. Each band is compared with the loudest of itself and its neighbours 20 ms earlier (SuperFlux), so vibrato within a semitone stays below the threshold.
  - A peak counts if it is above the local mean or median plus an adaptive delta, and at least 50 ms from the last one.
  - Each peak is then moved back to the sharpest doubling of the 10 ms energy.
  - Speed: about 1.5 s for a 4-minute file. Digital silence is not transformed.

## Screenshots (Linux, four themes)

`shots-linux/`: the Tap menu with Snap to vocal onsets checked, in Light, Dark, High contrast black and High contrast white. The audio is a generated sung WAV.

## Tests

- **`vocal_onsets_tests` (10):**
  - notes after silence (within 10 ms)
  - syllables joined by a short dip (within 20 ms)
  - a voice over a held chord
  - a held note with 60-cent vibrato (starts once)
  - bass below the voice
  - silence and noise
  - 44.1 and 22.05 kHz
  - audio past the 6 h cap
  - cancel
  - the snap's window and bounds
- **`audio_karaoke_tests`:** ADraggedBoundarySnapsToTheNearestOnset, AClickedBoundaryDoesNotSnap, SnappingKeepsBetweenTheNeighbours, TheRightButtonMovesTheLaterOnesByTheSnap.
- **`shell_tests` audioOnsetSnap** writes a sung WAV and checks:
  - the toggle and its setting
  - the worker, and its cancel when snapping is turned off
  - boundaries left in place when snapping is turned on
  - a drag with the real mouse, with snapping on and off
  - the per-file cache
  - Hold taps landing on the onsets

## CI and review on `140fd841a`

- **Qt rewrite — Ubuntu:** [run 39616265413](https://github.com/altqx/hikari/actions/runs/39616265413), success.
- **Qt rewrite — Windows:** [run 39616270144](https://github.com/altqx/hikari/actions/runs/39616270144). The job "Windows x64 release (latest MSVC), empty cache" ([119278602463](https://github.com/altqx/hikari/actions/runs/39616270144/job/119278602463)) succeeded. The warm-SDK job failed at its cache restore, which is the known cache eviction.
- **Codex:** the code review and the security review are both clean on `140fd841a`. Round 1's two findings were fixed and replied to inline: cancel a detection that is no longer wanted, and cap the audio looked at.
