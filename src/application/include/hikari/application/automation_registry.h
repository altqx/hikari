#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

struct MacroIdentity {
    std::string scriptPath;
    std::string scriptSha256;
    int ordinal = 0; // from 0, in registration order
    std::string name;
    std::string legacyAlias() const; // "<file name>:<ordinal>"
};

enum class AliasProblem {
    MalformedAlias,
    MissingScript,       // no loaded script has that file name
    BasenameCollision,   // several loaded scripts share it
    OrdinalOutOfRange,
    RegistrationChanged, // the recorded name or hash differs
};

struct AliasResolution {
    std::optional<MacroIdentity> identity;
    std::optional<AliasProblem> problem;
    std::vector<std::string> candidates; // the script paths involved
};

class AutomationRegistry {
public:
    struct Script {
        std::string path;
        std::string sha256;
        std::vector<std::string> macros; // names in registration order
    };

    // Replaces any script with the same path.
    void setScript(Script script);
    void removeScript(const std::string &path);
    std::vector<MacroIdentity> macros() const; // load order, then registration order

    // `recorded`, when known, is what the binding stored alongside the alias
    // (name and hash); a mismatch is RegistrationChanged, not a silent rebind.
    AliasResolution resolve(const std::string &alias, const std::optional<MacroIdentity> &recorded = {}) const;

private:
    std::vector<Script> m_scripts;
};

} // namespace hikari::application
