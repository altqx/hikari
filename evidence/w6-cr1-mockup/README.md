# CR1 #452 crash and issue reports mockup

- `kit.py`: PK1's mockup kit (as copied for PU2–PU7): the theme roles of src/ui/theme.cpp, DG1's dialog frame, the
  message box, the toast and IC1's icons (read from the tree given to gen.py).
- `gen.py [TREE]`: three sheets per theme: `1-crash` (HikariSub closed unexpectedly, report collapsed and
  expanded, the whole report and its GitHub form), `2-hang` (the freeze notice over the main window, Report a
  freeze, the window after a freeze that was closed, outcome toasts), `3-issue` (Help › Report an issue: Bug,
  Feature request, Question, the attachment previews and the Finish on GitHub page).
- `shots/`: AD3's screenshots of today's HikariSub (qt 7788b8a7c) behind the notice and as the screenshot thumbnail.
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
