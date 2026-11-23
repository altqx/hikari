#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

struct SettingLink {
    std::string setting; // empty: the window alone
    bool project = false;
};

// hikarisub://settings[/<setting id>][?scope=project|yours]; the scheme and host ignore case.
std::optional<SettingLink> parseSettingLink(std::string_view link);

}
