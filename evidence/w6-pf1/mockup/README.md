# PF1 #225 Settings redesign mockup

- `gen.py [TREE] [OUTDIR]`: one HTML sheet per theme, drawn from the theme layer's roles (src/ui/theme.cpp), DG1's
  dialog frame, PK1's message box and menu, and the icon set in TREE/src/ui/icons (two new icons, settings-grid
  and notices, are drawn in gen.py to K1's rules).
- `shoot.sh` (under vx11): renders the sheets with headless Chromium into `out/`.
- Built against qt ce0c6740e. The settings shown are every setting the dialog has there, plus KA1's two (PR #318).
