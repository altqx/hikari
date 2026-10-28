#!/bin/bash
# today-all.sh (under vx11): today's Line editor in the four themes, on Line 3, at the default dock and at
# its narrowest beside Audio; crops shots/today-THEME-{default,narrow}.png to the Line editor dock.
cd "$(dirname "$0")"
for t in dark light highContrastWhite highContrastBlack; do
  CLICK="300 599" ./today.sh $t shots >/dev/null 2>&1
  CLICK="300 599" DRAG="639 380 960 380" TAG=-narrow ./today.sh $t shots >/dev/null 2>&1
  convert shots/today-$t-1280x800.png -crop 640x200+640+279 +repage shots/today-$t-default.png
  convert shots/today-$t-1280x800-narrow.png -crop 480x200+800+279 +repage shots/today-$t-narrow.png
done
ls -la shots
