#pragma once

#include <QString>

#include <span>
#include <string_view>

namespace hikari::ui {

// Display names only: hotkeys.toml, settings, command lookup and automation keep the English.
QString hotkeyCommandName(std::string_view english);
QString hotkeyWindowName(int type);
QString visualToolName(std::string_view english);

// The English sources lupdate extracts, one per name.
std::span<const char *const> hotkeyCommandSources();
std::span<const char *const> hotkeyWindowSources();
std::span<const char *const> visualToolSources();

}
