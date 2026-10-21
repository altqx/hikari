#!/bin/sh
# Render each theme's sheet with headless Chromium. Run under vx11.
set -e
cd "$(dirname "$0")"
python3 -I gen.py "$@"
cd out
for t in light dark hcwhite hcblack; do
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1910,4800 --screenshot="$PWD/pf1-$t.png" "file://$PWD/pf1-$t.html" 2>/dev/null
    bg=$(magick "pf1-$t.png" -format '%[pixel:p{2,2}]' info:)
    magick "pf1-$t.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "pf1-$t.png"
done
ls -la *.png
