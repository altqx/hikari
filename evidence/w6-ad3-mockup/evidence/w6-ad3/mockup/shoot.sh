#!/bin/sh
# Render each page with headless Chromium. Run under vx11 after body.sh has filled shots/.
# gen.py is base.py (PK1's and NT1's generator) with ad3.py appended.
set -e
cd "$(dirname "$0")"
cat base.py ad3.py > gen.py
python3 -I gen.py "${1:-/home/altq/.t3/worktrees/hikari/w6-ad3}"
mkdir -p out
for f in ad3-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1376,4200 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "out/$n.png"
done
ls -la out
