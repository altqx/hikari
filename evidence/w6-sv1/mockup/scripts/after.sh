#!/usr/bin/env bash
# after.sh: libass at screen size (frame = WxH, storage = 640x360) over the bilinear-scaled clip, + Rotate Z overlay at logical size.
set -eu
cd ~/.cache/sv1/after
M=~/.cache/sv1/media
# name W H rx ry scale screenW screenH
while read name W H rx ry s SW SH; do
  ffmpeg -loglevel error -y -i $M/clip640.mkv -vf "scale=$W:$H:flags=bilinear,ass=$M/sharp.ass:original_size=640x360" -frames:v 1 pic-$name.png
  # place on a canvas the size of the screen shot so crop coordinates match the before shots
  magick -size ${SW}x${SH} xc:black pic-$name.png -geometry +$rx+$ry -composite base-$name.png
  ox=$(python3 -c "print($rx+420*$W/640)"); oy=$(python3 -c "print($ry+120*$H/360)")
  python3 -I - "$ox" "$oy" "$s" > draw-$name.mvg <<'PY'
import sys, math
ox, oy, s = map(float, sys.argv[1:])
r1, r2 = 40*s, 50*s
out = []
out.append(f"fill rgba(18,17,80,0.667) stroke none")
# annulus as a path with evenodd fill
out.append("fill-rule evenodd")
out.append(f"path 'M {ox+r2},{oy} A {r2},{r2} 0 1,0 {ox-r2},{oy} A {r2},{r2} 0 1,0 {ox+r2},{oy} Z M {ox+r1},{oy} A {r1},{r1} 0 1,0 {ox-r1},{oy} A {r1},{r1} 0 1,0 {ox+r1},{oy} Z'")
out.append(f"fill none stroke rgba(255,0,0,0.667) stroke-width {1*s}")
out.append(f"circle {ox},{oy} {ox+r2},{oy}")
out.append(f"circle {ox},{oy} {ox+r1},{oy}")
out.append(f"stroke rgba(255,0,0,0.667) stroke-width {10*s} line {ox-5*s},{oy} {ox+5*s},{oy}")
a = 12
x2, y2 = ox + r1*math.sin(math.radians(a+90)), oy + r1*math.cos(math.radians(a+90))
x3, y3 = ox + r1*math.sin(math.radians(a-90)), oy + r1*math.cos(math.radians(a-90))
out.append(f"stroke rgb(187,0,0) stroke-width {2*s} line {x2},{y2} {x3},{y3}")
out.append(f"line {ox-10*s},{oy} {ox+10*s},{oy}")
out.append(f"line {ox},{oy-10*s} {ox},{oy+10*s}")
print("\n".join(out))
PY
  magick base-$name.png -draw @draw-$name.mvg after-$name.png
done <<'T'
panel-s1 736 414 127 98 1 1920 1080
panel-s2 1472 828 254 196 2 3840 2160
full-s1 1920 1080 0 0 1 1920 1080
full-s2 3840 2160 0 0 2 3840 2160
T
