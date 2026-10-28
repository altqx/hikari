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
// page, conversion, video, the two audio pages. Each binding names the PF1
// page that shows it now.
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
    // EditorAdvanced.
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
    // video.
    {"video.fullScreenOnStart", Check, Video},
    {"video.pauseOnClick", Check, Video},
    {"video.openAtActiveLine", Check, Video},
    {"video.gpuConversion", Check, Video},
    {"video.acceptedAudioStream", Text, Video},
    {"video.ffms2Seeking", IndexChoice, Video, 0, 0, 4},
    {"video.zoomPercent", ZoomText, Video},
    // W2: VSFILTER_INSTANCE, "Subtitle display filter", bound last
    // (OptionsDialog.cpp:616-638); offered only with a CSRI renderer.
    {"video.subtitleProvider", Renderer, Video},
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
    {"audio.delay", Number, Audio, -50000000, 50000000},
    {"audio.markPlayTime", Number, Audio, 400, 5000},
    {"audio.lineBoundariesThickness", Number, Audio, 1, 5},
    {"audio.inactiveLinesDisplayMode", IndexChoice, Audio, 0, 0, 3},
    {"audio.cacheFilesLimit", Number, Audio, 0, 10000},
    {"audio.leadInValue", Number, Audio, 0, 10000},
    {"audio.leadOutValue", Number, Audio, 0, 10000},
    // The rewrite's own, after legacy's. Y4-playres-ask: a missing script
    // resolution a macro reads is set from the video without asking, on the
    // Video page's Script resolution section.
    {"automation.scriptResolutionFromVideo", Check, Video},
    // GR3: empty Grid columns hide themselves, on the Subtitle grid page.
    {"grid.autoHideColumns", Check, Grid},
    // H2-output-device (decision 60): the device both audio paths play
    // through, on the Audio page; empty follows the system default.
    {"audio.outputDevice", OutputDevice, Audio},
    // LE2: the Times/Frames switch (EDITBOX_TIMES_TO_FRAMES_SWITCH), which
    // left the Line editor for Settings and the Video panel's menu.
    {"editor.timesToFramesSwitch", Check, LineEditor},
    // LE3-tags-shown: the Line editor's hidden-tag view (shown by default).
    {"editor.hideTags", Check, LineEditor},
    // WS2: the panel layout in the subtitle file ([Hikari]), on the
    // Subtitle properties page with the other things a save writes there.
    {"subtitles.layoutInFile", Check, SubtitleProperties},
    // VF1: legacy VideoToolbar's two lists (MOVE_VIDEO_TO_ACTIVE_LINE,
    // VIDEO_PLAY_AFTER_SELECTION), which left the Video panel for its menu
    // and the Video page.
    {"video.moveToActiveLine", IndexChoice, Video, 0, 0, 6},
    {"video.playAfterSelection", IndexChoice, Video, 0, 0, 4},
    // KA1: Auto split's double consonants and the \k and \kf switch's long
    // syllable, on the Audio page after the karaoke options.
    {"audio.karaokeDoubleConsonantApart", Check, Audio},
    {"audio.karaokeKfThreshold", Number, Audio, 10, 10000},
    // PK1 (#301): notices as toasts, or in the Log only, on the General
    // page's Notices section.
    {"notices.toasts", Check, General},
    // PU7 (#309): the update window's three options and the version "Skip
    // this version" hid, on the Updates page.
    {"updater.autoCheck", Check, Updates},
    {"updater.checkForStable", Check, Updates},
    {"updater.autoInstall", Check, Updates},
    {"updater.skippedVersion", Text, Updates},
    // AD2 (#311): "Fill in my last answers", the Automation page's Script
    // dialogs section.
    {"automation.fillLastAnswers", Check, Automation},
    // SV1 (#365): "Preview subtitles at" (0 Video resolution, 1 Screen
    // size), the Video page's Subtitles on video section.
    {"video.previewSubtitlesAt", IndexChoice, Video, 0, 0, 2},
    // PV1 (#398): the style and font samples' text (STYLE_PREVIEW_TEXT,
    // before only the Style editor's preview), the Appearance page's Font
    // section.
    {"styles.previewText", Text, Appearance},
    // GR1 (#364): grammar check (Off, then Harper's four Englishes) and
    // Harper's rule groups, on the Spelling & grammar page.
    {"grammar.language", IndexChoice, Spelling, 0, 0, 5},
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
    // AN1 (#404): "Send anonymous usage data", the Updates and privacy
    // page's Privacy section.
    {"privacy.usageData", Check, Updates},
    // CR1 (#452): "Always send crash reports", the same section.
    {"privacy.alwaysSendCrashReports", Check, Updates},
#ifdef _WIN32
    // The Windows output's host API
    // (A4-wasapi-default: 0 WASAPI, 1 DirectSound), on the Audio page.
    {"audio.outputHostApi", IndexChoice, Audio, 0, 0, 2},
    // W1: the video player (0 built-in, 1 DirectShow), on the Video page.
    {"video.playbackPlayer", IndexChoice, Video, 0, 0, 2},
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
    case Renderer:
        return lists.renderers;
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

// #383: the options nothing reads.
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
        case OutputDevice: {
            if (lists.outputDevices.empty())
                break; // not offered
            // The stored id's entry; System default (the first) for an id
            // the list does not have.
            const auto it = std::ranges::find(lists.outputDevices, settings.text(id), &OutputDeviceChoice::id);
            state[id] = it == lists.outputDevices.end() ? std::int64_t{0} : std::int64_t(it - lists.outputDevices.begin());
            break;
        }
        case Renderer: {
            if (lists.renderers.size() < 2)
                break; // not offered
            // vsfilters.Index(name). Unset reads as libass (W2-libass-default:
            // CSRI is chosen explicitly), and so does a name that is not
            // listed (W2-missing-renderer-libass; legacy showed the first
            // entry, its default CSRI renderer): the choice shows what draws,
            // and OK never switches to a CSRI renderer nobody chose.
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
            // The entry's id, with its name beside it (shown while the device
            // is not connected, and PortAudio's name for it).
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
                    text = externalFontsPath(std::move(text), lists); // Y6: the fonts reload on the change
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
            // Cast to HikariTextCtrl: its SetValue (not virtual) shows the raw text.
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
                // The default is unset, which is libass (W2: CSRI is chosen
                // explicitly). Legacy's SetSelection(GetInt("")) showed the
                // first entry, its default CSRI renderer, and OK would then
                // switch to it; show libass as the opening does.
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
            // The inverted id test sends these to SetSelection(GetInt(text)).
            state[id] = select(state[id], legacyAtoi(settings.text(id)), choiceList(lists, b.control).size());
            break;
        case IndexChoice:
            // SetSelection(FindString(GetString(option))): the option's digits
            // name no entry, so the choice shows nothing (-1). VF1's lists
            // were never on legacy's dialog: they show their default.
            state[id] = id == "video.moveToActiveLine" || id == "video.playAfterSelection"
                            ? SettingValue(settings.integer(id))
                            : SettingValue(std::int64_t{-1});
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
    // Stylelist->Clear() keeps the selection; SetSelection(0) needs an entry.
    if (!lists.styles.empty())
        state["convert.style"] = std::int64_t{0};
}

} // namespace hikari::application
