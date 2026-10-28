# DG1 #222 dialog frame mockup

- `body.sh THEME OUTDIR` (under vx11): today's HikariSub (qt 9c1eef089, SH1's build) in THEME at 1280x800 with
  `fixture.ass` and `station.mkv` (SH1's fixture), restored through LastSession.txt; with `TODAY=1` it also opens
  today's About and Credits from the Help menu and screenshots them.
- `station.mkv` (not committed): the ffmpeg command in evidence/w6-sh1-mockup's README.
- `gen.py [TREE]`: the HTML pages (dialogs drawn from the theme layer's roles, SH1's title-bar metrics and IC1's icons
  over the real main window); `shoot.sh` (under vx11) crops today's dialogs and renders the pages with headless
  Chromium into `out/`.
