#pragma once

// FR1 (#363): Getting started, after MuseScore 4's first-launch setup: the
// settings most people change first, one page each, written at Finish. Here
// is what the pages set, and the pages' state while legacy settings are
// being imported: the imported values over the live ones, nothing written.

#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"

#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace hikari::application {

// The settings the pages set, in page order.
std::span<const std::string_view> gettingStartedSettings();

// The pages' state as the Settings window's controls hold it (choices by
// index, the appearance settings as stored): `live`'s values with
// `imported` (an import's values by setting id) over them. `live` is only
// read.
OptionsState gettingStartedState(const Settings &live, const OptionsLists &lists,
                                 const std::map<std::string, SettingValue, std::less<>> &imported);

} // namespace hikari::application
