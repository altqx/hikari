# PU6 #308 tool popups mockup (Spellchecker panel, Font collector, Conversion, Resample / properties)

- `kit.py`: PK1's mockup kit (as copied for PU2–PU5): the theme roles of src/ui/theme.cpp, DG1's dialog frame,
  the message box, the toast and IC1's icons (read from the tree given to gen.py).
- `body.sh THEME OUTDIR` (under vx11): today's HikariSub (the NT1 build, qt 7788b8a7c) in THEME at 1280x800 with
  `fixture.ass` (line 4 has the typo "tehm") and SH1's `station.mkv` (not committed); `CLICKS` open File › Log,
  whose place along the bottom is the Spellchecker panel's. `shots.sh` takes the four backdrops into `shots/`.
- `gen.py [TREE]`: the PU6 pages per theme: `1-spellchecker` (the panel over the Log's place, its ⋯ menu and
  empty states, Words added to dictionary, the toasts), `2-fonts` (Font collector on DG1's frame: checked, the
  copy review, running; the archive question and a Start refusal), `3-conversion` (Convert to ASS and to MDVD
  with sections and the live What changes; an invalid frame rate) and `4-resolution` (Resample subtitles with
  both buttons per row, ASS file properties, the incompatible resolution message box).
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
