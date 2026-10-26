#!/bin/sh
# Screenshot each le1-*.html under the private Xvfb (vx11), full page.
set -e
cd "$(dirname "$0")/out"
mkdir -p chromium
for f in le1-*.html; do
  /home/altq/Work/hikari-wt/bin/vx11 chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars \
    --ozone-platform=headless --user-data-dir="$PWD/chromium" --force-device-scale-factor=1 \
    --allow-file-access-from-files --window-size=1360,${LE1_HEIGHT:-2700} --screenshot="$PWD/${f%.html}.png" "file://$PWD/$f" >/dev/null 2>&1
done
ls -la *.png
