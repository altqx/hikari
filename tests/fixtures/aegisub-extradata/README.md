# Aegisub extradata fixtures

Inputs and Aegisub 3.5.1's saves of them, for `hikari_core_extradata_tests` (FA3 #560).

- `folds.ass` and `perspective.ass` are written for these tests. `folds.ass` has nested, collapsed and adjacent folds, a uuencoded script value shared by two lines, an unused entry and an id with no entry (`{=42}`). `perspective.ass` has outer quads shared by two lines, used by one line and unused.
- `folds.aegisub.ass` and `perspective.aegisub.ass` are those files after Aegisub opened and saved them unchanged.
- `folds-edited.aegisub.ass` is `folds.ass` after the "fa3 edit" macro in `make/fa3.lua` changed it and Aegisub saved it.
- `*.lua-extra.txt` are the dumps `fa3.lua` writes: each event's `text` and `line.extra`, sorted by key and separated by tabs.

To regenerate them, run `make/run.sh <out-dir>` with Aegisub 3.5.1 installed and an X server that has no window manager, for example `vx11 bash make/run.sh out`, then copy the five files over these.
