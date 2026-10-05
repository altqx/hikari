#pragma once

// O1: the legacy Options dialog (OptionsDialog at 20d647c4) over the settings
// registry, without Qt. A binding is one ConOpt control; the dialog's state is
// what each control holds; opening, OK/Apply (SetOptions) and "Set default"
// (ResetDefault) turn settings into control state and back exactly as legacy
// does, defects included:
// - SetOptions walks the bound controls only and writes each value that
//   differs from the stored one;
// - a choice bound to an index (ID_HIKARI_CHOICE) keeps -1 when the stored
//   index is past its list (HikariChoice::SetSelection returns early), so OK
//   writes -1;
// - after ResetDefault those choices show FindString of the option's digits
//   (-1, so OK writes -1), the language, dictionary, catalog and style choices
//   show the entry at wxAtoi of their text (0, or the previous one when past
//   the list), number fields are clamped without the 40/200 fallbacks, and the
//   zoom field shows the raw stored text.
// Themes (excluded), Hotkeys (O2) and the Windows Associations page are not
// bound here.

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
    Check,       // HikariCheckBox
    Number,      // NumCtrl with ID_NUMBER_CONTROL: an int clamped to [min, max]
    Text,        // HikariTextCtrl
    ZoomText,    // the zoom NumCtrl (ID_VIDEO_ZOOM_PERCENT): SetOptions takes its raw text
    IndexChoice, // HikariChoice ID_HIKARI_CHOICE: the setting is the selection
    Language,    // ID_PROGRAM_LANGUAGE: the selection's language tag
    Dictionary,  // ID_DICTIONARY_LANGUAGE: the selection's dictionary symbol
    Catalog,     // ID_CONVERSION_STYLE_CATALOG: the selection's catalog name
    Style,       // ID_CONVERSION_STYLE: the selection's style name
    ComboText,   // HikariChoice with HIKARI_COMBO_BOX (conversion FPS): its text
    Font,        // FontPickerButton: face name in `setting`, point size in `sizeSetting`
};

// The pages, in the legacy tree's order (AddPage/AddSubPage).
enum class OptionsPage { Editor, Conversion, EditorAdvanced, Video, Audio, AudioAdvanced, SubtitleProperties };

struct OptionsBinding {
    std::string_view setting;
    OptionsControl control;
    OptionsPage page;
    std::int64_t min = 0;          // Number
    std::int64_t max = 0;          // Number
    int entries = 0;               // IndexChoice: the number of entries
    std::string_view sizeSetting;  // Font
};

// Every bound control, in the legacy ConOpt order; on Windows the rewrite's
// audio.outputHostApi and video.playbackPlayer (W1) choices follow them.
std::span<const OptionsBinding> optionsBindings();
const OptionsBinding *findOptionsBinding(std::string_view setting);

// The conversion FPS entries (FPSes).
std::span<const std::string_view> optionsConversionFps();

// What the choices list and how paths are written, gathered when the dialog
// opens.
struct OptionsLists {
    std::vector<std::string> languageTags;      // programLanguages ("en" first)
    std::vector<std::string> languageNames;     // shown
    std::vector<std::string> dictionarySymbols; // dictionaryLanguagesSymbols
    std::vector<std::string> dictionaryNames;   // shown; the "Put files .dic..." entry when there is none
    std::vector<std::string> catalogs;          // Options.dirs
    std::vector<std::string> styles;            // the current catalog's Styles
    std::string currentCatalog;                 // actualStyleDir
    // config::FindLanguage.
    std::function<std::string(std::string_view)> findLanguage;
    // HikariChoice::FindString's wxArrayString::Index(text, false):
    // wxString::IsSameAs without case, that is equal lengths and then
    // CmpNoCase, which is the C runtime's _wcsicmp (Windows) or wcscasecmp
    // (Linux) under the process locale. Legacy leaves that locale "C" (ASCII
    // letters only) unless a translation language set it at startup
    // (wxLocale::Init), when every letter folds. Unset: ASCII letters only.
    std::function<bool(std::string_view, std::string_view)> sameIgnoringCase;
    // EXTERNAL_FONTS_DIRECTORY: wxFileName::GetPathSeparator, and whether
    // HikariNormalizePath turns backslashes into slashes (not on Windows).
    char pathSeparator = '/';
    bool slashesForBackslashes = true;
};

// What each control holds: Check bool; Number, the choices and a Font's size
// int; Text, ZoomText, ComboText and a Font's face string.
using OptionsState = std::map<std::string, SettingValue, std::less<>>;

struct OptionsOpening {
    OptionsState state;
    bool catalogMissing = false; // "The selected catalog for style for conversion does not exist..."
    bool styleMissing = false;   // "The selected style for conversion does not exist..."
};

// The OptionsDialog constructor: the controls' state. An FFMS2 seeking value
// outside 0-3 becomes 2 and is stored at once. The catalog the option names
// is expected to be loaded already (legacy LoadStyles), so `lists.styles` are
// its Styles.
OptionsOpening openOptionsDialog(Settings &settings, const OptionsLists &lists);
// The catalog the dialog loads when it opens: the option's, when it exists
// and is not the current one.
std::optional<std::string> optionsCatalogToLoad(const Settings &settings, const OptionsLists &lists);

// SetOptions: each bound control's value that differs from the stored one is
// written. Returns the ids written.
std::vector<std::string> commitOptionsDialog(Settings &settings, const OptionsLists &lists, const OptionsState &state);

// ResetDefault's control refresh, after the settings were reset: `state` is
// what the controls held before.
OptionsState refreshOptionsDialogAfterReset(const Settings &settings, const OptionsLists &lists, OptionsState state);

// OnChangeCatalog: `lists.styles` now holds the chosen catalog's Styles; the
// style choice moves to the first (or keeps its index when there is none).
void chooseOptionsDialogCatalog(const OptionsLists &lists, OptionsState &state, int catalog);

// wxAtoi: leading blanks, a sign and the digits that follow; 0 otherwise.
std::int64_t legacyAtoi(std::string_view text);
// NumCtrl::SetString then GetInt for an integer field: the text's number
// (wxString::ToDouble, truncated), 0 when it is not one, clamped.
std::int64_t numCtrlValue(std::string_view text, std::int64_t min, std::int64_t max);

} // namespace hikari::application
