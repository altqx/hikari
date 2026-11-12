#pragma once

#include "hikari/application/clash_choice.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// ADR 0030, "Shared things with a Project tier".
enum class Tier { Project, Mine };
enum class TierRule { HiddenByName, Merged, PerKey };

struct TieredName {
    std::u8string name;
    Tier tier = Tier::Mine;
    // A Mine item that the Project's item of the same name hides.
    bool hidden = false;
    bool operator==(const TieredName &) const = default;
};

// The Project's names, then the user's, each in its own order.
std::vector<TieredName> hideByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine);
// The Project's tier when it has `name`.
std::optional<Tier> resolveByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine,
                                  const std::u8string &name);

// The name an item copied into a tier lands under, or none when the clash is skipped. A Rename
// already taken gets "<new> (2)"...
std::optional<std::u8string> landingName(const std::vector<std::u8string> &taken, const std::u8string &name,
                                         const ClashChoice &choice);

struct TierStore {
    std::string_view id;
    // None for a user-only store.
    std::optional<TierRule> rule;
    // Relative to the Project folder; empty for a user-only store.
    std::string_view projectPath;
};
std::span<const TierStore> tierStores();
const TierStore *tierStore(std::string_view id);
// Where the store keeps its Project copy; none for a user-only store or without a Project.
std::optional<std::filesystem::path> projectPathOf(std::string_view id, const std::filesystem::path &projectRoot);

}
