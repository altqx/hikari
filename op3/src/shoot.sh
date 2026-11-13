#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 build.py
mkdir -p shots
for id in grid picker states icon; do
  for t in light dark hcw hcb; do
    raw=shots/raw-$id-$t.png
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
      --window-size=1500,1400 --screenshot="$raw" "file://$PWD/index.html?only=$id&theme=$t" >/dev/null 2>&1
    magick "$raw" -fuzz 1% -trim +repage -bordercolor '#f3f4f6' -border 16 "shots/$id-$t.png"
    rm -f "$raw"
  done
done
ls shots
