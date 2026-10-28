# AV1–AV3 on Windows (Winix VM "dev", Windows 11 Pro 26200)

Build: winix job `job-be263e3ff0e64aecb2e9b2b1af99a51e` (workspace `hikari-qt`,
`w6-av` at d7cffcaf: AV1 f20392f4, AV2 dabbad20, AV3 39937cc4), the real
`hikarisub.exe` on the VM desktop, driven by `winav.py` (the D1 gate's helpers:
UI Automation, `winix ui`, `winix vm device` for a QEMU USB audio device on the
silent audiodev). Media: `av-drift.mkv` (60 s, 25 fps, a burned-in clock, with
audio; the same clip as the Linux run) opened from Video > Open video….

## QtTest on Windows
- `job-01a47164cfe4418aa4b5512dc91a934a`: `audioPanelOutputDeviceMenu`,
  `audioAndVideoCursorsFollowEachOther`, `dummyVideoFromItsDialog` PASS
  (`linuxAudioPrefersPulseAudio` came later than that build).
- `job-0ac0b8fa23714dc3ab278efcf22e7bd9`: `hikari_ui_session_tabs_tests` 17/18,
  `aDummyVideoRestoresWithItsSilence` PASS; `aLaterTabNeverInheritsAudio` timed
  out waiting 20 s for a plain .mka to index (no video, no dummy, no AV code)
  and passed alone in `job-0032e6379fc040f0b2d920eb7b9ec662` (4 s) with
  `aDummyVideoRestoresWithItsSilence`: load on the VM, not the change.

## AV1 — output device menu
- `av1-w-3-menu-with-usb.png`, `av1-w-d1-menu-plugged-in.png`: the speaker
  button's menu lists System default, the hot-plugged "Speakers (N- Audio
  Device)" and "Speakers (High Definition Audio Device)", then Audio settings….
  The button's accessible name carries the device in use ("Output device:
  Speakers (6- Audio Device)").
- Choosing the USB device: it is checked in the menu, and Settings > Audio's
  Device shows it (`av1-w-5-settings-audio-page.png`, combo "Device: Speakers (4-
  Audio Device)", API WASAPI).
- Unplugged while the video plays (`av1-w-d2-playing-on-usb.png`, then
  `winix vm device remove`): the app goes on, the button names "Speakers (High
  Definition Audio Device)" (the system default), and the status bar says
  "Speakers (6- Audio Device) is not con…" 1.8 s and 4.7 s after the unplug
  (`av1-w-d3-unplugged-0/1.png`; cut short by the fields beside it, the whole
  text in its tooltip), gone by 6.8 s (`-2`). The menu then lists "Speakers (4-
  Audio Device) (not connected)" still checked (`av1-w-9-menu-not-connected.png`).
- Not observed: which endpoint the stream itself is on (the audiodev is silent;
  no per-endpoint session read); the hover tooltips (the tablet pointer lands
  short of the strip's right end on this two-monitor desktop; the Linux run has
  them).

## AV2 — the cursors follow each other
- Sync on, the video playing: `av2-w-video-plays-15.png` video 38.48 s (clock
  38.56), audio cursor ≈ 38.6 s on the ruler; `av2-w-video-plays-30.png` video
  53.48 s (clock 53.60), cursor ≈ 53.5 s: within about 0.1 s, the view following
  (30–40 s, then 45–55 s). `-45`: the video at its end, no cursor.
- Off (`av2-w-off-video-plays.png`): the video played to its end, no cursor in
  the audio panel and its view still at 0–11 s. Persistence across a restart is
  covered on Windows by `audioAndVideoCursorsFollowEachOther` (job above).

## AV3 — a dummy video brings its own silence
- `av3-w-2-dummy-with-silence.png`, `av3-w-b1-dummy-with-silence.png`: Video >
  Open dummy video (1920x1080, 35964 frames): the audio panel opens its silence
  (ruler from 0, the line's selection drawn). The Audio menu's retired "Open
  blank 2h30m audio" is checked by `dummyVideoFromItsDialog` on Windows (job
  above).
