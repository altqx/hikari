#!/bin/sh
# Render each theme's sheet, and each of its sections, with headless Chromium. Run under vx11.
set -e
cd "$(dirname "$0")"
python3 -I gen.py "$@"
cd out
for f in gr1-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1910,4400 --screenshot="$PWD/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "$n.png"
done
ls -la *.png
