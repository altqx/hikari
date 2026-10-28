# PU2 #304 Lines and edit popups mockup

- `body.sh THEME OUTDIR` (under vx11): today's HikariSub (qt ce0c6740e, PU2's base) in THEME at 1280x800 with
  `fixture.ass` and `station.mkv` (SH1's fixture), restored through LastSession.txt: the main window the toasts
  sit on. `station.mkv` (not committed): the ffmpeg command in evidence/w6-sh1-mockup's README.
- `kit.py`: PK1's mockup kit (evidence/w6-pk1-mockup gen.py without its pages): the theme roles of
  src/ui/theme.cpp, DG1's dialog frame, the popover frame, the message box, the toast and IC1's icons.
- `gen.py [TREE]`: the PU2 pages (windows, the Fix minor errors panel, questions and notices); `shoot.sh`
  (under vx11) renders them with headless Chromium into `out/`.
