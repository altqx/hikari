#pragma once

#include <string_view>

namespace hikari::core {

// Module identity only; subtitle behavior arrives with its own cards.
std::string_view moduleName() noexcept;

} // namespace hikari::core
