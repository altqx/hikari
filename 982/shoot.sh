#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
for t in light dark hcw hcb; do
  chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
    --window-size=1880,2600 --screenshot="raw-$t.png" "file://$PWD/credentials.html?shot=1&theme=$t" >/dev/null 2>&1
  magick "raw-$t.png" -trim +repage "credentials-$t.png"
  rm -f "raw-$t.png"
done
