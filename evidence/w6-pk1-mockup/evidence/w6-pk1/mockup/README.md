# PK1 #301 popup kit mockup

- `body.sh THEME OUTDIR` (under vx11): today's HikariSub (the DG1 build, qt 1f6fe86f8) in THEME at 1280x800 with
  `fixture.ass` and `station.mkv` (SH1's fixture), restored through LastSession.txt: the main window the notices
  sit on.
- `station.mkv` (not committed): the ffmpeg command in evidence/w6-sh1-mockup's README.
- `gen.py [TREE]`: the HTML pages, drawn from the theme layer's roles (src/ui/theme.cpp), DG1's dialog frame,
  the colour popover's frame and IC1's icons; `shoot.sh` (under vx11) renders them with headless Chromium into
  `out/`.
