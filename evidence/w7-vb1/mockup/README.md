# VB1 #481 adaptive visual toolbar and inspector bar: mockup

- `today.sh THEME W H OUT` / `tools.sh THEME W H OUT` (under vx11): today's HikariSub (main b30fa7d77 content, AS1's
  build tree) with `fixture.ass` and the dummy video `?dummy:23.976:4000:1920:1080:47:163:161:c`; `tools.sh` clicks each
  rail tool and saves the Video panel. `today-strips.png` is every tool's bar today at a 640-pixel panel.
- `gen.py ICONS OUT`: the HTML sheets, drawn from the theme roles (src/ui/theme.cpp), the K1 icons and PK1's popover
  surface; `shoot.sh` (under vx11) renders them with headless Chromium into `out/`.
