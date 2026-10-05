# MuseScore 4 UI appearance model

Reference for how MuseScore Studio 4 models themes, accent colour and colour roles.
Source code was read at tag **v4.7.5** (commit `3654226c2e99289916916953a98e585a3d3b315a`),
the newest release tag on 2026-10-05. The four theme files and the accent presets are
byte-identical at v4.4.4 and v4.5.2, and the high-contrast colour editor is the same at
v4.5.2, so everything below also holds for 4.4 and 4.5. (In 4.5 the preferences QML lived
under `src/appshell/qml/Preferences/`. It moved to `src/preferences/qml/` later.)
Short links like [uiconfiguration.cpp] point to the v4.7.5 sources in the list at the end.

## 1. Themes

There are four fixed themes. Users cannot add themes or name their own.
The code keys come from [uitypes.h], the titles from [themeconverter.cpp], and the values
from [app/configs/*.cfg].

| Code (`ThemeCode`, a string) | UI title | Dark? | High contrast? |
|---|---|---|---|
| `light` (default) | Light | no | no |
| `dark` | Dark | yes | no |
| `high_contrast_white` | White | no | yes |
| `high_contrast_black` | Black | yes | yes |

- The page shows two themes at a time, as clickable sample cards. Light and Dark show by
  default. Ticking **Enable high-contrast** swaps the cards to White and Black
  ([AppearancePreferencesPage.qml], [ThemesSection.qml]).
- The high-contrast switch keeps the light/dark side: `setIsHighContrast()` maps
  light↔White and dark↔Black ([uiconfiguration.cpp]).
- Each theme is a JSON `.cfg` resource (`:/configs/<code>.cfg`). Unstable (dev) builds
  first look for `<appDataPath>/<code>.cfg`, watch that file, and hot-reload it when it
  changes ([uiconfiguration.cpp] `makeStandardTheme`).

### Follow system theme

- This is a **Follow system theme** checkbox. Its setting key is
  `ui/application/followSystemTheme` and it defaults to `false`. It is a flag next to the
  theme code, not a fifth theme.
- The checkbox only appears when `IPlatformTheme::isFollowSystemThemeAvailable()` returns
  true. While it is on, a platform listener calls `doSetIsDarkMode(isSystemThemeDark())`.
  That call keeps the high-contrast flag and only flips dark/light (for example, a system
  change can move White to Black).
- Picking a theme by hand (`setCurrentTheme`, `setIsDarkMode`) turns following **off**.
- The listener watches the OS light/dark setting only. No platform maps the OS
  high-contrast mode onto MuseScore's high-contrast themes.
- **Windows** ([windowsplatformtheme.cpp]):
  - The app always offers following on Windows.
  - Detection uses WinRT `UISettings.GetColorValue(UIColorType::Foreground)`. If the
    foreground colour is light (`5G + 2R + B > 1024`), the system is dark. This is the
    method Microsoft documents.
  - Changes come in through the `UISettings.ColorValuesChanged` event.
- **Linux** ([linuxplatformtheme.cpp]):
  - Uses the XDG desktop portal over D-Bus: `org.freedesktop.portal.Settings.Read("org.freedesktop.appearance", "color-scheme")`.
  - `1` means dark. Any other value (0 = no preference, 2 = light) means light.
  - Changes come in through the `SettingChanged` signal.
  - Following is available only when `org.freedesktop.portal.Desktop` is registered and
    the read succeeds.
- **macOS**: reads `AppleInterfaceStyle` and sets `NSApp.appearance` to Aqua or DarkAqua
  to match the theme.

## 2. Accent colour

There are 7 preset accents per mode. They are hard-coded in
`UiConfiguration::possibleAccentColors()` ([uiconfiguration.cpp]). The dark list is used
when `isDarkMode()` is true, which covers both `dark` and `high_contrast_black`.

| # | Light theme | Dark theme |
|---|---|---|
| 0 red | `#F28585` | `#F25555` |
| 1 orange | `#EDB17A` | `#E1720B` |
| 2 yellow | `#E0CC87` | `#AC8C1A` |
| 3 green/teal | `#8BC9C5` | `#27A341` |
| 4 blue (**default**) | `#70AFEA` | `#2093FE` |
| 5 purple | `#A09EEF` | `#926BFF` |
| 6 pink | `#DBA0C7` | `#E454C4` |

- **Defaults.** The default accent is the `accent_color` in each theme file: light
  `#70AFEA` and dark `#2093FE` (preset 4). High-contrast White uses `#00D87D` and Black
  uses `#0071DA`; neither is a preset.
- **Accent is per theme.** Picking a swatch writes `ACCENT_COLOR` into the *current*
  theme's values. Light and Dark therefore keep separate accents. The selected swatch is
  found by matching the stored hex against the list. If nothing matches, the index is −1
  and no swatch is selected.
- **Custom accent.** In Light and Dark there is no picker, only the 7 swatches
  ([AccentColorsList.qml], a `RadioButtonGroup` of round swatches). The swatch row is
  hidden in high-contrast mode. In that mode a free colour picker for the accent appears
  instead (see §4).
- **What the accent drives** (framework `Muse.UiComponents` QML and the QWidget style):

  | Element | How the accent is used |
  |---|---|
  | Selected list/tree rows (`ListItemBlank`) | accent fill at `accentOpacityNormal` (0.5); 0.3 on hover, 0.7 when pressed |
  | Primary buttons (`FlatButton.accentButton`, default `QPushButton`s) | accent background at `buttonOpacityNormal` (0.7) |
  | Toggle switch (`ToggleButton`) | track is accent when on, `buttonColor` when off; the knob is always `#FFFFFF` |
  | Radio button (`RoundedRadioButton`) | checked dot |
  | Checkable tool buttons (`FlatToggleButton`) | `checkedColor` |
  | Tabs (`StyledTabButton`, `PageTabButton`) | 2 px underline on the current tab |
  | Text fields (`TextInputField`) | border is accent when focused, accent at 0.6 on hover; text selection is accent at 0.5 |
  | Progress bar and busy spinner | fill or stroke |
  | QWidget `QPalette::Highlight` | accent ([themeapi.cpp] `setupWidgetTheme`) |

  The accent does **not** drive the keyboard focus ring or checkboxes. A `CheckBox` is a
  `buttonColor` box with a tick in `fontPrimaryColor`.

## 3. Theme roles

`ThemeStyleKey` in [uitypes.h] declares 19 colour roles, 2 widths and 7 opacities.
`ThemeApi` exposes them to QML as `ui.theme.<role>` ([themeapi.h]). The names below are the
JSON/QML names from [themeconverter.cpp]. The values come from [app/configs/*.cfg].

| Role | Light | Dark | HC White | HC Black |
|---|---|---|---|---|
| backgroundPrimaryColor | `#F5F5F6` | `#2D2D30` | `#FFFFFF` | `#000000` |
| backgroundSecondaryColor | `#E6E9ED` | `#363638` | `#FFFFFF` | `#000000` |
| popupBackgroundColor | `#F5F5F6` | `#39393C` | `#FFFFFF` | `#000000` |
| textFieldColor | `#FFFFFF` | `#242427` | `#FFFFFF` | `#000000` |
| accentColor | `#70AFEA` | `#2093FE` | `#00D87D` | `#0071DA` |
| strokeColor | `#CED1D4` | `#1E1E1E` | `#000000` | `#FFFFFF` |
| buttonColor | `#CFD5DD` | `#595959` | `#FFFFFF` | `#000000` |
| fontPrimaryColor | `#111132` | `#EBEBEB` | `#1E0073` | `#FFFD38` |
| fontSecondaryColor | `#FFFFFF` | `#BDBDBD` | `#000000` | `#BDBDBD` |
| linkColor | `#0B69BF` | `#8EC9FF` | `#000000` | `#FFFFFF` |
| focusColor | `#75507b` | `#75507b` | `#75507b` | `#75507b` |
| borderWidth | 0 | 0 | 1.0 | 1.0 |
| navigationControlBorderWidth (`navCtrlBorderWidth`) | 2.0 | 2.0 | 2.0 | 2.0 |
| accentOpacityNormal / Hover / Hit | 0.5 / 0.3 / 0.7 | same | same | same |
| buttonOpacityNormal / Hover / Hit | 0.7 / 0.5 / 1.0 | same | same | same |
| itemOpacityDisabled | 0.3 | same | same | same |

- **Declared but unset in 4.7.5.** These keys exist in the enum but none of the four
  `.cfg` files gives them a value, so they come out as invalid `QColor`s:
  `backgroundTertiaryColor`, `backgroundQuarternaryColor`, `projectTabColor`,
  `strokeSecondaryColor`, `whiteColor`, `blackColor`, `playColor`, `recordColor`. No QML in
  the tree reads them.
- **`extra`.** `ThemeApi.extra` also exposes every raw `.cfg` key as a `QVariantMap`.
- **`focusColor` is vestigial.** It has the same value in all four themes and only one
  Learn-panel item reads it. The real focus ring is `NavigationFocusBorder`: a border in
  `fontPrimaryColor`, `navCtrlBorderWidth` (2 px) wide. The QWidget `ProxyStyle` draws
  focus the same way.
- **How the opacity triples are used.** Hover and pressed states are drawn as a translucent
  overlay rather than as separate colours. Buttons put `buttonColor` (or the accent) at
  normal 0.7, hover 0.5 and pressed 1.0. Selected rows use the accent at 0.5/0.3/0.7. Any
  disabled control sets `opacity: itemOpacityDisabled` (0.3) on the whole item.
- **QWidget palette** ([themeapi.cpp]):

  | QPalette role | Theme role |
  |---|---|
  | Window | backgroundPrimary |
  | Base, AlternateBase | backgroundSecondary |
  | Text, WindowText, ButtonText, PlaceholderText, HighlightedText, ToolTipText | fontPrimary |
  | Button | button |
  | Link | link |
  | ToolTipBase | popupBackground |
  | Highlight | accent |

  Disabled variants use the same colour with alpha `itemOpacityDisabled`. Widgets are
  drawn by a Fusion-based `ProxyStyle` that copies the QML look.

## 4. High contrast: what users can edit

When high contrast is on, the page shows a **UI colors** section ([UiColorsSection.qml]).
It replaces the accent swatches and has four `ColorPicker`s. They are wired in
`AppearancePreferencesModel::setNewColor` ([appearancepreferencesmodel.cpp]):

| Picker label | Writes theme role | Works? |
|---|---|---|
| Accent color | `accentColor` | yes, any colour |
| Text and icons | `fontPrimaryColor` | yes |
| Disabled text | — | **no**: the picker always shows `#000000`, and the handler is `NOT_IMPLEMENTED` |
| Border color | `strokeColor` | yes |

- Each edit is stored on the current high-contrast theme only, so White and Black keep
  their own edits.
- Light and Dark have no colour editing apart from the 7 accent presets.
- "Reset to default" (`resetAppearancePreferencesToDefault`) rebuilds all four themes from
  their `.cfg` files. It also resets fonts, the score background and the paper.

## 5. Colour preferences outside the theme

These live in the notation and engraving configuration, not in the UI theme
([notationconfiguration.cpp], [engravingconfiguration.cpp]).

- **Background** (the canvas behind the score). You choose a solid colour or a wallpaper
  image (`ui/canvas/background/useColor`, `…/wallpaper`).
  - The colour is stored **per theme**:

    | Theme | Setting key | Default |
    |---|---|---|
    | Light | `lightTheme_score_background_color` | `#BCC1CC` |
    | Dark | `darkTheme_score_background_color` | `#27272B` |
    | HC Black | `hc_black_score_background_color` | `#000000` |
    | HC White | `hc_white_score_background_color` | `#000000` (sic) |

  - The setter writes the slot for whichever theme is current.
- **Paper** (the page itself). You choose a solid colour or a wallpaper
  (`ui/canvas/foreground/useColor|color|wallpaper`). This is a single setting for all
  themes, with a default of `#f9f9f9`.
- **Invert score** (`engraving/scoreColorInversion`), with the option
  `ui/canvas/onlyInvertInDarkTheme`. When it is active, the paper is forced to black and
  notation is drawn in `rgb(220,220,220)`. "Only in dark theme" ties inversion to
  `isDarkMode()`, which covers `dark` and `high_contrast_black`.
- **Score content colours are theme-independent.** The selection colour for each voice is
  set in Advanced preferences (`engraving/colors/voice1..4`, `allVoicesColor`):

  | Voice | Default |
  |---|---|
  | 1 | `#0065BF` |
  | 2 | `#007F00` |
  | 3 | `#C53F00` |
  | 4 | `#C31989` |
  | All voices | `#6038FC` |

  Other engraving colours:

  | Colour | Default |
  |---|---|
  | Formatting | `#C31989` |
  | Frame | `#A0A0A4` |
  | Invisible | `#808080` |
  | Unlinked | `#FF9300` |
  | Score text | black |

  - Selection of an invisible item is the voice colour tinted by 0.6.
  - The playback cursor and the note-input preview colour come from the selection colour.
  - None of these follow the UI accent. The only engraving value that reads the theme is
    `EngravingConfiguration::fontPrimaryColor()`. `SingleDraw` uses it to draw
    layout-break and spacer symbols in palette cells.

## 6. Icons

- Icons are glyphs from an **icon font**: `MusescoreIcon.ttf` in [ui/data]. The setting is
  `ui/theme/iconsFontFamily`, default `"MusescoreIcon"`, and each icon is an `IconCode`
  enum value.
- `StyledIconLabel` is a `StyledTextLabel` whose text is `String.fromCharCode(iconCode)`.
  Its colour therefore defaults to `ui.theme.fontPrimaryColor`.
- `FlatButton.iconColor` also defaults to `fontPrimaryColor`.
- Icon size is body font size + 4 px (regular) or + 6 px (toolbar).
- The framework never tints an icon glyph with the accent. When something is "accented",
  the accent fills the background (toggle buttons, accent buttons) and the glyph stays
  `fontPrimaryColor`.
- In high contrast, the "Text and icons" picker recolours text and icons together because
  both read `fontPrimaryColor`.

## 7. Persistence and live apply

- **Storage.** Settings go through `muse::Settings`, which wraps `QSettings` in INI format
  under the app data path. The keys are:

  | Key | Contents |
  |---|---|
  | `ui/application/currentThemeCode` | one of the 4 codes; default `light` |
  | `ui/application/followSystemTheme` | bool |
  | `ui/application/themes` | compact JSON array holding **only the modified themes** |
  | `ui/theme/fontFamily`, `ui/theme/fontSize` | UI font; default size 12 |

  - Each object in `ui/application/themes` is the full `ThemeConverter::toMap` of a theme:
    `codeKey`, `title`, and every role.
  - On load, modified themes from that array override the built-in `.cfg` themes with the
    same code ([uiconfiguration.cpp] `updateThemes`/`writeThemes`).
- **Live apply.**
  - Every setter calls `setSharedValue`. That fires the key's change channel, and
    `UiConfiguration` turns the change into `currentThemeChanged`.
  - `ThemeApi::update()` then re-reads the values, rebuilds the QPalette and Fusion proxy
    style, and emits `themeChanged`.
  - Every `ui.theme.*` QML binding has `NOTIFY themeChanged`, so the whole UI repaints at
    once.
  - In multi-instance builds, `setSharedValue` also sends the change to the other running
    instances. The handbook confirms that changes show immediately and apply to all open
    instances.
- **Preferences dialog.**
  - Opening the dialog calls `settings()->beginTransaction()` ([appshellconfiguration.cpp]).
  - During the transaction, values change in memory and their channels fire, so the
    preview is live. Nothing is written to disk yet.
  - **OK** runs `commitTransaction`, which writes to disk.
  - **Cancel** runs `rollbackTransaction`, which sends the old values back through the
    channels and so reverts the live preview ([settings.cpp]).

## Sources

All code links are pinned to tag `v4.7.5` (commit `3654226c2e99289916916953a98e585a3d3b315a`).

- [uitypes.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/uitypes.h
- [uiconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/uiconfiguration.cpp
- [themeconverter.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/themeconverter.cpp
- [themeapi.h]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/api/themeapi.h
- [themeapi.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/api/themeapi.cpp
- [app/configs/*.cfg]:
  - https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/light.cfg
  - https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/dark.cfg
  - https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/high_contrast_white.cfg
  - https://github.com/musescore/MuseScore/blob/v4.7.5/src/app/configs/high_contrast_black.cfg
- [windowsplatformtheme.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/windows/windowsplatformtheme.cpp
- [linuxplatformtheme.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/linux/linuxplatformtheme.cpp
- macOS: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/ui/internal/platform/macos/macosplatformtheme.mm
- [AppearancePreferencesPage.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/AppearancePreferencesPage.qml
- [ThemesSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/ThemesSection.qml
- AccentColorsSection.qml: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/AccentColorsSection.qml
- [AccentColorsList.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/AccentColorsList.qml
- [UiColorsSection.qml]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/internal/UiColorsSection.qml
- [appearancepreferencesmodel.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/preferences/qml/MuseScore/Preferences/appearancepreferencesmodel.cpp
- [notationconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/notation/internal/notationconfiguration.cpp
- [engravingconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/engraving/internal/engravingconfiguration.cpp
- [settings.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/framework/global/settings.cpp
- [appshellconfiguration.cpp]: https://github.com/musescore/MuseScore/blob/v4.7.5/src/appshell/internal/appshellconfiguration.cpp
- [ui/data] (icon font): https://github.com/musescore/MuseScore/tree/v4.7.5/src/framework/ui/data
- Components:
  - https://github.com/musescore/MuseScore/tree/v4.7.5/src/framework/uicomponents/qml/Muse/UiComponents
  - Files read there: `StyledIconLabel`, `StyledTextLabel`, `NavigationFocusBorder`,
    `FlatButton`, `FlatToggleButton`, `ToggleButton`, `CheckBox`, `RoundedRadioButton`,
    `ListItemBlank`, `TextInputField`, `StyledTabButton`.
- Cross-version check (same theme `.cfg`s and accent lists):
  - https://github.com/musescore/MuseScore/blob/v4.4.4/src/framework/ui/internal/uiconfiguration.cpp
  - https://github.com/musescore/MuseScore/blob/v4.5.2/src/framework/ui/internal/uiconfiguration.cpp
  - https://github.com/musescore/MuseScore/blob/v4.5.2/src/appshell/qml/Preferences/internal/UiColorsSection.qml
- Handbook, Appearance: https://handbook.musescore.org/customization/appearance
- Handbook, Preferences: https://handbook.musescore.org/customization/preferences
