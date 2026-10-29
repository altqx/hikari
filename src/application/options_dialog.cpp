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

constexpr OptionsBinding kBindings[] = {
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
    {"program.language", Language, General},
    {"editor.dictionaryLanguage", Dictionary, Spelling},
    {"grid.loadSortedSubs", Check, General},
    {"editor.spellchecker", Check, Spelling},
    {"grid.autoSelectLinesFromLastTab", Check, Grid},
    {"editor.suggestionsOnDoubleClick", Check, Spelling},
    {"subtitles.openInNewTab", Check, General},
    {"editor.dontGoToNextLineOnTimesEdit", Check, LineEditor},
    {"video.disableLiveEditing", Check, Video},
    {"grid.setVisibleLineAfterFullScreen", Check, Grid},
    {"grid.changeActiveOnSelection", Check, Grid},
    {"translation.showOriginal", Check, LineEditor},
    {"translation.hideOriginalOnVideo", Check, LineEditor},
    {"grid.duplicationDontChangeSelection", Check, Grid},
    {"grid.dontCenterActiveLine", Check, Grid},
    {"editor.allowNumpadHotkeys", Check, LineEditor},
    {"video.visualWarningsOff", Check, Video},
    {"video.dontAskForBadResolution", Check, Video},
    {"automation.oldScriptsCompatibility", Check, Automation},
    {"editor.saveAfterCharacterCount", Number, General, 0, 10000},
    {"autosave.maxFiles", Number, General, 2, 1000000},
    {"automation.traceLevel", Number, Automation, 0, 5},
    {"program.tabTextMaxChars", Number, General, 20, 150},
    {"grid.insertStartOffset", Number, Grid, -100000, 100000},
    {"grid.insertEndOffset", Number, Grid, -100000, 100000},
    {"grid.tagsSwapCharacter", Text, Grid},
    {"grid.font", Font, Grid, 0, 0, 0, "grid.fontSize"},
    {"program.font", Font, Appearance, 0, 0, 0, "program.fontSize"},
    {"grid.calcSpacesAndPunctuationForWraps", Check, Grid},
    {"grid.calcSpacesAndPunctuationForCps", Check, Grid},
    {"automation.loadingMethod", IndexChoice, Automation, 0, 0, 4},
    {"fonts.externalDirectory", Text, General},
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
    {"video.fullScreenOnStart", Check, Video},
    {"video.pauseOnClick", Check, Video},
    {"video.openAtActiveLine", Check, Video},
    {"video.gpuConversion", Check, Video},
    {"video.acceptedAudioStream", Text, Video},
    {"video.ffms2Seeking", IndexChoice, Video, 0, 0, 4},
    {"video.zoomPercent", ZoomText, Video},
    {"video.subtitleProvider", Renderer, Video},
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
    {"audio.delay", Number, Audio, -50000000, 50000000},
    {"audio.markPlayTime", Number, Audio, 400, 5000},
    {"audio.lineBoundariesThickness", Number, Audio, 1, 5},
    {"audio.inactiveLinesDisplayMode", IndexChoice, Audio, 0, 0, 3},
    {"audio.cacheFilesLimit", Number, Audio, 0, 10000},
    {"audio.leadInValue", Number, Audio, 0, 10000},
    {"audio.leadOutValue", Number, Audio, 0, 10000},
    {"automation.scriptResolutionFromVideo", Check, Video},
    {"grid.autoHideColumns", Check, Grid},
    {"audio.outputDevice", OutputDevice, Audio},
    {"editor.timesToFramesSwitch", Check, LineEditor},
    {"editor.hideTags", Check, LineEditor},
    {"subtitles.layoutInFile", Check, SubtitleProperties},
    {"video.moveToActiveLine", IndexChoice, Video, 0, 0, 6},
    {"video.playAfterSelection", IndexChoice, Video, 0, 0, 4},
    {"audio.karaokeDoubleConsonantApart", Check, Audio},
    {"audio.karaokeKfThreshold", Number, Audio, 10, 10000},
    {"notices.toasts", Check, General},
    {"updater.autoCheck", Check, Updates},
    {"updater.checkForStable", Check, Updates},
    {"updater.autoInstall", Check, Updates},
    {"updater.skippedVersion", Text, Updates},
    {"automation.fillLastAnswers", Check, Automation},
    {"video.previewSubtitlesAt", IndexChoice, Video, 0, 0, 2}, // 0 video resolution, 1 screen size
    {"styles.previewText", Text, Appearance},
    {"grammar.language", IndexChoice, Spelling, 0, 0, 5}, // 0 off, then Harper's four Englishes
    {"grammar.group.properNouns", Check, Spelling},
    {"grammar.group.initialisms", Check, Spelling},
    {"grammar.group.closedCompounds", Check, Spelling},
    {"grammar.group.phraseCorrections", Check, Spelling},
    {"grammar.group.capitalizationAndCase", Check, Spelling},
    {"grammar.group.punctuationAndSpacing", Check, Spelling},
    {"grammar.group.numbersAndUnits", Check, Spelling},
    {"grammar.group.pronounsAndPossession", Check, Spelling},
    {"grammar.group.compoundsAndHyphenation", Check, Spelling},
    {"grammar.group.spellingAndOrthography", Check, Spelling},
    {"grammar.group.styleAndRedundancy", Check, Spelling},
    {"grammar.group.grammarAndAgreement", Check, Spelling},
    {"grammar.group.wordChoiceAndUsage", Check, Spelling},
    {"grammar.group.regionalismsAndDialect", Check, Spelling},
    {"grammar.group.miscellaneous", Check, Spelling},
    {"privacy.usageData", Check, Updates},
    {"privacy.alwaysSendCrashReports", Check, Updates},
#ifdef _WIN32
    {"audio.outputHostApi", IndexChoice, Audio, 0, 0, 2},  // 0 WASAPI, 1 DirectSound
    {"video.playbackPlayer", IndexChoice, Video, 0, 0, 2}, // 0 built-in, 1 DirectShow
#endif
};

constexpr std::pair<OptionsPage, std::string_view> kPageIds[] = {
    {General, "general"},   {Appearance, "appearance"}, {LineEditor, "lineEditor"}, {Spelling, "spelling"},
    {Grid, "grid"},         {Video, "video"},           {Audio, "audio"},
    {Conversion, "conversion"}, {SubtitleProperties, "subtitleProperties"}, {Automation, "automation"},
    {Updates, "updates"},
};

constexpr std::string_view kFps[] = {"23.976", "24", "25", "29.97", "30", "60"};

char asciiLower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

bool sameIgnoringCase(const OptionsLists &lists, std::string_view a, std::string_view b)
{
    if (lists.sameIgnoringCase)
        return lists.sameIgnoringCase(a, b);
    return std::ranges::equal(a, b, {}, asciiLower, asciiLower);
}

// -1 for an empty text.
int findString(const OptionsLists &lists, const std::vector<std::string> &list, std::string_view text)
{
    if (text.empty())
        return -1;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (sameIgnoringCase(lists, list[i], text))
            return int(i);
    return -1;
}

// Past the list it changes nothing.
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
    case Renderer:
        return lists.renderers;
    default:
        return lists.styles;
    }
}

std::string externalFontsPath(std::string path, const OptionsLists &lists)
{
    if (lists.slashesForBackslashes)
        std::ranges::replace(path, '\\', '/');
    if (!path.empty() && path.back() != lists.pathSeparator)
        path.push_back(lists.pathSeparator);
    return path;
}

constexpr std::string_view kNotShown[] = {
    "scriptProperties.title",        "scriptProperties.titleOn",
    "scriptProperties.script",       "scriptProperties.scriptOn",
    "scriptProperties.translation",  "scriptProperties.translationOn",
    "scriptProperties.editing",      "scriptProperties.editingOn",
    "scriptProperties.timing",       "scriptProperties.timingOn",
    "scriptProperties.update",       "scriptProperties.updateOn",
    "scriptProperties.askForChange", "grid.loadSortedSubs",
    "grid.autoSelectLinesFromLastTab", "grid.dontCenterActiveLine",
    "grid.font",                     "editor.saveAfterCharacterCount",
    "automation.loadingMethod",      "automation.oldScriptsCompatibility",
    "convert.showSettings",
};

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

bool optionsBindingShown(std::string_view setting)
{
    return !std::ranges::contains(kNotShown, setting);
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
        out = std::min(out * 10 + (text[i] - '0'), limit + 1);
    out = negative ? -out : out;
    return std::clamp<std::int64_t>(out, std::numeric_limits<std::int32_t>::min(), limit);
}

std::int64_t numCtrlValue(std::string_view text, std::int64_t min, std::int64_t max)
{
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
    return std::int64_t(value);
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
                // MUST KILL: opening the dialog writes a setting
                settings.set(id, sel);
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
        case OutputDevice: {
            if (lists.outputDevices.empty())
                break; // not offered
            const auto it = std::ranges::find(lists.outputDevices, settings.text(id), &OutputDeviceChoice::id);
            state[id] = it == lists.outputDevices.end() ? std::int64_t{0} : std::int64_t(it - lists.outputDevices.begin());
            break;
        }
        case Renderer: {
            if (lists.renderers.size() < 2)
                break; // not offered
            const std::string &name = settings.text(id);
            auto it = std::ranges::find(lists.renderers, name);
            if (name.empty() || it == lists.renderers.end())
                it = std::ranges::find(lists.renderers, std::string("libass"));
            const auto sel = it == lists.renderers.end() ? 0 : std::int64_t(it - lists.renderers.begin());
            state[id] = select(std::int64_t{-1}, sel, lists.renderers.size());
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
        case OutputDevice: {
            const std::int64_t sel = intOf(state, id);
            if (sel < 0 || sel >= std::int64_t(lists.outputDevices.size()))
                break;
            const auto &choice = lists.outputDevices[std::size_t(sel)];
            if (settings.text(id) != choice.id) {
                write(id, choice.id);
                write("audio.outputDeviceName", choice.name);
            }
            break;
        }
        case Catalog:
        case Style:
        case Renderer: {
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
                    text = externalFontsPath(std::move(text), lists);
                write(id, text);
            }
            break;
        }
        case ZoomText: {
            const std::string text = textOf(state, id);
            if (zoomText(settings, id) != text) {
                if (text.empty()) {
                    settings.reset(id);
                    written.emplace_back(id);
                } else
                    write(id, legacyAtoi(text));
            }
            break;
        }
        case Number: {
            const std::int64_t n = std::clamp(intOf(state, id), b.min, b.max);
            if (legacyAtoi(settings.text(id)) != n)
                write(id, n);
            break;
        }
        }
    }
    return written;
}

OptionsState refreshOptionsDialogAfterReset(const Settings &settings, const OptionsLists &lists, OptionsState state,
                                            std::optional<OptionsPage> page)
{
    for (const auto &b : kBindings) {
        if (page && b.page != *page)
            continue;
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
            state[id] = zoomText(settings, id);
            break;
        case OutputDevice: {
            if (!state.contains(id))
                break; // not offered
            const auto it = std::ranges::find(lists.outputDevices, settings.text(id), &OutputDeviceChoice::id);
            state[id] = it == lists.outputDevices.end() ? std::int64_t{0} : std::int64_t(it - lists.outputDevices.begin());
            break;
        }
        case Renderer:
            if (!state.contains(id))
                break; // not offered
            if (settings.text(id).empty()) {
                const auto it = std::ranges::find(lists.renderers, std::string("libass"));
                if (it != lists.renderers.end()) {
                    state[id] = std::int64_t(it - lists.renderers.begin());
                    break;
                }
            }
            [[fallthrough]];
        case Language:
        case Dictionary:
        case Catalog:
        case Style:
            // MUST KILL: reset selects the entry at the atoi of the stored text
            state[id] = select(state[id], legacyAtoi(settings.text(id)), choiceList(lists, b.control).size());
            break;
        case IndexChoice:
            // MUST KILL: reset shows -1 for index choices, so OK writes -1
            state[id] = id == "video.moveToActiveLine" || id == "video.playAfterSelection"
                            ? SettingValue(settings.integer(id))
                            : SettingValue(std::int64_t{-1});
            break;
        case Font:
            state[id] = settings.text(id);
            state[std::string(b.sizeSetting)] = legacyAtoi(settings.text(b.sizeSetting));
            break;
        case Number:
            state[id] = std::clamp(legacyAtoi(settings.text(id)), b.min, b.max);
            break;
        }
    }
    return state;
}

std::string_view optionsPageId(OptionsPage page)
{
    for (const auto &[p, id] : kPageIds)
        if (p == page)
            return id;
    return {};
}

std::optional<OptionsPage> optionsPageOfId(std::string_view id)
{
    for (const auto &[p, pageId] : kPageIds)
        if (pageId == id)
            return p;
    return std::nullopt;
}

void chooseOptionsDialogCatalog(const OptionsLists &lists, OptionsState &state, int catalog)
{
    state["convert.styleCatalog"] = std::int64_t(catalog);
    if (!lists.styles.empty())
        state["convert.style"] = std::int64_t{0};
}

} // namespace hikari::application
