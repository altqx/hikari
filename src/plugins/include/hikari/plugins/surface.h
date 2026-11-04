#pragma once

#include "hikari/api/json.h"

#include <cstddef>
#include <optional>
#include <string>

namespace hikari::plugins {

// What one panel or dialog may hold; docs/plugins.md lists the kinds and their fields.
inline constexpr std::size_t kMaxNodes = 2000;
inline constexpr int kMaxDepth = 24;
inline constexpr std::size_t kMaxRows = 20000;
inline constexpr std::size_t kMaxDrawOps = 10000;

// Why a panel's widget tree is refused (English, naming the place), or nothing when it fits.
// Only a panel's root may carry a `menu`.
std::optional<std::string> panelTreeProblem(const api::Json &tree);
// The same for hikari.dialogs.show and ask: {title, body, buttons, width, height}.
std::optional<std::string> dialogProblem(const api::Json &spec);
// Each input's value by widget id, as the tree gives it or the input's empty value.
api::Json inputValues(const api::Json &tree);

}
