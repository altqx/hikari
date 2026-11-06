#pragma once

#include <string_view>

namespace hikari::plugin_helper {

// document.lua: hikari.document and hikari.selection, built on hikari.call.
std::string_view documentPrelude();
// ui.lua: hikari.panels and hikari.dialogs.
std::string_view uiPrelude();
// files.lua: hikari.files and hikari.net.
std::string_view filesPrelude();

}
