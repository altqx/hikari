# H2 #125: human listening, Windows half (2026-10-15)

Listener: the owner, on host RUBY ([../setup/host.md](../setup/host.md)). Machine-readable: [results.json](results.json); answers as given: [answers.txt](answers.txt); my actions with timestamps (UIA presses, default-output and endpoint changes): [actions.log](actions.log); app stderr: [app.log](app.log); devices: [env/](env/).

**Path under test.** `hikari_h2_audition` was **not built**: it links `hikari_backends` and the media helper, which need this repo's full vcpkg graph (FFmpeg, FFMS2, libass, PortAudio…) built on this PC, which would take hours. So, as the brief allows, part A used the **app's own Line audition** (A4 #157, Audio panel buttons pressed through UI Automation Invoke), which drives the same `PortAudioOutput` + `LineAudition` + media helper. Part B used the app's general playback (Qt Multimedia).

**Fixture.** `make_fixture.py` from the evidence head; peak −24 dBFS for all four files (`levels.json`); hashes: [env/fixture-sha256.txt](env/fixture-sha256.txt). Script `h2-script.ass` opened from the command line; the associated video/audio offer was accepted.

**Volume.** Default output 22E1W at 23 %, not muted, before the first sound ([env/default-output-volume-before.txt](env/default-output-volume-before.txt)); the owner set the system volume to ~20 %.

**Outputs.** Speakers (Razer BlackShark V2 X USB), a USB headset; 22E1W (NVIDIA HD Audio), HDMI to the AOC monitor. Others present but inactive: LM30-E330C (HDMI), Realtek Digital Output.

## Part A: Line audition (PortAudio; WASAPI unless noted)

| # | Step | Expected | Heard | Result |
| --- | --- | --- | --- | --- |
| A1 | open device, media | `opened` with its rate | — | **not observable**: the app has no device-id open and shows no output rate |
| A2 | Play the current line (Line 1, 3.0–5.0 s) | one rising whoop ~2 s; no hum, ticks or clicks | whoop only, clean (on the monitor, the default) | yes |
| A3 | Play 500 ms before the start | only the low hum | hum only | yes |
| A4 | Play 500 ms after the start (first 500 ms) | low beginning of the whoop | low whoop start | yes |
| A5 | Play 500 ms before the end (last 500 ms) | high end of the whoop, no ticks | high end, no ticks | yes |
| A6 | Play 500 ms after the end | four fast ticks | four ticks only | yes |
| A7 | 44.1 kHz audio (`h2-44k1.mkv`), Line 2 | same whoop as A2, no crackle | same as A2, clean | yes |
| A8 | Play to the end, Stop after ~0.9 s | stops at once | stopped at once | yes |
| A9 | default output changed during playback (22E1W → Razer at +4.4 s) | observation | **stayed on the monitor** to the end | observed |
| A10 | another chosen output | the whoop from that output | — | **not observable as written**: the app offers no device choice, only the host API (WASAPI / DirectSound) |
| A10 variant | Windows default set to the Razer *before* a new audition | plays on the current default | **played on the monitor** | **no**: the editor output keeps the device it first opened; a new audition does not follow the default |
| A10b | host API switched to DirectSound in Options (Razer default) | — | Razer, clean | yes: changing the host API reopens on the current default |
| A11 (DirectSound) | Razer unplugged during playback (+1.9 s) | device-lost or quiet; no hang | sound **moved to the monitor** and kept playing; app responsive; no message | observed |
| A11b (DirectSound) | reconnect, play | the device opens again | Razer, clean | yes |
| A11 (WASAPI) | Razer unplugged during playback (+2.7 s) | device-lost or quiet; no hang | sound **stopped**; app showed "Log window: **Audio player failed.**"; app responsive | observed |
| A11b (WASAPI) | reconnect, play | the device opens again | Razer, clean; no message | yes |

Evidence: [a7-line2-selected-44k1-audio.png](a7-line2-selected-44k1-audio.png), [a11-wasapi-log-window-audio-player-failed.png](a11-wasapi-log-window-audio-player-failed.png).
Finished-event numbers (tail after logical stop, output latency, underruns) for A2/A7 are **not observable** on this path: the app reports none.

## Part B: general playback (Qt Multimedia)

| # | Step | Expected | Heard | Result |
| --- | --- | --- | --- | --- |
| B0 | start with `h2-script.ass` | window with three Lines | three Lines | yes |
| B1 | accept associated files | counting video; Line 1 at 3–5 s | video and Line 1 subtitle shown | yes ([b3-playing-at-4s.png](b3-playing-at-4s.png)) |
| B2 | look for Line audition | (checklist predates A4) | present: Audio panel has Play line, 500 ms before/after start and end, Play to the end, Stop; Video panel has Play the current line | observed |
| B3 | Play from the start | beeps, hum, whoop, ticks in step with the counter | all in sync, **on the monitor although the Razer was the default** | yes (sound); see note |
| B4 | Pause, Play, Stop | pause silences, play resumes, stop → frame 0 | as expected; stop at 00:00:00,000 | yes ([b4-paused.png](b4-paused.png), [b4-stopped.png](b4-stopped.png)) |
| B5 | Line 2, Play; seek to ~14 s, Play | whoop at once; hum then whoop at 15 s | both as expected (seek landed at 14.056 s) | yes ([b5-after-seek.png](b5-after-seek.png)) |
| B6 | default output changed during playback | observation | stayed on the monitor; no gap | observed |
| B7 | playing output removed (Win+P "PC screen only" removes the 22E1W HDMI endpoint) | stops or moves; app keeps responding | **stopped**, did not move to the Razer; app responsive, no message; the video ran on silently; Qt logged `AUDCLNT_E_DEVICE_INVALIDATED` 165 times | observed |
| B7b | monitor back (Extend), Play | sound again | on the monitor (now the default) | yes |
| B8 | close | closes cleanly | closed, no prompt, process gone | yes |

## Answers to carry to I3 #115 / N6 #110

- Audition content (I3): every content step heard as specified (A2–A8, both rates). **No "no" for I3.**
- Device handling (N6): **one "no"**, the A10 variant: with WASAPI, the editor output stays on the device it first opened. A Line audition started after the Windows default changed still plays on the old device, and the app offers no device choice. So the only way to move it is to switch the host API in Options. Also recorded, without an expected result in the checklist: on WASAPI, losing the device stops the sound and shows "Audio player failed."; on DirectSound, Windows moves the stream to the new default.
- General playback (Qt Multimedia, V1/N5): does not follow the default output (B3, B6), and goes silent while the video keeps running when its device disappears (B7).
- Still missing: A1 and the finished-event numbers (they need `hikari_h2_audition` built on Windows); an explicit second-device choice (A10 as written).
