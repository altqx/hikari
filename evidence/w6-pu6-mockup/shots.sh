#!/bin/bash
# shots.sh (under vx11): the four backdrops, one theme after another, with the Log panel open: its place
# along the bottom, the whole width, is where the Spellchecker panel opens.
cd "$(dirname "$0")"
for t in light dark highContrastWhite highContrastBlack; do CLICKS="52,16 94,379" ./body.sh "$t" shots; done
ls -la shots
