#include "hikari/application/automation_registry.h"

#include <algorithm>
#include <charconv>

namespace hikari::application {

namespace {

std::string fileNameOf(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::string MacroIdentity::legacyAlias() const
{
    return fileNameOf(scriptPath) + ":" + std::to_string(ordinal);
}

void AutomationRegistry::setScript(Script script)
{
    const auto it = std::find_if(m_scripts.begin(), m_scripts.end(),
                                 [&](const Script &s) { return s.path == script.path; });
    if (it != m_scripts.end())
        *it = std::move(script);
    else
        m_scripts.push_back(std::move(script));
}

void AutomationRegistry::removeScript(const std::string &path)
{
    std::erase_if(m_scripts, [&](const Script &s) { return s.path == path; });
}

std::vector<MacroIdentity> AutomationRegistry::macros() const
{
    std::vector<MacroIdentity> out;
    for (const auto &s : m_scripts)
        for (std::size_t i = 0; i < s.macros.size(); ++i)
            out.push_back({s.path, s.sha256, static_cast<int>(i), s.macros[i]});
    return out;
}

AliasResolution AutomationRegistry::resolve(const std::string &alias,
                                            const std::optional<MacroIdentity> &recorded) const
{
    AliasResolution result;
    const auto colon = alias.find_last_of(':');
    int ordinal = -1;
    if (colon == std::string::npos || colon == 0 ||
        std::from_chars(alias.data() + colon + 1, alias.data() + alias.size(), ordinal).ec != std::errc{} ||
        ordinal < 0) {
        result.problem = AliasProblem::MalformedAlias;
        return result;
    }
    const std::string file = alias.substr(0, colon);
    std::vector<const Script *> matches;
    for (const auto &s : m_scripts)
        if (fileNameOf(s.path) == file) {
            matches.push_back(&s);
            result.candidates.push_back(s.path);
        }
    if (matches.empty()) {
        result.problem = AliasProblem::MissingScript;
        return result;
    }
    if (matches.size() > 1) {
        result.problem = AliasProblem::BasenameCollision;
        return result;
    }
    const Script &script = *matches.front();
    if (static_cast<std::size_t>(ordinal) >= script.macros.size()) {
        result.problem = AliasProblem::OrdinalOutOfRange;
        return result;
    }
    MacroIdentity identity{script.path, script.sha256, ordinal, script.macros[static_cast<std::size_t>(ordinal)]};
    if (recorded && (recorded->name != identity.name ||
                     (!recorded->scriptSha256.empty() && recorded->scriptSha256 != identity.scriptSha256))) {
        result.problem = AliasProblem::RegistrationChanged;
        return result;
    }
    result.identity = std::move(identity);
    return result;
}

} // namespace hikari::application
