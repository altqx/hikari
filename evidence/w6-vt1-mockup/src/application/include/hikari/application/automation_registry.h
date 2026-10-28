#pragma once

// Macro identity for the automation manager (L1, S44-macro-alias;
// docs/qt/proposals/settings-import.md). A macro is identified by the host's
// registration: its script's path and content hash, its registration ordinal
// and its displayed name. The legacy `filename:ordinal` alias (what old
// bindings stored) resolves only when it is unambiguous and the registration
// still matches what was recorded; anything else stays visibly unresolved,
// never bound to the nearest name.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

struct MacroIdentity {
    std::string scriptPath;  // as loaded
    std::string scriptSha256;
    int ordinal = 0;         // registration order within the script, from 0
    std::string name;        // the displayed macro name
    std::string legacyAlias() const; // "<file name>:<ordinal>"
};

enum class AliasProblem {
    MalformedAlias,
    MissingScript,       // no loaded script has that file name
    BasenameCollision,   // several loaded scripts share it
    OrdinalOutOfRange,
    RegistrationChanged, // the recorded name or content differs from the current registration
};

struct AliasResolution {
    std::optional<MacroIdentity> identity;
    std::optional<AliasProblem> problem;
    std::vector<std::string> candidates; // script paths involved, for review
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
