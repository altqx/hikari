# H2 listening checklist: Windows

These are the steps of `h2-session.sh` for a Windows machine with real audio endpoints. The Winix VM and GitHub's Windows runners have none, so WASAPI and DirectSound streams remain unobserved there; see N6 #110. Print this page, or keep it open beside the session. For each step, write **yes / no / skipped** and a note on what you heard. A skipped step stays open; it is never a pass.

Host: ______________________ Windows build: ______________ Date/time started: ______________

Build commit (`git rev-parse HEAD`): ______________________________________

## Before you start

1. Build the project (`windows-x64-release` preset), then build the driver:
   `cmake --build out\build\windows-x64-release --target hikari_h2_audition`
2. Copy the fixture folder from a Linux run (`~/hikari-h2-results/<date>/fixture/`), or make it with `python tools\human\h2-listening\make_fixture.py C:\h2\fixture` (needs `ffmpeg` on PATH). Check that `levels.json` and `h2-fixture.json` show a peak of -24 dBFS.
3. Open a terminal where the application runs from the build tree, with the Qt and FFmpeg DLLs found. Start the driver and keep its output:
   `hikari_h2_audition.exe --helper <build>\src\helpers\media\hikari-media-helper.exe | Tee-Object -FilePath C:\h2\driver.jsonl`
   Then type the commands below, one per line.
4. In Settings > System > Sound, write down the outputs and the current default: ______________________________

> **Volume first.** Before anything plays, turn the Windows volume down to about 20–30 %. Hold headphones away from your ears for the first sound. The fixture is quiet, but your output chain may not be.
>
> Low volume set: yes / no Volume: ____ %

## Part A: Line audition through PortAudio (WASAPI and DirectSound)

Type `devices`. It lists the WASAPI, DirectSound and MME devices. Run part A once on a **WASAPI** device and again on a **DirectSound** device if time allows. Write down the ids you use.

| # | Command(s) | Expected | Heard | Note |
| --- | --- | --- | --- | --- |
| A1 | `open <WASAPI id>` then `media C:\h2\fixture\h2-48k.mkv` | `opened` (write the rate) and `media` events | | id: |
| A2 | `play 3000 5000` | one rising whoop, about 2 s; no hum before, no ticks after, no clicks | | |
| A3 | `play 2500 3000` | only the low hum, half a second | | |
| A4 | `play 3000 3500` | the low beginning of the whoop only | | |
| A5 | `play 4500 5000` | the high end of the whoop only, no ticks | | |
| A6 | `play 5000 5500` | four fast ticks only | | |
| A7 | `media C:\h2\fixture\h2-44k1.mkv` then `play 9000 11000` | the same whoop as A2: same pitch and length, no crackle | | |
| A8 | `play 3000 20000`, then `stop` while the whoop sounds | stops at once; the report says `"drained": false` | | |
| A9 | `media ...h2-48k.mkv`, `play 0 20000`, then change the default output in Sound settings while it plays | observation: does the sound move, stay or stop? Is there a gap or glitch? (an explicitly opened WASAPI device is not expected to follow) | | |
| A10 | `open <another output's id>`, `media ...h2-48k.mkv`, `play 3000 5000` | the whoop from that output only | | id: |
| A11 | `open <USB/Bluetooth output id>`, `media ...`, `play 0 20000`, then unplug it (or disable it in Device Manager) | a `device-lost` or `failed` event; quiet; no hang or crash | | events: |
| A11b | reconnect (or enable), then `devices`, `open <same id>`, `media ...`, `play 3000 5000` | the id opens again; the whoop from that device | | |
| | `status` at any point, `quit` at the end | | | |

For each `finished` event, copy `tailAfterLogicalStopMs`, `outputLatency` and `underruns` for at least A2 and A7:

- A2: tail ______ ms, latency ______ ms, underruns ______
- A7: tail ______ ms, latency ______ ms, underruns ______

## Part B: General playback in the application (Qt Multimedia)

Start the application with private settings by pointing `APPDATA` and `LOCALAPPDATA` at an empty folder for this run. Open `C:\h2\fixture\h2-script.ass` from the command line (`hikarisub.exe C:\h2\fixture\h2-script.ass`). Save the console output if there is any.

| # | What to do | Expected | Heard | Note |
| --- | --- | --- | --- | --- |
| B0 | start the app | the window opens with three Lines | | |
| B1 | accept the associated-files offer in the Video panel | the counting video appears; Line 1's subtitle at 3–5 s | | |
| B2 | look for Line audition (Play line, Play 500 ms before/after) in the Audio panel and menus | not in this build: answer "no" and describe anything you find | | |
| B3 | Play from the start (Video panel Play, or Space) | three beeps, hum (2–3 s), whoop (3–5 s), ticks (5–6 s), in step with the counter | | |
| B4 | Pause, Play, Stop | Pause silences at once; Play resumes; Stop silences and goes to frame 0 | | |
| B5 | click Line 2, Play; then drag the seek bar to about 14 s and Play | whoop at once for Line 2; hum, then whoop at 15 s after the seek | | |
| B6 | Play, then change the default output in Sound settings | observation: where the sound goes; any gap | | |
| B7 | Play, then unplug or disable the playing output | the sound stops or moves; the app keeps running and responding | | |
| B7b | reconnect, then Play | sound again (write which device) | | |
| B8 | close the app (do not save) | closes cleanly; copy any Log window text into the note | | |

## After the session

Attach this page, `driver.jsonl` and a screenshot or list of the Sound settings outputs to #125. Any "no" reopens I3 #115 (audition content) or N6 #110 (device handling).
