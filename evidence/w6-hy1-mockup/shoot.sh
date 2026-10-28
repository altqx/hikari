#!/bin/bash
# Screenshot each theme's sheet with headless Chromium (run under vx11).
cd "$(dirname "$0")/out"
for t in light dark hcwhite hcblack; do
  chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
    --window-size=1780,1990 --ozone-platform=headless --screenshot="hy1-$t.png" "file://$PWD/hy1-$t.html" 2>/dev/null
done
ls -la *.png
