# AU2 #268 evidence: PR #314 at 77c411b18

## CI (Qt rewrite workflows, head 77c411b18)
- Ubuntu 39501935288: success (Ubuntu x64 release, empty cache).
- Windows 39501940848: success (latest MSVC, empty cache; warm SDK, no credentials).
- Codex code review and security review: clean on 77c411b18.

## Local Linux runs (under vx11, akari-rvm not used: the hk-remote run waited 2 h for a slot and was stopped)
- `ui-offscreen-full-before-icon-fix.log`: all 81 `hikari_ui_*.offscreen` suites, run before 77c411b18. 80/81 passed; `panels` failed in `everyMenuItemHasAnIcon` (Classic and Default arrangement had no icon). The icons were added before the commit.
- `panels-offscreen-77c411b18.log`: the `panels` suite at 77c411b18, passing.
- `au2-tests-77c411b18.log`: `audioPanelCustomiseShowsTheFoldedControls`, `audioPanelArrangementSurvivesARestart`, `audioPanelRowFoldsItsControls`, `everyMenuItemHasAnIcon` and `audioPanelScreenshots` at 77c411b18: 7/7 (with init/cleanup).
- `settings-import-77c411b18.log`: `hikari_application_settings_import_tests` 30/30, including `TheAudioPanelArrangementIsLeftAsItIs`.

## Screenshots (linux/, four themes: dark, light, hcwhite, hcblack)
- `audio-panel-customise-*`: the Customise menu open from the gear (the whole window; the menu is taller than the room under the panel).
- `audio-panel-classic-*`: Classic in the default 640 px dock: the slider column, the switches and the extras that do not fit in ⋯.
- `audio-panel-classic-more-*`: the same with ⋯ open (Next after commit and Go folded first, then the switches).
- `audio-panel-classic-wide-*`: Classic in a 1100 px floating dock, the whole row.
- `audio-panel-default-*` and AU1's other states (play-around, commit, speaker, zoom, back-to-line, karaoke-spectrum, narrow) for comparison. `karaoke-spectrum` shows the new fold: with the gear, Karaoke mode's row no longer fits the default dock. The speaker shots show the test host's device names.

Windows: Qt Windows MSVC CI (the card is not Windows-specific); no Windows screenshots.
