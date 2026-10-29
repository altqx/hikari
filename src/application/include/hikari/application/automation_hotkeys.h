#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Resolves only while the recorded macro name and script hash still match.
struct MacroBinding {
    std::string legacyName;  // "Script <file name>-<ordinal>"
    std::string keys;        // portable key sequence ("Ctrl+Shift+A")
    std::string macroName;   // empty when imported
    std::string scriptSha256;
};

// "Script foo.lua-2" <-> the registry alias "foo.lua:2".
std::optional<std::string> aliasOfLegacyName(std::string_view legacyName);
std::string legacyNameOf(std::string_view fileName, int ordinal);

// Script lines of a HikariSub legacy Hotkeys.txt ("Script <file>-<k>=<accel>"),
// accelerators ("Ctrl-Shift-A") made portable ("Ctrl+Shift+A"); other lines skipped.
std::vector<MacroBinding> parseLegacyScriptHotkeys(std::string_view text);
std::string portableKeys(std::string_view wxAccelerator);

} // namespace hikari::application
