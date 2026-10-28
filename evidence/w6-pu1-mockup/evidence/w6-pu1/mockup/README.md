# PU1 #303 file and session popups mockup

- `body.sh THEME OUTDIR MODE` (under vx11): today's HikariSub (the PK1 build, qt ce0c6740e content) in THEME at
  1280x800. MODE `doc`: Episode 03.ass and Signs.ass open (`fixture.ass`, `station.mkv`), restored through
  LastSession.txt. MODE `home`: the Home page with the last Session and two recent files. `shots.sh` takes all
  eight into `shots/`.
- `station.mkv` (not committed): the ffmpeg command in evidence/w6-sh1-mockup's README.
- `gen.py [TREE]`: the HTML pages, drawn with PK1's mockup helpers (`kit.py`, from evidence/w6-pk1-mockup's
  gen.py) from the theme layer's roles (src/ui/theme.cpp), DG1's dialog frame and IC1's icons; `shoot.sh`
  (under vx11) renders them with headless Chromium into `out/`.
