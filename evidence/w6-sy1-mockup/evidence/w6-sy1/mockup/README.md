# SY1 #247 Style manager and Style editor mockup

- `body.sh THEME OUTDIR` (under vx11): today's Style manager (the PK1 build, qt 58192af34 content) in THEME with
  `fixture.ass` open and `Default.sty` as the style catalog: the manager, the editor on "Sign - Station", SY2's
  colour popover and SY3's font list. `crop.sh` cuts the popover and the font list out of those shots.
- `gen.py [TREE]`: the HTML pages, drawn from the theme layer's roles (src/ui/theme.cpp), DG1's dialog frame,
  PK1's kit and IC1's icons; `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
