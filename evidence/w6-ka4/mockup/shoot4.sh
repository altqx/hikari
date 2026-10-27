#!/bin/sh
# Render each KA4 page with headless Chromium. Run under vx11.
# gen.py is base.py with ka3.py (less its ka3_main() call) and ka4.py appended.
set -e
cd "$(dirname "$0")"
{ cat base.py; grep -v '^ka3_main()$' ka3.py; cat ka4.py; } > gen.py
python3 -I gen.py "${1:-/home/altq/.t3/worktrees/hikari/w6-ka4}"
mkdir -p out
for f in ka4-*.html; do
    n=${f%.html}
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=1600,4200 --screenshot="$PWD/out/$n.png" "file://$PWD/$f" 2>/dev/null
    bg=$(magick "out/$n.png" -format '%[pixel:p{2,2}]' info:)
    magick "out/$n.png" -bordercolor "$bg" -border 1 -trim +repage -bordercolor "$bg" -border 24 "out/$n.png"
done
ls -la out
