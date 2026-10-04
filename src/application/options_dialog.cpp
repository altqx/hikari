#include "hikari/application/options_dialog.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace hikari::application {

namespace {

using enum OptionsControl;
using enum OptionsPage;

// OptionsDialog at 20d647c4, in the order its constructor calls ConOpt: the
// SubtitlesProperties page is built first, then Main, the advanced editor
// page, conversion, video, the two audio pages.
constexpr OptionsBinding kBindings[] = {
    // SubtitlesProperties (OptionsPanels.cpp): the six fields with their switches.
    {"scriptProperties.title", Text, SubtitleProperties},
    {"scriptProperties.titleOn", Check, SubtitleProperties},
    {"scriptProperties.script", Text, SubtitleProperties},
    {"scriptProperties.scriptOn", Check, SubtitleProperties},
    {"scriptProperties.translation", Text, SubtitleProperties},
    {"scriptProperties.translationOn", Check, SubtitleProperties},
    {"scriptProperties.editing", Text, SubtitleProperties},
    {"scriptProperties.editingOn", Check, SubtitleProperties},
    {"scriptProperties.timing", Text, SubtitleProperties},
    {"scriptProperties.timingOn", Check, SubtitleProperties},
    {"scriptProperties.update", Text, SubtitleProperties},
    {"scriptProperties.updateOn", Check, SubtitleProperties},
    {"scriptProperties.askForChange", Check, SubtitleProperties},
    // Main (GLOBAL_EDITOR).
    {"program.language", Language, Editor},
    {"editor.dictionaryLanguage", Dictionary, Editor},
    {"grid.loadSortedSubs", Check, Editor},
    {"editor.spellchecker", Check, Editor},
    {"grid.autoSelectLinesFromLastTab", Check, Editor},
    {"editor.suggestionsOnDoubleClick", Check, Editor},
    {"subtitles.openInNewTab", Check, Editor},
    {"editor.dontGoToNextLineOnTimesEdit", Check, Editor},
    {"video.disableLiveEditing", Check, Editor},
    {"grid.setVisibleLineAfterFullScreen", Check, Editor},
    {"shiftTimes.changeValuesWithTab", Check, Editor},
    {"grid.changeActiveOnSelection", Check, Editor},
    {"translation.showOriginal", Check, Editor},
    {"translation.hideOriginalOnVideo", Check, Editor},
    {"grid.duplicationDontChangeSelection", Check, Editor},
    {"grid.dontCenterActiveLine", Check, Editor},
    {"editor.allowNumpadHotkeys", Check, Editor},
    {"video.visualWarningsOff", Check, Editor},
    {"video.dontAskForBadResolution", Check, Editor},
    {"automation.oldScriptsCompatibility", Check, Editor},
    // EditorAdvanced.
    {"editor.saveAfterCharacterCount", Number, EditorAdvanced, 0, 10000},
    {"autosave.maxFiles", Number, EditorAdvanced, 2, 1000000},
    {"automation.traceLevel", Number, EditorAdvanced, 0, 5},
    {"program.tabTextMaxChars", Number, EditorAdvanced, 20, 150},
    {"grid.insertStartOffset", Number, EditorAdvanced, -100000, 100000},
    {"grid.insertEndOffset", Number, EditorAdvanced, -100000, 100000},
    {"grid.tagsSwapCharacter", Text, EditorAdvanced},
    {"grid.font", Font, EditorAdvanced, 0, 0, 0, "grid.fontSize"},
    {"program.font", Font, EditorAdvanced, 0, 0, 0, "program.fontSize"},
    {"grid.calcSpacesAndPunctuationForWraps", Check, EditorAdvanced},
    {"grid.calcSpacesAndPunctuationForCps", Check, EditorAdvanced},
    {"automation.loadingMethod", IndexChoice, EditorAdvanced, 0, 0, 4},
    {"fonts.externalDirectory", Text, EditorAdvanced},
    // ConvOpt.
    {"convert.styleCatalog", Catalog, Conversion},
    {"convert.style", Style, Conversion},
    {"convert.fps", ComboText, Conversion},
    {"convert.fpsFromVideo", Check, Conversion},
    {"convert.newEndTimes", Check, Conversion},
    {"convert.showSettings", Check, Conversion},
    {"convert.timePerCharacter", Number, Conversion, 30, 1000},
    {"convert.resolutionWidth", Number, Conversion, 1, 3000},
    {"convert.resolutionHeight", Number, Conversion, 1, 3000},
    {"convert.assTagsToInsertInLine", Text, Conversion},
    // video (VSFILTER_INSTANCE, the subtitle display filter, is not offered).
    {"video.fullScreenOnStart", Check, Video},
    {"video.pauseOnClick", Check, Video},
    {"video.openAtActiveLine", Check, Video},
    {"video.gpuConversion", Check, Video},
    {"video.acceptedAudioStream", Text, Video},
    {"video.ffms2Seeking", IndexChoice, Video, 0, 0, 4},
    {"video.zoomPercent", ZoomText, Video},
    // AudioMain.
    {"audio.drawTimeCursor", Check, Audio},
    {"audio.drawSecondaryLines", Check, Audio},
    {"audio.drawSelectionBackground", Check, Audio},
    {"audio.drawVideoPosition", Check, Audio},
    {"audio.drawKeyframes", Check, Audio},
    {"audio.lockScrollOnCursor", Check, Audio},
    {"audio.autoFocus", Check, Audio},
    {"audio.snapToKeyframes", Check, Audio},
    {"audio.snapToOtherLines", Check, Audio},
    {"audio.dontPlayWhenLineChanges", Check, Audio},
    {"audio.mergeEveryNWithSyllable", Check, Audio},
    {"audio.karaokeMoveOnClick", Check, Audio},
    {"audio.ramCache", Check, Audio},
    // AudioSecond.
    {"audio.delay", Number, AudioAdvanced, -50000000, 50000000},
    {"audio.markPlayTime", Number, AudioAdvanced, 400, 5000},
    {"audio.lineBoundariesThickness", Number, AudioAdvanced, 1, 5},
    {"audio.inactiveLinesDisplayMode", IndexChoice, AudioAdvanced, 0, 0, 3},
    {"audio.cacheFilesLimit", Number, AudioAdvanced, 0, 10000},
    {"audio.leadInValue", Number, AudioAdvanced, 0, 10000},
    {"audio.leadOutValue", Number, AudioAdvanced, 0, 10000},
#ifdef _WIN32
    // The rewrite's own, after legacy's: the Windows output's host API
    // (A4-wasapi-default: 0 WASAPI, 1 DirectSound), on the Audio page.
    {"audio.outputHostApi", IndexChoice, Audio, 0, 0, 2},
#endif
};

constexpr std::string_view kFps[] = {"23.976", "24", "25", "29.97", "30", "60"};

char asciiLower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

// wxString::IsSameAs(text, false) under the legacy process locale (see
// OptionsLists::sameIgnoringCase).
bool sameIgnoringCase(const OptionsLists &lists, std::string_view a, std::string_view b)
{
    if (lists.sameIgnoringCase)
        return lists.sameIgnoringCase(a, b);
    return std::ranges::equal(a, b, {}, asciiLower, asciiLower);
}

// HikariChoice::FindString: wxArrayString::Index without case, -1 for an
// empty text.
int findString(const OptionsLists &lists, const std::vector<std::string> &list, std::string_view text)
{
    if (text.empty())
        return -1;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (sameIgnoringCase(lists, list[i], text))
            return int(i);
    return -1;
}

// HikariChoice::SetSelection: past the list it changes nothing.
SettingValue select(const SettingValue &before, std::int64_t sel, std::size_t count)
{
    return sel >= std::int64_t(count) ? before : SettingValue(sel);
}

std::int64_t intOf(const OptionsState &state, std::string_view id)
{
    const auto it = state.find(id);
    return it == state.end() ? 0 : std::get<std::int64_t>(convertSetting(it->second, SettingType::Int));
}

std::string textOf(const OptionsState &state, std::string_view id)
{
    const auto it = state.find(id);
    return it == state.end() ? std::string() : std::get<std::string>(convertSetting(it->second, SettingType::String));
}

// The text legacy keeps for the zoom: VIDEO_ZOOM_PERCENT has no default, so
// an unset value is empty.
std::string zoomText(const Settings &settings, std::string_view id)
{
    return settings.isSet(id) ? settings.text(id) : std::string();
}

const std::vector<std::string> &choiceList(const OptionsLists &lists, OptionsControl control)
{
    switch (control) {
    case Language:
        return lists.languageNames;
    case Dictionary:
        return lists.dictionaryNames;
    case Catalog:
        return lists.catalogs;
    default:
        return lists.styles;
    }
}

std::string externalFontsPath(std::string path, const OptionsLists &lists)
{
    // HikariNormalizePath, then the separator when it is missing.
    if (lists.slashesForBackslashes)
        std::ranges::replace(path, '\\', '/');
    if (!path.empty() && path.back() != lists.pathSeparator)
        path.push_back(lists.pathSeparator);
    return path;
}

} // namespace

std::span<const OptionsBinding> optionsBindings()
{
    return kBindings;
}

const OptionsBinding *findOptionsBinding(std::string_view setting)
{
    const auto it = std::ranges::find(kBindings, setting, &OptionsBinding::setting);
    return it == std::end(kBindings) ? nullptr : &*it;
}

std::span<const std::string_view> optionsConversionFps()
{
    return kFps;
}

std::int64_t legacyAtoi(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || (text[i] >= '\t' && text[i] <= '\r')))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        negative = text[i++] == '-';
    std::int64_t out = 0;
    constexpr std::int64_t limit = std::numeric_limits<std::int32_t>::max();
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i)
        out = std::min(out * 10 + (text[i] - '0'), limit + 1); // int overflow is undefined; saturate
    out = negative ? -out : out;
    return std::clamp<std::int64_t>(out, std::numeric_limits<std::int32_t>::min(), limit);
}

std::int64_t numCtrlValue(std::string_view text, std::int64_t min, std::int64_t max)
{
    // ToDouble accepts the whole text as a number (a comma is a decimal
    // point there); anything else leaves 0.
    std::string s(text);
    std::ranges::replace(s, ',', '.');
    double value = 0;
    if (!s.empty()) {
        char *end = nullptr;
        const double parsed = std::strtod(s.c_str(), &end);
        if (end && *end == '\0' && std::isfinite(parsed))
            value = parsed;
    }
    value = std::clamp(value, double(min), double(max));
    return std::int64_t(value); // the shown text drops the fraction
}

std::optional<std::string> optionsCatalogToLoad(const Settings &settings, const OptionsLists &lists)
{
    const std::string option = settings.text("convert.styleCatalog");
    const int sel = findString(lists, lists.catalogs, option);
    if (sel >= 0 && lists.currentCatalog != option)
        return lists.catalogs[std::size_t(sel)];
    return std::nullopt;
}

OptionsOpening openOptionsDialog(Settings &settings, const OptionsLists &lists)
{
    OptionsOpening out;
    auto &state = out.state;
    for (const auto &b : kBindings) {
        const std::string id(b.setting);
        switch (b.control) {
        case Check:
            state[id] = settings.boolean(id);
            break;
        case Number:
            if (id == "program.tabTextMaxChars") {
                std::int64_t n = legacyAtoi(settings.text(id));
                if (n == 0)
                    n = 40;
                state[id] = numCtrlValue(std::to_string(n), b.min, b.max);
            } else
                state[id] = numCtrlValue(settings.text(id), b.min, b.max);
            break;
        case Text:
            state[id] = settings.text(id);
            break;
        case ZoomText: {
            std::int64_t percent = legacyAtoi(settings.text(id));
            if (percent < 100 || percent > 1100)
                percent = 200;
            state[id] = std::to_string(percent);
            break;
        }
        case IndexChoice: {
            std::int64_t sel = legacyAtoi(settings.text(id));
            if (id == "video.ffms2Seeking" && (sel < 0 || sel > 3)) {
                sel = 2;
                settings.set(id, sel); // and SaveOptions at once
            }
            state[id] = select(std::int64_t{-1}, sel, std::size_t(b.entries));
            break;
        }
        case Language: {
            const int sel = findString(lists, lists.languageNames, lists.findLanguage(settings.text(id)));
            state[id] = select(std::int64_t{-1}, std::max(sel, 0), lists.languageNames.size());
            break;
        }
        case Dictionary:
            state[id] = std::int64_t(findString(lists, lists.dictionaryNames, lists.findLanguage(settings.text(id))));
            break;
        case Catalog: {
            int sel = findString(lists, lists.catalogs, settings.text(id));
            if (sel < 0) {
                sel = std::max(findString(lists, lists.catalogs, lists.currentCatalog), 0);
                out.catalogMissing = true;
            }
            state[id] = select(std::int64_t{-1}, sel, lists.catalogs.size());
            break;
        }
        case Style: {
            int sel = findString(lists, lists.styles, settings.text(id));
            if (sel < 0) {
                sel = 0;
                out.styleMissing = true;
            }
            state[id] = select(std::int64_t{-1}, sel, lists.styles.size());
            break;
        }
        case ComboText: {
            const std::string text = settings.text(id);
            const auto it = std::ranges::find_if(kFps, [&](std::string_view f) {
                return !text.empty() && sameIgnoringCase(lists, f, text);
            });
            state[id] = it == std::end(kFps) ? text : std::string(*it);
            break;
        }
        case Font:
            state[id] = settings.text(id);
            state[std::string(b.sizeSetting)] = legacyAtoi(settings.text(b.sizeSetting));
            break;
        }
    }
    return out;
}

std::vector<std::string> commitOptionsDialog(Settings &settings, const OptionsLists &lists, const OptionsState &state)
{
    std::vector<std::string> written;
    auto write = [&](std::string_view id, const SettingValue &value) {
        settings.set(id, value);
        written.emplace_back(id);
    };
    for (const auto &b : kBindings) {
        const std::string_view id = b.setting;
        if (!state.contains(id))
            continue;
        switch (b.control) {
        case Check: {
            const bool v = std::get<bool>(convertSetting(state.find(id)->second, SettingType::Bool));
            if (settings.boolean(id) != v)
                write(id, v);
            break;
        }
        case Font: {
            const std::string face = textOf(state, id);
            if (settings.text(id) != face)
                write(id, face);
            if (state.contains(b.sizeSetting)) {
                const std::int64_t size = intOf(state, b.sizeSetting);
                if (legacyAtoi(settings.text(b.sizeSetting)) != size)
                    write(b.sizeSetting, size);
            }
            break;
        }
        case ComboText: {
            const std::string text = textOf(state, id);
            if (settings.text(id) != text)
                write(id, text);
            break;
        }
        case Language:
        case Dictionary: {
            const auto &values = b.control == Language ? lists.languageTags : lists.dictionarySymbols;
            const std::int64_t sel = intOf(state, id);
            if (sel >= 0 && sel < std::int64_t(values.size()) && settings.text(id) != values[std::size_t(sel)])
                write(id, values[std::size_t(sel)]);
            break;
        }
        case Catalog:
        case Style: {
            // GetString(GetSelection()): empty past the list.
            const auto &values = choiceList(lists, b.control);
            const std::int64_t sel = intOf(state, id);
            const std::string option = sel >= 0 && sel < std::int64_t(values.size()) ? values[std::size_t(sel)] : std::string();
            if (settings.text(id) != option)
                write(id, option);
            break;
        }
        case IndexChoice: {
            const std::int64_t sel = intOf(state, id);
            if (legacyAtoi(settings.text(id)) != sel)
                write(id, sel);
            break;
        }
        case Text: {
            std::string text = textOf(state, id);
            if (settings.text(id) != text) {
                if (id == "fonts.externalDirectory")
                    text = externalFontsPath(std::move(text), lists); // fonts reload: not yet
                write(id, text);
            }
            break;
        }
        case ZoomText: {
            // The zoom is not an ID_NUMBER_CONTROL: its text is stored as typed.
            const std::string text = textOf(state, id);
            if (zoomText(settings, id) != text) {
                if (text.empty()) {
                    settings.reset(id); // an empty option reads as unset
                    written.emplace_back(id);
                } else
                    write(id, legacyAtoi(text));
            }
            break;
        }
        case Number: {
            const std::int64_t n = std::clamp(intOf(state, id), b.min, b.max); // NumCtrl::GetInt
            if (legacyAtoi(settings.text(id)) != n)
                write(id, n);
            break;
        }
        }
    }
    return written;
}

OptionsState refreshOptionsDialogAfterReset(const Settings &settings, const OptionsLists &lists, OptionsState state)
{
    for (const auto &b : kBindings) {
        const std::string id(b.setting);
        switch (b.control) {
        case Check:
            state[id] = settings.boolean(id);
            break;
        case ComboText:
        case Text:
            state[id] = settings.text(id);
            break;
        case ZoomText:
            // Cast to HikariTextCtrl: its SetValue (not virtual) shows the raw text.
            state[id] = zoomText(settings, id);
            break;
        case Language:
        case Dictionary:
        case Catalog:
        case Style:
            // The inverted id test sends these to SetSelection(GetInt(text)).
            state[id] = select(state[id], legacyAtoi(settings.text(id)), choiceList(lists, b.control).size());
            break;
        case IndexChoice:
            // SetSelection(FindString(GetString(option))): the option's digits
            // name no entry, so the choice shows nothing (-1).
            state[id] = std::int64_t{-1};
            break;
        case Font:
            state[id] = settings.text(id);
            state[std::string(b.sizeSetting)] = legacyAtoi(settings.text(b.sizeSetting));
            break;
        case Number:
            // NumCtrl::SetInt(GetInt): clamped, without the constructor's 40.
            state[id] = std::clamp(legacyAtoi(settings.text(id)), b.min, b.max);
            break;
        }
    }
    return state;
}

void chooseOptionsDialogCatalog(const OptionsLists &lists, OptionsState &state, int catalog)
{
    state["convert.styleCatalog"] = std::int64_t(catalog);
    // Stylelist->Clear() keeps the selection; SetSelection(0) needs an entry.
    if (!lists.styles.empty())
        state["convert.style"] = std::int64_t{0};
}

} // namespace hikari::application
