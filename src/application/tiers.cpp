#include "hikari/application/tiers.h"

#include <algorithm>
#include <array>

namespace hikari::application {

namespace {

bool has(const std::vector<std::u8string> &names, const std::u8string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

constexpr std::array kStores{
    TierStore{"styleCatalogs", TierRule::HiddenByName, ".hikari/catalogs"},
    TierStore{"fontCatalogs", TierRule::HiddenByName, ".hikari/catalogs/fonts.toml"},
    TierStore{"layouts", TierRule::HiddenByName, ".hikari/layouts.json"},
    TierStore{"shapes", TierRule::HiddenByName, ".hikari/shapes.toml"},
    TierStore{"tags", TierRule::HiddenByName, ".hikari/tags.toml"},
    TierStore{"dialogPresets", TierRule::HiddenByName, ".hikari/presets.toml"},
    TierStore{"shiftPresets", TierRule::HiddenByName, ".hikari/presets.toml"},
    TierStore{"postprocessorPresets", TierRule::HiddenByName, ".hikari/presets.toml"},
    TierStore{"hotkeys", std::nullopt, {}},
    TierStore{"aiProviders", std::nullopt, {}},
    TierStore{"aiThreads", std::nullopt, {}},
    TierStore{"recentFiles", std::nullopt, {}},
    TierStore{"windowState", std::nullopt, {}},
    TierStore{"hunspellDictionaries", std::nullopt, {}},
};

}

std::vector<TieredName> hideByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine)
{
    std::vector<TieredName> out;
    out.reserve(project.size() + mine.size());
    for (const auto &name : project)
        out.push_back({name, Tier::Project, false});
    for (const auto &name : mine)
        out.push_back({name, Tier::Mine, has(project, name)});
    return out;
}

std::optional<Tier> resolveByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine,
                                  const std::u8string &name)
{
    if (has(project, name))
        return Tier::Project;
    if (has(mine, name))
        return Tier::Mine;
    return std::nullopt;
}

std::optional<std::u8string> landingName(const std::vector<std::u8string> &taken, const std::u8string &name,
                                         const ClashChoice &choice)
{
    if (!has(taken, name))
        return name;
    switch (choice.action) {
    case ClashAction::Replace:
        return name;
    case ClashAction::Skip:
        return std::nullopt;
    case ClashAction::Rename:
        break;
    }
    const std::u8string base = choice.newName.empty() ? name : choice.newName;
    if (!has(taken, base))
        return base;
    for (int n = 2;; ++n) {
        const auto next = base + u8" (" + std::u8string(reinterpret_cast<const char8_t *>(std::to_string(n).c_str())) + u8")";
        if (!has(taken, next))
            return next;
    }
}

std::span<const TierStore> tierStores()
{
    return kStores;
}

const TierStore *tierStore(std::string_view id)
{
    const auto it = std::find_if(kStores.begin(), kStores.end(), [id](const TierStore &s) { return s.id == id; });
    return it == kStores.end() ? nullptr : &*it;
}

std::optional<std::filesystem::path> projectPathOf(std::string_view id, const std::filesystem::path &projectRoot)
{
    const auto *store = tierStore(id);
    if (!store || !store->rule || projectRoot.empty())
        return std::nullopt;
    return projectRoot / std::filesystem::path(std::u8string(store->projectPath.begin(), store->projectPath.end()));
}

}
