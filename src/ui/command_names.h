#pragma once

#include <QString>

#include <span>
#include <string_view>

namespace hikari::ui {

// Display names only: hotkeys.toml, settings, command lookup and automation keep the English.
QString hotkeyCommandName(std::string_view english);
QString hotkeyWindowName(int type);
// captureHotkey's refusal messages.
QString hotkeyRefusal(std::string_view english);
QString visualToolName(std::string_view english);
// A tool's option, value and notice texts; the tag and shape names users make stay as they are.
QString visualToolText(std::u16string_view english);

// The English sources lupdate extracts, one per name.
std::span<const char *const> hotkeyCommandSources();
std::span<const char *const> hotkeyWindowSources();
std::span<const char *const> hotkeyRefusalSources();
std::span<const char *const> visualToolSources();
std::span<const char *const> visualToolTextSources();

}
