#pragma once

#include "hikari/application/audio_devices.h"
#include "hikari/application/settings.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

enum class OptionsControl {
    Check,
    Number,      // an int clamped to [min, max]
    Text,
    ZoomText,    // committed from its raw text
    IndexChoice, // the setting is the selection index
    Language,    // the selection's language tag
    Dictionary,  // the selection's dictionary symbol
    Catalog,     // the selection's catalog name
    Style,       // the selection's style name
    ComboText,   // its text
    Font,        // face name in `setting`, point size in `sizeSetting`
    Renderer,    // the selection's renderer name
    OutputDevice, // the selection's device id, its name in audio.outputDeviceName
};

enum class OptionsPage { General, Appearance, LineEditor, Spelling, Grid, Video, Audio, Conversion, SubtitleProperties,
                         Automation, Updates };
// Settings window page ids ("general", "lineEditor", ...).
std::string_view optionsPageId(OptionsPage page);
std::optional<OptionsPage> optionsPageOfId(std::string_view id);

struct OptionsBinding {
    std::string_view setting;
    OptionsControl control;
    OptionsPage page;
    std::int64_t min = 0;          // Number
    std::int64_t max = 0;          // Number
    int entries = 0;               // IndexChoice
    std::string_view sizeSetting;  // Font
};

std::span<const OptionsBinding> optionsBindings();
const OptionsBinding *findOptionsBinding(std::string_view setting);
// Hidden bindings (nothing reads them yet) keep their stored values.
bool optionsBindingShown(std::string_view setting);

std::span<const std::string_view> optionsConversionFps();

struct OptionsLists {
    std::vector<std::string> languageTags;      // "en" first
    std::vector<std::string> languageNames;
    std::vector<std::string> dictionarySymbols;
    std::vector<std::string> dictionaryNames;   // a placeholder entry when there is none
    std::vector<std::string> catalogs;
    std::vector<std::string> styles;            // the current catalog's
    std::string currentCatalog;
    // Fewer than two: not offered. A stored name not listed shows libass.
    std::vector<std::string> renderers;
    // Empty: not offered. Entry 0 is System default (empty id), also shown for an unknown stored id.
    std::vector<OutputDeviceChoice> outputDevices;
    std::function<std::string(std::string_view)> findLanguage;
    // Unset: folds ASCII letters only.
    std::function<bool(std::string_view, std::string_view)> sameIgnoringCase;
    // For fonts.externalDirectory.
    char pathSeparator = '/';
    bool slashesForBackslashes = true;
};

// Check: bool; Number, choices, Font size: int; Text, ZoomText, ComboText, Font face: string.
using OptionsState = std::map<std::string, SettingValue, std::less<>>;

struct OptionsOpening {
    OptionsState state;
    bool catalogMissing = false;
    bool styleMissing = false;
};

// `lists.styles` must be the option's catalog's styles. An FFMS2 seeking value outside 0-3 is
// stored as 2 at once.
OptionsOpening openOptionsDialog(Settings &settings, const OptionsLists &lists);
// The option's catalog when it exists and is not the current one.
std::optional<std::string> optionsCatalogToLoad(const Settings &settings, const OptionsLists &lists);

// Writes each value that differs from the stored one; returns the ids written.
std::vector<std::string> commitOptionsDialog(Settings &settings, const OptionsLists &lists, const OptionsState &state);

// After a reset: `state` is what the controls held before; `page` limits the refresh to one page.
OptionsState refreshOptionsDialogAfterReset(const Settings &settings, const OptionsLists &lists, OptionsState state,
                                            std::optional<OptionsPage> page = std::nullopt);

// `lists.styles` must hold the chosen catalog's styles; the style choice moves to the first, if any.
void chooseOptionsDialogCatalog(const OptionsLists &lists, OptionsState &state, int catalog);

// Leading blanks, a sign and the digits that follow, saturated to int32; 0 otherwise.
std::int64_t legacyAtoi(std::string_view text);
// A comma is a decimal point; 0 when the text is not a number; truncated, then clamped.
std::int64_t numCtrlValue(std::string_view text, std::int64_t min, std::int64_t max);

} // namespace hikari::application
