#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out=../shots
mkdir -p "$out"
ids="$(node -e "const fs=require('fs');global.ICONS={};eval(fs.readFileSync('spec.js','utf8')+';console.log(SECTIONS.map(s=>String(s.n).padStart(2,\"0\")+\"-\"+s.id).join(\" \"))')") 13-questions"
for entry in $ids; do
  id=${entry#*-}
  raw=$out/raw-$entry.png
  chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
    --window-size=1700,2200 --screenshot="$raw" "file://$PWD/../index.html?only=$id&" >/dev/null 2>&1
  magick "$raw" -fuzz 1% -trim +repage -bordercolor '#f3f4f6' -border 16 "$out/$entry.png"
  rm -f "$raw"
done
ls "$out"
