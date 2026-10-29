#pragma once

#include "hikari/application/audio_display.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hikari::application {

inline constexpr int kSpectrumLineLength = 1 << 10;
inline constexpr int kSpectrumDoubleLength = kSpectrumLineLength * 2;

using SpectrumLine = std::array<float, kSpectrumLineLength>;

// `data` holds 2048 complex values (re, im), transformed in place.
void legacyFft(float *data);

// The 1024 magnitudes of the transform of the 2048 samples at `samples`.
void legacySpectrumLine(const std::int16_t *samples, float *magnitudes);
SpectrumLine legacySpectrumLine(const std::int16_t *samples);

// 0xAARRGGBB.
inline constexpr std::uint32_t kSpectrumBackgroundDefault = 0xFF000000;
inline constexpr std::uint32_t kSpectrumEchoDefault = 0xFF674FD7;
inline constexpr std::uint32_t kSpectrumInnerDefault = 0xFFF4F4F4;

// 256 RGB entries from 0xAARRGGBB colours; entries 128 and up are all `inner`.
std::array<std::uint8_t, 256 * 3> legacySpectrumPalette(std::uint32_t background, std::uint32_t echo,
                                                        std::uint32_t inner);

// With `peek` 0, one time per line and the band range's strongest bin as an
// intensity (0-100 of a full-scale bin); otherwise the times where a run of
// lines reaching `peek` peaked, and no intensities.
struct FrequencyPeaks {
    std::vector<int> times;
    std::vector<int> intensities;
};

class AudioSpectrum {
public:
    static constexpr int kSubCacheLength = 16;
    static constexpr int kMaxOverlaps = 24;

    // `threads` (0: the processors) sizes the cache budget and the workers.
    explicit AudioSpectrum(int threads = 0);

    void setScaling(float powerScale) { m_powerScale = powerScale; } // the vertical zoom
    void setNonLinear(bool nonLinear) { m_nonLinear = nonLinear; }
    void setColours(std::uint32_t background, std::uint32_t echo, std::uint32_t inner);

    // Samples [rangeStart, rangeEnd) into `w` columns of `h` rows, BGRA with
    // row 0 at the top; `pitch` is in pixels, alpha untouched. `percent` is the
    // horizontal zoom, which sets the overlaps.
    void render(const DisplayAudio &audio, std::int64_t rangeStart, std::int64_t rangeEnd, std::uint8_t *image, int w,
                int pitch, int h, int percent);

    // One line per 2048 samples from the line holding `timeStart` to the one
    // holding `timeEnd`; results follow the platform's `unsigned long`. The
    // caller checks both times are at least 0 and start before end. A sample
    // rate under 2048 gives no peaks.
    FrequencyPeaks createRange(const DisplayAudio &audio, long long timeStart, long long timeEnd, int freqStart,
                               int freqEnd, int peek);

    static int overlapsFor(int w, std::int64_t rangeStart, std::int64_t rangeEnd, int percent);
    static std::int64_t lineSample(std::uint64_t line, int overlaps);

    int overlaps() const { return m_overlaps; }
    int threads() const { return m_threads; }
    std::size_t cacheSlots() const { return m_slots.size(); }
    std::size_t cacheBytes() const;
    // Each cache miss adds 16 x overlaps.
    std::uint64_t transformedLines() const { return m_transformed; }

private:
    struct SubCache {
        std::uint64_t index = ~std::uint64_t(0); // ~0: none
        std::uint64_t used = 0;
        std::vector<float> lines; // kSubCacheLength x overlaps x 1024 magnitudes
    };
    const float *line(std::uint64_t line) const;
    void setOverlaps(int overlaps);
    // MUST KILL: sub-caches grow to three times a call's need and are never freed
    void reserveSlots(std::uint64_t startCache, std::uint64_t endCache);
    void fill(const DisplayAudio &audio, std::uint64_t startCache, std::uint64_t endCache);

    int m_threads = 1;
    int m_overlaps = 1;
    float m_powerScale = 1;
    bool m_nonLinear = false;
    std::array<std::uint8_t, 256 * 3> m_palette{};
    std::vector<SubCache> m_slots;
    std::unordered_map<std::uint64_t, std::size_t> m_byIndex;
    std::uint64_t m_clock = 0;
    std::uint64_t m_transformed = 0;
    std::vector<float> m_nullLine;
};

// createRange on a spectrum of its own, not the audio box's.
FrequencyPeaks legacyFrequencyPeaks(const DisplayAudio &audio, long long timeStart, long long timeEnd, int freqStart,
                                    int freqEnd, int peek);

} // namespace hikari::application
