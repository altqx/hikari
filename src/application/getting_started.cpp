#include "hikari/application/getting_started.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <variant>

namespace hikari::application {

namespace {

constexpr std::array<std::string_view, 14> kSettings{
    "program.language",                                             // Language
    "appearance.theme", "appearance.followSystem", "appearance.lightAccent", "appearance.darkAccent",
    "appearance.titleBar",                                          // Look
    "video.subtitleProvider", "audio.outputDevice",                 // Video and audio
    "autosave.maxFiles", "editor.spellchecker", "editor.dictionaryLanguage",
    "grammar.language",                                             // Editing
    "updater.checkForStable", "updater.autoCheck",                  // Updates
};

} // namespace

std::span<const std::string_view> gettingStartedSettings()
{
    return kSettings;
}

OptionsState gettingStartedState(const Settings &live, const OptionsLists &lists,
                                 const std::map<std::string, SettingValue, std::less<>> &imported)
{
    // Opening the Settings window over a copy turns the values into its
    // controls' state; the device's name goes with its id.
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
        // The window leaves out what it does not offer (one renderer);
        // the look's settings are not its controls and go as they are.
        if (const auto it = opened.find(id); it != opened.end())
            state.emplace(id, it->second);
        else if (id.starts_with("appearance."))
            state.emplace(id, copy.value(id));
    }
    return state;
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
