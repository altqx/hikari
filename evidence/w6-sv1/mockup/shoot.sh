#!/bin/sh
# Render the SV1 sheets with headless Chromium. Run under vx11. gen.py is base.py (FR1's shared generator) + sv1.py.
set -e
cd "$(dirname "$0")"
cat base.py sv1.py > gen.py
python3 -I gen.py "${1:-/home/altq/.t3/worktrees/hikari/w6-sv1}"
mkdir -p out
for f in sv1-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1920,1400 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "out/$n.png"
done
ls out
