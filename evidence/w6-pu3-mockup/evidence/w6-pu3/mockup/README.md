# PU3 #305 Style and font popups mockup

- `kit.py`: PK1's mockup kit (as copied for PU2): the theme roles of src/ui/theme.cpp, DG1's dialog frame, the
  popover frame, the message box, the toast and IC1's icons.
- `gen.py [TREE]`: the PU3 pages per theme: `1-picker` (the font picker from the Line editor and from the Style
  editor), `2-catalogs` (Manage font catalogs, its New / Rename popovers, Add fonts from subtitles) and
  `3-questions` (the catalog message boxes, the load clash review, toasts). Font names are drawn in their own
  faces where this machine has them; fonts, catalogs and text are examples.
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
