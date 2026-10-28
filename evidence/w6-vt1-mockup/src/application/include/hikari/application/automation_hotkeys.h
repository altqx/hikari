#pragma once

// Automation hotkeys (S2; legacy AutomationHotkeysDialog and Hotkeys at
// 20d647c4). A binding is stored under the legacy name "Script
// <file name>-<ordinal>" with the macro name and script hash it was made for,
// so the registry resolves it only while that registration still matches
// (S44-macro-alias); anything else stays visibly unresolved.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct MacroBinding {
    std::string legacyName;  // "Script <file name>-<ordinal>"
    std::string keys;        // portable key sequence ("Ctrl+Shift+A")
    std::string macroName;   // recorded with the binding (empty when imported)
    std::string scriptSha256;
};

// "Script foo.lua-2" <-> the registry alias "foo.lua:2".
std::optional<std::string> aliasOfLegacyName(std::string_view legacyName);
std::string legacyNameOf(std::string_view fileName, int ordinal);

// The script lines of a legacy Hotkeys.txt ("Script <file>-<k>=<accel>"),
// with wx accelerators ("Ctrl-Shift-A") written as portable sequences
// ("Ctrl+Shift+A"). Other lines are not automation hotkeys and are skipped.
std::vector<MacroBinding> parseLegacyScriptHotkeys(std::string_view text);
std::string portableKeys(std::string_view wxAccelerator);

} // namespace hikari::application
