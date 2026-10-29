#include "hikari/application/getting_started.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <variant>

namespace hikari::application {

namespace {

constexpr std::array<std::string_view, 15> kSettings{
    "program.language",
    "appearance.theme", "appearance.followSystem", "appearance.lightAccent", "appearance.darkAccent",
    "appearance.titleBar",
    "video.subtitleProvider", "audio.outputDevice",
    "autosave.maxFiles", "editor.spellchecker", "editor.dictionaryLanguage",
    "grammar.language",
    "updater.checkForStable", "updater.autoCheck", "privacy.usageData",
};

} // namespace

std::span<const std::string_view> gettingStartedSettings()
{
    return kSettings;
}

OptionsState gettingStartedState(const Settings &live, const OptionsLists &lists,
                                 const std::map<std::string, SettingValue, std::less<>> &imported)
{
    MemorySettingsStorage storage;
    Settings copy(storage);
    copy.set("audio.outputDeviceName", live.value("audio.outputDeviceName"));
    for (const std::string_view id : kSettings)
        copy.set(id, live.value(id));
    for (const auto &[id, value] : imported)
        if (std::ranges::contains(kSettings, id) || id == "audio.outputDeviceName")
            copy.set(id, value);
    const OptionsState opened = openOptionsDialog(copy, lists).state;
    OptionsState state;
    for (const std::string_view id : kSettings) {
        // MUST KILL: appearance.* settings bypass the Settings window's controls
        if (const auto it = opened.find(id); it != opened.end())
            state.emplace(id, it->second);
        else if (id.starts_with("appearance."))
            state.emplace(id, copy.value(id));
    }
    return state;
}

std::string_view gettingStartedPageOf(std::string_view setting)
{
    if (setting == "program.language")
        return "language";
    if (setting.starts_with("appearance."))
        return "look";
    if (setting.starts_with("video.") || setting.starts_with("audio."))
        return "media";
    if (setting.starts_with("updater.") || setting.starts_with("privacy."))
        return "updates";
    return "editing";
}

std::set<std::string, std::less<>> gettingStartedPageSettings(const OptionsState &pages)
{
    std::set<std::string, std::less<>> out;
    for (const std::string_view id : kSettings) {
        const auto it = pages.find(id);
        if (it == pages.end())
            continue;
        if (const auto *index = std::get_if<std::int64_t>(&it->second); index && *index < 0)
            continue;
        out.emplace(id);
        if (id == "audio.outputDevice")
            out.emplace("audio.outputDeviceName");
    }
    return out;
}

} // namespace hikari::application
