# KA2 (#277): tap-to-time karaoke timing and slowed playback, evidence

PR [#317](https://github.com/altqx/hikari/pull/317), branch `w6-ka2`. Owner-approved mockup:
[issuecomment-6299509979](https://github.com/altqx/hikari/issues/277#issuecomment-6299509979), approved with all seven defaults.

## What was built

- **Slowed playback with the pitch kept.** SoundTouch 2.4.1 (vcpkg `soundtouch`, LGPL-2.1-or-later, static) runs in the audio box player's fill thread. The speed is 25, 33, 50, 75 or 100 %, set in the speaker popover or with `[` and `]`. Positions, ends and taps stay in the song's own time (KA2-slowed-playback).
- **Tap to time.** In Karaoke mode a Tap split button sits after Auto split, with **Y** as its hotkey. Hold mode (the default) and Press mode are offered. Backspace undoes the last syllable and rewinds 2 s; Shift+Backspace undoes the whole Line. Esc stops and keeps the taps, with Resume and Revert Line. Each finished Line is one "Tap timing" history step, and tapping moves on to the next Line (KA2-tap-to-time).
- **Latency calibration.** The Tap menu opens a compact dialog-frame window: 16 beeps at 100 BPM, the first four practice. The result is the median of the presses after outliers are dropped, and it can be typed in as well (`audio.tapLatency`, −200 to 500 ms).
- **Hotkeys.** AUDIO_TAP is 1030 (Y), AUDIO_SPEED_DOWN 1031 (`[`) and AUDIO_SPEED_UP 1032 (`]`), from the coordinator's registry.

## Windows: slowed playback keeps its pitch (Winix VM)

Job `job-497e1f8b`, task `audio-slowed`, ran `BoxPlayer.SlowedPlaybackThroughADevice` through the VM's HDA endpoint over WASAPI:

```
Slowed playback through Speakers (High Definition Audio Device) (Windows WASAPI): 48000 Hz x 1, the tone 1.988 s long at 439.90 Hz, played 2.713 s, underruns 0
Slowed playback through Speakers (High Definition Audio Device) (Windows WASAPI): 48000 Hz x 1, the tone 1.988 s long at 439.90 Hz, played 2.748 s, underruns 0
```

The test plays a 1 s, 440 Hz tone at 50 %. It came out about twice as long (1.988 s) and still at 439.90 Hz, with no underruns.

Linux (ALSA null PCM) gives the same pitch:

```
Slowed playback through hikari_null (ALSA): 48000 Hz x 1, the tone 1.913 s long at 439.90 Hz, played 6.000 s
```

The underruns that line reports are the clockless null sink's, not the player's.

## Screenshots

- `shots-linux/`: the app on Linux in Light, Dark, High contrast black and High contrast white. Each theme has these states:
  - Tapping (the tap bar, the syllables, the lane on the waveform)
  - Stopped (Resume / Revert Line)
  - The Line that needs splitting first
  - The Tap menu
  - The calibration window
  - The speaker popover's Speed row
  - The Karaoke row
- `shots-windows/`: the same states from the Windows VM build, apart from the Karaoke row.

## CI

On head `37930def8`:

- Qt rewrite — Ubuntu, [run 39560629202](https://github.com/altqx/hikari/actions/runs/39560629202): success.
- Qt rewrite — Windows, [run 39560632973](https://github.com/altqx/hikari/actions/runs/39560632973): "Windows x64 release (latest MSVC), empty cache" succeeded. The warm-SDK job failed at its cache restore, which is the known cache eviction.

Head `003c21644` adds two Codex fixes:

- **`e5ccc89c7`:** the Commit, Previous / Next line and lead buttons wait while tapping.
- **`003c21644`:** the tap labels are plain text.

Its CI is in flight: Ubuntu [39575801467](https://github.com/altqx/hikari/actions/runs/39575801467), Windows [39575806250](https://github.com/altqx/hikari/actions/runs/39575806250). Locally on `003c21644`, 1664 of 1664 non-shell tests pass and all 9 shell suites pass.

## Tests

- `karaoke_tap_tests` (13), Qt-free: the Hold and Press state machine, gaps, undo-rewind, stop / resume / revert, the audio's end, mode changes, song time at each speed and latency, and the calibration maths.
- `audio_box_player_tests`: the stretch, the position mapping with SoundTouch's lead, the fill cap on a clockless sink, and a device run for the pitch.
- `hotkeys_tests` and `settings_import_tests`: the three commands, their defaults and the pinned counts.
- `shell_tests`:
  - `audioTapToTime`: the tap flow end to end. It covers focus, refused commits, unloading, a video playing, the audio's end against a failed output, the mode switch, the buttons waiting while tapping, and plain-text labels.
  - `audioTapCalibration`: presses on every beep, Apply / Cancel / Esc, the field disabled while the beeps play, and Enter in the field.
  - `audioSpeedInTheSpeakerPopover`, `audioPanelRowFoldsItsControls` (the Karaoke row folds the toggles first), `audioHotkeysFollowTheRegistry` (real key presses).

Ledger rows: KA2-tap-to-time, KA2-slowed-playback and KA2-karaoke-row-fold in `docs/qt/compatibility-decisions.md`, plus the KA2 row in `docs/qt/coverage.md`.
