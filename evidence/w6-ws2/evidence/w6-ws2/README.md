# WS2 #216 evidence: the `[Hikari]` section

Fixtures: `tests/fixtures/hikari-section` (`plain.ass`, `fonts.ass` with an attached font in `[Fonts]`, and each with the `[Hikari]` section a save writes, from `make-fixtures.py`).

- `linux/libass`: libass 0.17.5 through FFmpeg's ass filter (`probe/libass.sh`): every frame of 4 s at 10 fps the same with and without the section; PNGs at 1.5 s.
- `linux/aegisub-3.5.0`: Aegisub 3.5.0 (Arch package, private Xvfb): opens each fixture without error; its saves drop the section and are byte-identical to its saves of the files without it; `[Fonts]` kept.
- `linux/tests`: the local `hikari_ui_*` runs under a private Xvfb.
- `windows/csri-probe.txt`: the CSRI renderers on Windows 11 (Winix VM): the legacy release's xy-VSFilter and VSFilterMod and the pinned VSFilterMod build, read from the file as a player reads it (`probe/csri-probe.cpp`, `probe/run.ps1`): the same frames with and without the section, the attached font in use.
- `windows/legacy-*`, `windows/legacy.txt`: legacy HikariSub v0.0.1-rc.1 on Windows 11: opens each fixture without error (6 lines, nothing from the section); after an edit and Ctrl+S its saves drop the section and are byte-identical to its saves of the files without it; `[Fonts]` kept.
- `wine/`: the same renderer probe and legacy round trip under Wine 11.20 (private Xvfb), before the VM turn; the same results, except that under Wine the renderers did not use the attached font with or without the section.
