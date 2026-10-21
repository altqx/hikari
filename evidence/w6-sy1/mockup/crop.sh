#!/bin/sh
# Crop SY2's colour popover and SY3's font list out of body.sh's shots.
set -e
cd "$(dirname "$0")/shots"
for t in light dark highContrastWhite highContrastBlack; do
    magick today-popover-$t.png -crop ${POP:-218x278+420+284} +repage pop-$t.png
    magick today-fontlist-$t.png -crop ${FONTS:-484x430+419+0} +repage fonts-$t.png
done
