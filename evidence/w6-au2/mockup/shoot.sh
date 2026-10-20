#!/bin/sh
# Screenshot each au2-*.html under the private Xvfb (vx11), full page.
set -e
cd "$(dirname "$0")/out"
for f in au2-*.html; do
  /home/altq/Work/hikari-wt/bin/vx11 chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars \
    --user-data-dir=/tmp/au2-chromium --force-device-scale-factor=1 \
    --window-size=1384,2080 --screenshot="$PWD/${f%.html}.png" "file://$PWD/$f" >/dev/null 2>&1
done
ls -la *.png
