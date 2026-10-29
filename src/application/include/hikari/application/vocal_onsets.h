#pragma once

#include "hikari/application/audio_display.h"

#include <atomic>
#include <climits>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hikari::application {

inline constexpr int kOnsetHopMs = 10;
inline constexpr int kOnsetMinGapMs = 50;
inline constexpr int kOnsetSnapWindowMs = 100;
// Audio past this is ignored; a dummy audio can claim any length.
inline constexpr std::int64_t kOnsetMaxMs = std::int64_t(6) * 3600 * 1000;

// Ms on the kOnsetHopMs grid, ascending. Nullopt once `cancel` is set.
std::optional<std::vector<int>> detectVocalOnsets(const DisplayAudio &audio,
                                                  const std::atomic<bool> *cancel = nullptr);

// Nearest onset in ascending `onsetsMs` at most `windowMs` from `ms` and within
// [lowMs, highMs], the earlier on a tie; else `ms`.
int snapToOnset(std::span<const int> onsetsMs, int ms, int windowMs, int lowMs = INT_MIN, int highMs = INT_MAX);

} // namespace hikari::application
