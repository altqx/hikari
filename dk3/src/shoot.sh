#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p shots
ids=$(node -e "const fs=require('fs');global.ICONS={};eval(fs.readFileSync('spec.js','utf8')+';console.log(SECTIONS.map(s=>String(s.n).padStart(2,\"0\")+\"-\"+s.id).join(\" \"))')")
for entry in $ids; do
  id=${entry#*-}
  raw=shots/raw-$entry.png
  chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
    --window-size=2400,1500 --screenshot="$raw" "file://$PWD/index.html?only=$id&" >/dev/null 2>&1
  magick "$raw" -fuzz 1% -trim +repage -bordercolor '#f3f4f6' -border 16 "shots/$entry.png"
  rm -f "$raw"
done
ls shots
