#!/bin/sh
# Screenshot each tp1-*.html under the private Xvfb (vx11), full page.
set -e
cd "$(dirname "$0")/out"
for f in tp1-*.html; do
  /home/altq/Work/hikari-wt/bin/vx11 chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars \
    --ozone-platform=headless --user-data-dir=/tmp/tp1-chromium --force-device-scale-factor=1 \
    --window-size=1340,${TP1_HEIGHT:-2120} --screenshot="$PWD/${f%.html}.png" "file://$PWD/$f" >/dev/null 2>&1
done
ls -la *.png
