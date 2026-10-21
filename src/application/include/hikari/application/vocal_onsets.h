#pragma once

// KA5 (#280) vocal-onset snapping (KA5-onset-snap). Qt-free: where sung
// syllables start in the display audio, and the snap of a boundary to the
// nearest of them. The detection is band-limited spectral flux (the voice's
// 150-4000 Hz, log magnitudes, each bin against the louder of its neighbours
// two frames before, so vibrato moves little), its peaks over an adaptive
// threshold at least kOnsetMinGapMs apart, each taken back to the sharpest
// rise of the short-time energy just before it (librosa's onset_backtrack
// idea). It is unreliable on smooth singing, where one syllable glides into
// the next without a rise, which is why snapping is a toggle.

#include "hikari/application/audio_display.h"

#include <atomic>
#include <climits>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hikari::application {

inline constexpr int kOnsetHopMs = 10;      // the onsets' grid (legacy ZEROIT's)
inline constexpr int kOnsetMinGapMs = 50;   // two onsets closer than this are one
inline constexpr int kOnsetSnapWindowMs = 100; // a boundary snaps to an onset this near
// Audio past this is not looked at: a dummy audio's length is whatever its
// name says, and the detection keeps two numbers per 10 ms (about 35 MB here).
inline constexpr std::int64_t kOnsetMaxMs = std::int64_t(6) * 3600 * 1000;

// The onsets of the audio's decoded samples up to kOnsetMaxMs, in ms on the
// kOnsetHopMs grid, ascending. Nullopt once `cancel` is set (checked between
// blocks).
std::optional<std::vector<int>> detectVocalOnsets(const DisplayAudio &audio,
                                                  const std::atomic<bool> *cancel = nullptr);

// The onset in `onsetsMs` (ascending) nearest to `ms`, at most `windowMs`
// away and within [lowMs, highMs] (the earlier one on a tie); else `ms`.
int snapToOnset(std::span<const int> onsetsMs, int ms, int windowMs, int lowMs = INT_MIN, int highMs = INT_MAX);

} // namespace hikari::application
