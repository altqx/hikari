# H2 listening session

Card: [H2 #125](https://github.com/altqx/hikari/issues/125). A person listens on real devices and records what they hear, alongside the automated I3 (#115) and N6 (#110) records in [the coverage ledger](../../../docs/qt/coverage.md). A "no" reopens I3 or N6. A step that is skipped or not observed stays open and is never recorded as a pass.

## What the session covers

| Part | Path under test | Steps |
| --- | --- | --- |
| A, Line audition | `PortAudioOutput` + `LineAudition` + the media helper (I3, N6), driven by `hikari_h2_audition` | play the Line, 500 ms before the start, the first 500 ms, the last 500 ms, 500 ms after the end, a 44.1 kHz source on a 48 kHz output, stop while playing, the desktop default output changing while playing, another chosen output, device loss while playing, then reopening by id after reconnecting |
| B, the application | general playback through Qt Multimedia (N5, V1) in `hikarisub` with private settings | load the associated video, look for Line audition in the UI, play from the start, pause/stop, seek to a Line and play, the default output changing while playing, device loss while playing, play after reconnecting |

At this build, the application has no Line audition: no Audio-panel playback, Play line, or 500 ms commands. That arrives with the Audio playback card (A4 #157). Part A therefore drives the same backend objects from a small console driver. Step B2 records what the UI offers. The in-app Line audition path stays unobserved until A4 lands.

## Fixture

`make_fixture.py` writes a 20 s fixture that peaks at **-24 dBFS** and refuses anything above -20 dBFS. It is never the loud ramp the automated tests use. The fixture has three Lines at 3–5 s, 9–11 s and 15–17 s. Around each Line:

- 1 s before the Line: a steady low hum (300 Hz)
- the Line itself: a rising whoop (500 → 1500 Hz sweep)
- 1 s after the Line: eight fast ticks (2 kHz blips)

It starts with three short beeps; the rest is silence. Every sound has 10 ms fades, so any click you hear is not part of the fixture. The audio is identical at 48 kHz and 44.1 kHz (PCM in MKV). The video is a counting test pattern. `h2-script.ass` names the video in Script Info, so the application offers to load it.

## Running it (Linux desktop, PipeWire or PulseAudio)

Requirements: a built tree (`out/build/ubuntu-x64-release` by default), plus `jq`, `python3`, `ffmpeg` and `pactl`. `wpctl` is optional. The script builds the driver target on first use with `cmake --build <build> --target hikari_h2_audition`. The target is excluded from `all`.

```sh
tools/human/h2-listening/h2-session.sh            # the real session, about 20-30 minutes
tools/human/h2-listening/h2-session.sh --dry-run  # no sound: checks the script itself
```

The script first records the device lists from PortAudio and pactl. It then makes the fixture and asks you to **set a low volume** before anything plays. It waits until you type `low`. Every step tells you what to expect. You answer `y`, `n`, `r` (replay) or `s` (skip), then add an optional note about what you heard.

Device steps change the desktop default output (`pactl set-default-sink`) or switch a card's profile off and back (`pactl set-card-profile`). They do this only after you press Enter. Every change is logged in `actions.log`, and the originals are restored on exit, including Ctrl+C. For physical unplugging, a USB headset is the easiest device to use.

Raw ALSA hardware devices (`... (hw:X,Y)`) bypass the desktop volume. The driver plays them 12 dB quieter, but keep the volume low anyway. The desktop routes (`pipewire`, `pulse`, `default`) follow the desktop volume and default output.

Options: `--build-dir`, `--out`, `--app`, `--helper`, `--audition-bin`, `--no-build`, `--skip-app`, `--skip-audition`.

## Results

These are written to `~/hikari-h2-results/<date>/` (a second run that day gets `-2`, and so on):

- `results.md`: the table of steps, what was expected, what was heard, and notes
- `results.json`: every answer with timestamps, the driver events during the step (audition reports with logical stop, last-frame consumption, estimated audible end and tail, underruns, device loss), the host, build and fixture
- `driver.jsonl`: every event from the PortAudio driver; `driver.stderr.log` holds ALSA's own messages
- `app/app.log`: the application's output, including Qt Multimedia audio-sink categories; `app/config` holds its private settings
- `env/`: system and PipeWire info, sinks and cards before, during and after, and the PortAudio device lists before and after reconnecting
- `fixture/`: the fixture and its hashes; `actions.log` lists the desktop audio changes

Attach `results.md` and `results.json` to #125.

## Windows

`windows-checklist.md` lists the same steps for a Windows machine, using WASAPI and DirectSound, the Sound settings and Device Manager.

## The driver by hand

`hikari_h2_audition --helper <hikari-media-helper>` reads one command per line and prints one JSON event per line. It accepts `devices`, `open <id>|default`, `media <path>`, `play <startMs> <endMs>`, `stop`, `status`, `gain <dB>`, `close` and `quit`; `simulate-loss` is for the dry run. Nothing plays until `play`.
