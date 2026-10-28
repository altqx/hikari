#pragma once

// Resident memory of a running process (A33-resources: per-script helper
// cost). An observation for reports, not a budget check.

#include <cstdint>

namespace hikari::backends {

// Bytes resident in memory, or 0 when the process cannot be read.
std::int64_t processResidentBytes(std::int64_t pid);

} // namespace hikari::backends
