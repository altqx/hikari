#!/bin/sh
# Screenshot each tm1-*.html under the private Xvfb (vx11), full page.
set -e
cd "$(dirname "$0")/out"
for f in tm1-*.html; do
  /home/altq/Work/hikari-wt/bin/vx11 chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars \
    --ozone-platform=headless --user-data-dir=/tmp/tm1-chromium --force-device-scale-factor=1 \
    --window-size=1340,${TM1_HEIGHT:-2700} --screenshot="$PWD/${f%.html}.png" "file://$PWD/$f" >/dev/null 2>&1
done
ls -la *.png
