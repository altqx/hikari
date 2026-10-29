#pragma once

#include <cstdint>

namespace hikari::backends {

// Bytes resident in memory, or 0 when the process cannot be read.
std::int64_t processResidentBytes(std::int64_t pid);

} // namespace hikari::backends
