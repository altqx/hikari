# PU5 #307 Visual tool popups mockup (Edit shapes, Edit tags)

- `kit.py`: PK1's mockup kit (as copied for PU2–PU4): the theme roles of src/ui/theme.cpp, DG1's dialog frame, the
  popover frame, the message box, the toast and IC1's icons (read from the tree given to gen.py).
- `gen.py [TREE]`: the PU5 pages per theme: `1-shapes` (Edit shapes, legacy "Vector shape editing": as it opens,
  an empty drawing with the Undo tooltip, a name clash, the clash and Restore default message boxes, the ⋯ menu)
  and `2-tags` (Edit tags, legacy "Tag editing": as it opens on blur, color1's three values with a refused range,
  the search, the clash and Restore default message boxes, the ⋯ menu). Shapes are legacy's five defaults plus an
  example of the user's own; tags are legacy's 22 under HY1's sections plus one of the user's own.
- `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
