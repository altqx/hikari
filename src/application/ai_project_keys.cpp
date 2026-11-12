#include "hikari/application/ai_project_keys.h"

namespace hikari::application::ai {

std::optional<ProjectKey> resolveProjectKey(const KeyName &name, const EnvironmentLookup &environment,
                                            const std::map<std::string, std::string, std::less<>> &credentials)
{
    if (!name.env.empty())
        if (auto value = environment(name.env); value && !value->empty())
            return ProjectKey{KeySource::Environment, name.env, std::move(*value)};
    if (!name.entry.empty())
        if (const auto found = credentials.find(name.entry); found != credentials.end() && !found->second.empty())
            return ProjectKey{KeySource::CredentialsFile, name.entry, found->second};
    return std::nullopt;
}

}
