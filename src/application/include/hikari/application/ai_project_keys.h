#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::ai {

/// Where a Project's `[ai.keys]` says a provider's key is: an environment variable, an entry of
/// `.hikari/credentials.toml`, or both, the environment first. It never holds the key itself.
struct KeyName {
    std::string env;
    std::string entry;
    bool empty() const { return env.empty() && entry.empty(); }
    bool operator==(const KeyName &) const = default;
};

/// A Project's `[ai.keys]` by the user's provider id, project.local.toml's entries over project.toml's.
struct ProjectKeyNames {
    std::map<std::string, KeyName, std::less<>> byProvider;
    /// Providers whose `[ai.keys]` entry holds a value instead of a name, and project.toml's `[keys]` table as
    /// "keys"; the values are ignored.
    std::vector<std::string> valuesFound;
};

enum class KeySource { Environment, CredentialsFile };

struct ProjectKey {
    KeySource source;
    /// The variable or the entry it came from.
    std::string from;
    std::string key;
};

using EnvironmentLookup = std::function<std::optional<std::string>(std::string_view name)>;

/// The key `name` points at: the variable, else the credentials entry. Empty values don't count.
std::optional<ProjectKey> resolveProjectKey(const KeyName &name, const EnvironmentLookup &environment,
                                            const std::map<std::string, std::string, std::less<>> &credentials);

}
