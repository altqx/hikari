#!/bin/sh
# Screenshot each theme's page with headless Chromium. Run under vx11.
set -e
cd "$(dirname "$0")"
python3 -I gen.py "$@"
for t in light dark hcwhite hcblack; do
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --user-data-dir="$(mktemp -d)" --window-size=2632,2500 --screenshot="$PWD/fs1-$t.png" "file://$PWD/fs1-$t.html" 2>/dev/null
done
ls -la *.png
