#pragma once

// The audio box's spectrum (A2; legacy HikariSub/AudioSpectrum.cpp and
// GFFT/GFFT.cpp at 20d647c4). Qt-free: legacy's 2048-point float FFT, its
// palette, and the image AudioSpectrum::RenderRange draws from the display
// audio (legacy Provider::GetBuffer's mixdown), with legacy's cache of
// transformed lines and its memory budget.

#include "hikari/application/audio_display.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hikari::application {

// Legacy GFFT.h: frequency components per line, half the transform's samples.
inline constexpr int kSpectrumLineLength = 1 << 10;
inline constexpr int kSpectrumDoubleLength = kSpectrumLineLength * 2;

// Legacy GFFT<2048, float>::fft: `data` holds 2048 complex values (re, im)
// and is transformed in place, with legacy's float arithmetic.
void legacyFft(float *data);

// Legacy FFT::Transform and SpectrumCache::CreateCache: the 1024 magnitudes
// of the transform of the 2048 samples at `samples`.
void legacySpectrumLine(const std::int16_t *samples, float *magnitudes);

// Legacy AudioSpectrum::ChangeColours: 256 RGB entries from the background
// (AUDIO_SPECTRUM_BACKGROUND), echo and inner colours (0xAARRGGBB). Entries
// 128 and up are all the inner colour.
std::array<std::uint8_t, 256 * 3> legacySpectrumPalette(std::uint32_t background, std::uint32_t echo,
                                                        std::uint32_t inner);

// Legacy PlaybackVolumeFromSlider: the volume slider's player volume, the
// cubic curve up to 50 and a linear 100% - 150% ramp above.
float playbackVolumeFromSlider(int position);

// Legacy AudioSpectrum with its sub-caches of transformed lines.
class AudioSpectrum {
public:
    static constexpr int kSubCacheLength = 16; // legacy orgsubcachelen
    static constexpr int kMaxOverlaps = 24;

    // `threads` stands in for legacy's worker count (the processors); it
    // sizes the cache budget and how many workers transform lines.
    explicit AudioSpectrum(int threads = 0);

    void setScaling(float powerScale) { m_powerScale = powerScale; } // legacy SetScaling (the vertical zoom)
    void setNonLinear(bool nonLinear) { m_nonLinear = nonLinear; }   // AUDIO_SPECTRUM_NON_LINEAR_ON
    void setColours(std::uint32_t background, std::uint32_t echo, std::uint32_t inner);

    // Legacy RenderRange: samples [rangeStart, rangeEnd) drawn into `w`
    // columns of `h` rows, BGRA with row 0 at the top (legacy wrote rows
    // bottom up from the lowest band); `pitch` is in pixels, alpha untouched.
    // `percent` is the horizontal zoom, which sets the overlaps.
    void render(const DisplayAudio &audio, std::int64_t rangeStart, std::int64_t rangeEnd, std::uint8_t *image, int w,
                int pitch, int h, int percent);

    // Legacy's overlaps for a render (newOverlaps).
    static int overlapsFor(int w, std::int64_t rangeStart, std::int64_t rangeEnd, int percent);
    // The first sample legacy transforms for line `line` at `overlaps`.
    static std::int64_t lineSample(std::uint64_t line, int overlaps);

    int overlaps() const { return m_overlaps; }
    int threads() const { return m_threads; }
    // The sub-caches kept (legacy sub_caches.size()) and the memory they hold.
    std::size_t cacheSlots() const { return m_slots.size(); }
    std::size_t cacheBytes() const;
    // Lines transformed since the spectrum was made (each cache miss is 16 x overlaps).
    std::uint64_t transformedLines() const { return m_transformed; }

private:
    struct SubCache {
        std::uint64_t index = ~std::uint64_t(0); // legacy start / subcachelen; none
        std::uint64_t used = 0;
        std::vector<float> lines; // subcachelen x 1024 magnitudes
    };
    const float *line(std::uint64_t line) const;
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

} // namespace hikari::application
