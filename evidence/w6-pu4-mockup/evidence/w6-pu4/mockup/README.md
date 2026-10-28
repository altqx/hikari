# PU4 #306 Video, audio and timing popups mockup

- `kit.py`: PK1's mockup kit (as copied for PU2/PU3): the theme roles of src/ui/theme.cpp, DG1's dialog frame, the
  popover frame, the message box, the toast and IC1's icons (read from the tree given to gen.py).
- `gen.py [TREE]`: the PU4 pages per theme: `1-video` (Open dummy video, Set new FPS, each as it opens and while a
  value is refused), `2-aspect` (Change aspect ratio as a popover from the Video context menu and as a compact window
  from its hotkey; the fullscreen video window with the popover, a toast and the Next file message box) and
  `3-questions` (Next file and Shift times message boxes, NT1's toasts, and the two popups PK1 already moved).
  File names, times and values are examples.
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
