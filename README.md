# PU7 #309 update, import and workspace popups mockup

- `kit.py`: PK1's mockup kit (as copied for PU2–PU5): the theme roles of src/ui/theme.cpp, DG1's dialog frame, the
  popover frame, the message box, the toast and IC1's icons (read from the tree given to gen.py).
- `gen.py [TREE]`: four sheets per theme, with PF1's Settings window (#337, evidence/w6-pf1-mockup) redrawn for
  Settings › Updates and Settings › Hotkeys:
  `1-updates` (A new version is available: install, downloading, AUR, AUR prerelease, download page, requirement;
  Install update; Settings › Updates), `2-import` (the 3-step Import legacy settings wizard and the Roll back
  question), `3-workspace` (Save layout as, Manage layouts, the delete question, Move panel), `4-hotkeys`
  (Settings › Hotkeys with Import legacy hotkeys, its toasts, the capture's refusal and conflict both ways).
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
