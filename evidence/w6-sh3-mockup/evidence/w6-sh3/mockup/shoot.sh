#!/bin/sh
# Render every page with headless Chromium. Run under vx11. Usage: shoot.sh <icons dir>
set -e
cd "$(dirname "$0")"
python3 -I gen.py "$1" html
mkdir -p ../out
for f in html/*.html; do
    n=$(basename "${f%.html}")
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1600,2000 --screenshot="$PWD/../out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "../out/$n.png" -format '%[pixel:p{1400,1999}]' info: 2>/dev/null || true)
    magick "../out/$n.png" -trim +repage -bordercolor "$(magick "../out/$n.png" -format '%[pixel:p{1599,1999}]' info:)" -border 16 "../out/$n.png"
done
ls -la ../out
