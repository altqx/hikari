# AV1–AV3 native observation, Linux (2026-10-19)

Build: branch `w6-av` (AV1 f20392f4, AV2 dabbad20, AV3 39937cc4 on qt 4a4b0f45),
`out/build/w6-av-distcc`, the real `hikarisub` on X11 (a private Xvfb through
`vx11`, 1600x1000, no window manager).

Sound: a private PipeWire 1.6.9 + WirePlumber 0.5.18 + pipewire-pulse inside
the session (`session.sh`), with the hardware monitors off
(`90-no-hardware.conf`) and two null sinks, `Studio_Monitors` (default) and
`USB_Headset`. Nothing reached the host's sound server or cards. A sink is
"unplugged" by unloading its module. Media: `episode.ass` and a 60 s 25 fps
clip with a burnt-in timecode and frame number and a 440 Hz tone
(`ffmpeg testsrc2 + sine`, AAC).

## AV3 — a dummy video brings its own silence
- `av3-1-dummy-dialog.png`: Video > Open dummy video, 1920x1080, 23.976 fps, 0:25:00.
- `av3-2-dummy-video-with-silence.png`: after OK the Audio panel holds the dummy's
  silence (flat waveform, the 25 min timeline); nothing else was opened. The
  Audio menu has no "Open blank 2h30m audio".
- `av3-3-video-audio-replaces-silence.png`: opening a real video replaces the
  silence with that video's audio.

## AV1 — the output device menu
- `av1-1-button-tooltip.png`: the speaker button at the strip's end, its tooltip
  "Output device: Studio_Monitors".
- `av1-2-menu-open.png`: System default (checked), Studio_Monitors, USB_Headset,
  Audio settings….
- `av1-3-headset-chosen.png`: USB_Headset chosen from the menu: checked, the
  tooltip follows.
- `av1-4-settings-agree.png`: Audio settings… opens Options on its Audio page,
  Device = USB_Headset.
- General playback follows the choice: while the video played, `pactl list
  sink-inputs` showed the HikariSub stream on `headset` (RUNNING) and `studio`
  SUSPENDED. The audio box's PortAudio stream ("PipeWire ALSA [hikarisub]")
  stayed on the server's default sink: on Linux PortAudio's ALSA list has no
  sink names, as H2 recorded.
- `av1-5-unplugged-idle-notice-elided.png`: USB_Headset unloaded with nothing
  playing: "USB_Headset is not connected. Audi…" in the status bar, cut short
  by the video fields; this led to the status field's tooltip.
- `av1-5-unplugged-while-playing.png`, `av1-6-notice-tooltip.png`: with
  Studio_Monitors chosen and the video playing, the sink unloaded: the stream
  moved to `headset` and the status bar said "Studio_Monitors is not connected.
  Audio plays through USB_Headset." (whole in the tooltip);
  `av1-7-notice-cleared-after-5s.png`: gone after 5 s;
  `av1-8-menu-after-unplug.png`: the tooltip names USB_Headset, the menu keeps
  "Studio_Monitors (not connected)" checked.
- **Defect found (not AV1's):** with Qt 6.11.2's default PipeWire audio
  backend, removing the sink a playing general-player stream is on crashes the
  app (SIGSEGV in `libpipewire-module-protocol-native.so` on PipeWire's own
  thread, four times: twice with a chosen sink, twice with the default sink
  under System default, once of those with AV1's notice compiled out). With
  `QT_AUDIO_BACKEND=pulseaudio` the stream moves to the new default and the app
  goes on; the unplug-while-playing shots above were taken that way. Raised on
  #238 for the owner. **Fixed** after the owner's "Approved migrate to
  pulseaudio" (ledger AV1-linux-pulseaudio, 1952427d): on Linux the app now
  starts Qt Multimedia on its PulseAudio backend unless `QT_AUDIO_BACKEND` is
  set, and with no override the app went on through a sink removal while
  playing.
- Light theme: `light-output-menu.png`, `light-sync-tooltip.png`.

## AV2 — the cursors follow each other
- Video → audio (sync on): `av2-video-plays-{3,15,30,45}.png`. Video time against
  the audio cursor read off the ruler: 2.16 s / 2.17 s, 14.16 s / 14.29 s,
  29.04 s / 29.12 s, 44.16 s / 44.27 s: within about 0.13 s (3 frames, the two
  repaints not being the same instant) with no growing drift over 45 s; the
  view scrolled with the cursor (30–47 s at 44 s).
- Audio → video: `av2-audio-line-played-video-at-its-end.png`: the box played
  the selected line (1–3 s) and the paused video ended on frame 74 (2.96 s);
  `av2-audio-to-end-{2,6}.png`: "play to end" from the line: the box's cursor
  at about 7.2 s and the paused video on frame 180 (7.20 s). The video's
  play button stayed paused throughout (ADR 0009).
- `av2-1-toggle-tooltip.png`: the toggle, "Sync the video and audio cursors".
- Off: `av2-off-video-plays.png`: the video plays to 55.76 s, no cursor in the
  audio panel and the view does not move; `audio.syncVideo=false` stored.
- Persisted: `av2-off-after-restart.png` (off after a restart), `av2-on-again.png`
  (one click turns it on; `audio.syncVideo=true`).
