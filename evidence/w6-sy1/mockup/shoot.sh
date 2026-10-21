#!/bin/sh
# Render each page with headless Chromium. Run under vx11 after body.sh and crop.sh have filled shots/.
set -e
cd "$(dirname "$0")"
python3 -I gen.py "$@"
mkdir -p out
for f in sy1-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1376,3000 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "out/$n.png"
done
ls out
