#pragma once

#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"

#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace hikari::application {

// In page order.
std::span<const std::string_view> gettingStartedSettings();

// Choices by index, appearance settings as stored; `imported` (by setting id)
// overrides `live`.
OptionsState gettingStartedState(const Settings &live, const OptionsLists &lists,
                                 const std::map<std::string, SettingValue, std::less<>> &imported);

// Page id: language, look, media, editing or updates.
std::string_view gettingStartedPageOf(std::string_view setting);

// Settings Finish leaves out of an import; a choice at index -1 stays the import's.
std::set<std::string, std::less<>> gettingStartedPageSettings(const OptionsState &pages);

}
