#!/bin/sh
# Render each sheet with headless Chromium into out/ (run under vx11), trimmed to its content.
set -e
cd "$(dirname "$0")"
python3 -I gen.py /home/altq/.t3/worktrees/hikari/w7-vb1/src/ui/icons html
mkdir -p out
for f in html/vb1-*.html; do
    n=$(basename "${f%.html}")
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$PWD/.chromium" --window-size=2760,2600 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 20 "out/$n.png"
done
ls -la out
