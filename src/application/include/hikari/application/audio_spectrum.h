#pragma once

// The audio spectrum's power lines (legacy HikariSub/AudioSpectrum.cpp and
// GFFT at 20d647c4), as aegisub.get_frequency_peaks reads them (L6). A line
// is the magnitude of the 1024 lowest of 2048 frequency bins of the 2048
// mono samples from line * 2048 (legacy's FFT over the audio's mixdown, no
// window, no overlap); the spectrum display (A2) is not part of this.

#include "hikari/application/audio_display.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hikari::application {

inline constexpr unsigned long kSpectrumLineLength = 1UL << 10; // legacy line_length
inline constexpr unsigned long kSpectrumDoubleLength = kSpectrumLineLength * 2; // legacy doublelen

using SpectrumLine = std::array<float, kSpectrumLineLength>;

// Legacy FFT::Transform then SpectrumCache::CreateCache for one line: the
// samples in legacy's 2048-point FFT (GFFT, float), each bin's magnitude.
SpectrumLine legacySpectrumLine(const std::int16_t *samples);

// Legacy AudioSpectrum::CreateRange(output, intensities, start, end,
// wxPoint(freqStart, freqEnd), peek) over `audio`, with legacy's integer
// arithmetic (`unsigned long` as the platform has it, R5-per-platform).
// With `peek` 0: one time per line from the line holding `start` to the one
// holding `end`, and the strongest bin in [freqStart, freqEnd] as an
// intensity (0-100 of a full-scale bin); otherwise the times where a run of
// lines reaching `peek` peaked, and no intensities. The caller checks the
// times as legacy did before (both at least 0, start before end); `audio`
// must have a sample rate of at least 2048 (legacy divided by it / 2048).
struct FrequencyPeaks {
    std::vector<int> times;
    std::vector<int> intensities;
};
FrequencyPeaks legacyFrequencyPeaks(const DisplayAudio &audio, long long timeStart, long long timeEnd, int freqStart,
                                    int freqEnd, int peek);

} // namespace hikari::application
