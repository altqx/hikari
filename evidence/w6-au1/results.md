# AU1 #267 evidence (PR #291)

## CI on acac3e758 (the head before the qt merge 9d87e4511)
- Qt rewrite — Ubuntu 39462943094: success.
- Qt rewrite — Windows 39462948129: "Windows x64 release (latest MSVC), empty cache" success. The "warm SDK" job failed at actions/cache/restore (known cache-eviction noise).
- Codex: no findings on acac3e758 (code and security).

## Local runs (local (akari-rvm down) for the non-UI ones)
- e7f0317ba: heavy UI suites hikari_ui_*.offscreen 73/73 (ctest-ui.log). Everything else 1579/1589 at -j6, and the 10 xvfb failures pass serially (ctest-rest.log, ctest-rerun.log).
- 3e0dd06de: heavy UI suites 73/73 (ctest-ui-r2.log). Non-UI suites through hk-local, local (akari-rvm down), all pass (ctest-local-r2.log). That run's xvfb server died mid-run, so its .xvfb results don't count.
- 1d17abf9a: the .xvfb variants through hk-local at -j2, 77/82 (ctest-xvfb-r3.log). Serially (ctest-xvfb-rerun.log), panels and translation pass. documents (styleEditorColoursUseThePopover), video (chaptersAndStreamsOfTheVideo) and playing_frame (seek-bar timing) are the known local xvfb failures on plain qt that pass in CI.
- acac3e758: hikari_ui_shell_tests.translation.offscreen passes with no audio device (ALSA_CONFIG_PATH=<empty>, QT_AUDIO_BACKEND=alsa, the Windows CI condition). translation.offscreen and translation.xvfb also pass with sound. translation-acac.log is the no-device run.

## Screenshots
32 shots, 8 states in each of the 4 themes, from audioPanelScreenshots (HIKARI_SURFACE_SHOT_DIR) at e7f0317ba. Later commits change no visuals.
