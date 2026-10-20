# SH1 #220 title bar mockup

- `body.sh THEME OUT.png` (under vx11): today's HikariSub (w6-ws3's build) in THEME at 1280x800 with
  `fixture.ass` and `station.mkv`, restored through LastSession.txt; with `MENU_OUT` it also opens the
  status bar's layout switcher and screenshots its menu.
- `station.mkv` (not committed): `ffmpeg -f lavfi -i "gradients=s=1920x1080:r=24:c0=0x1d2b53:c1=0x7e4a6b:c2=0xe8a87c:nb_colors=3:x0=0:y0=0:x1=1920:y1=1080:d=30" -f lavfi -i "aevalsrc='0.6*sin(2*PI*180*t)*abs(sin(2*PI*0.37*t))*(0.5+0.5*sin(2*PI*3.1*t))+0.05*random(0)':s=48000:d=30" -c:v libx264 -crf 30 -pix_fmt yuv420p -c:a aac -shortest station.mkv`
- `gen.py [TREE]`: the HTML pages (title bar drawn from the theme roles over the real body); `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
