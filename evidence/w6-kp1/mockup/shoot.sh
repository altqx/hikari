#!/bin/sh
# Screenshot each kp1-*.html under the private Xvfb (vx11), full page.
set -e
cd "$(dirname "$0")/out"
for f in kp1-*.html; do
  /home/altq/Work/hikari-wt/bin/vx11 chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars \
    --ozone-platform=headless --user-data-dir=$HOME/.cache/kp1/chromium --force-device-scale-factor=1 \
    --window-size=1340,${KP1_HEIGHT:-2700} --screenshot="$PWD/${f%.html}.png" "file://$PWD/$f" >/dev/null 2>&1
  magick "${f%.html}.png" -trim +repage -bordercolor "$(magick "${f%.html}.png" -format "%[pixel:p{1,1}]" info:)" -border 18 "${f%.html}.png"
done
ls -la *.png
