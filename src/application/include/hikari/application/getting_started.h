#pragma once

// FR1 (#363): Getting started, after MuseScore 4's first-launch setup: the
// settings most people change first, one page each, written at Finish. Here
// is what the pages set, and the pages' state while legacy settings are
// being imported: the imported values over the live ones, nothing written.

#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"

#include <functional>
#include <map>
#include <set>
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

// AN1 (#404): the page (its id: language, look, media, editing, updates)
// that sets `setting`, one of gettingStartedSettings().
std::string_view gettingStartedPageOf(std::string_view setting);

// The settings Finish leaves out of an import because the pages set them
// (the device's name goes with its id). A choice the pages cannot show
// (index -1: an imported dictionary that is not installed until the import
// takes effect) is not a page value and stays the import's.
std::set<std::string, std::less<>> gettingStartedPageSettings(const OptionsState &pages);

} // namespace hikari::application
