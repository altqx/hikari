#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hikari::api {

std::string base64(std::string_view bytes);
// nullopt: not base64.
std::optional<std::string> fromBase64(std::string_view text);

}
