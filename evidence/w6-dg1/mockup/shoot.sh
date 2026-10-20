#!/bin/sh
# Render each page with headless Chromium. Run under vx11 after body.sh has filled shots/.
set -e
cd "$(dirname "$0")"
for t in light dark highContrastWhite highContrastBlack; do   # today's dialogs, cropped from body.sh's shots
    magick "shots/today-about-$t.png" -crop 528x435+376+202 +repage "shots/crop-about-$t.png"
    magick "shots/today-credits-$t.png" -crop 608x332+336+253 +repage "shots/crop-credits-$t.png"
done
python3 -I gen.py "$@"
mkdir -p out
for f in dg1-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1328,3200 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "out/$n.png"
done
ls -la out
