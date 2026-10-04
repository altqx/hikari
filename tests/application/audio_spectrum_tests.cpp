// A2: the audio box's spectrum, zoom and gain, pinned against legacy
// AudioSpectrum.cpp, GFFT/GFFT.cpp and AudioBox.cpp at 20d647c4. The pixel
// fixtures are worked out here from legacy RenderRange's formulas, column by
// column and row by row, over the transforms of the lines legacy reads; the
// transform itself is checked against a direct DFT.

#include "hikari/application/audio_playback.h"
#include "hikari/application/audio_spectrum.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <numbers>

using namespace hikari::application;

namespace {

// A deterministic test signal: a few tones and an LCG's noise, mono 48 kHz.
std::vector<std::int16_t> testSignal(std::int64_t count)
{
    std::vector<std::int16_t> out(static_cast<std::size_t>(count));
    std::uint32_t seed = 12345;
    for (std::int64_t i = 0; i < count; ++i) {
        seed = seed * 1103515245u + 12345u;
        const double t = double(i) / 48000.0;
        const double v = 6000 * std::sin(2 * std::numbers::pi * 440 * t) + 3000 * std::sin(2 * std::numbers::pi * 3150 * t) +
                         (double((seed >> 16) & 0x7FFF) - 16384) / 8;
        out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::lround(v));
    }
    return out;
}

DisplayAudio displayAudio(const std::vector<std::int16_t> &samples, std::int64_t count = -1)
{
    DisplayAudio audio(48000, count < 0 ? std::int64_t(samples.size()) : count);
    audio.appendFrames(samples.data(), std::int64_t(samples.size()), 1);
    audio.finish();
    return audio;
}

// Legacy RenderRange worked out pixel by pixel: the last line whose columns
// [w*k/n, w*(k+1)/n] hold x paints it; its bands per row (the largest
// magnitude), the intensity and the palette entry.
std::vector<std::uint8_t> expectedImage(const DisplayAudio &audio, std::int64_t rangeStart, std::int64_t rangeEnd,
                                        int w, int h, int percent, float scale, bool nonLinear,
                                        const std::array<std::uint8_t, 768> &palette)
{
    const int overlaps = AudioSpectrum::overlapsFor(w, rangeStart, rangeEnd, percent);
    const std::uint64_t first = std::uint64_t(overlaps * rangeStart / 2048);
    const std::uint64_t last = std::uint64_t(overlaps * rangeEnd / 2048);
    const std::uint64_t n = last - first + 1;
    std::vector<std::uint8_t> image(std::size_t(w) * h * 4, 0);
    std::vector<std::int16_t> samples(2048);
    std::vector<float> line(1024);
    const double upscale = scale * 16384 / 1024ul;
    const float factor = std::pow(1024ul, 1.f / (h - 1));
    for (int x = 0; x < w; ++x) {
        std::uint64_t owner = 0;
        for (std::uint64_t k = 0; k < n; ++k) {
            const int from = int(std::uint64_t(w) * k / n);
            const int to = std::min<int>(int(std::uint64_t(w) * (k + 1) / n), w - 1);
            if (from <= x && x <= to)
                owner = first + k;
        }
        audio.read(AudioSpectrum::lineSample(owner, overlaps), 2048, samples.data());
        legacySpectrumLine(samples.data(), line.data());
        // the bands of each row
        std::vector<std::pair<int, int>> bands;
        if (nonLinear) {
            int s1 = 0, s2 = 0;
            float counter = 1.f;
            for (int y = 0; y < h; ++y) {
                s2 = s2 >= int(counter) ? s2 + 1 : int(counter);
                counter *= factor;
                s1 = std::min(s1, 1023);
                s2 = std::min(s2, 1023);
                bands.emplace_back(s1, s2);
                s1 = s2 + 1;
            }
        } else {
            for (int y = 0; y < h; ++y)
                bands.emplace_back(1024 * y / h, std::min(1023, 1024 * (y + 1) / h));
        }
        for (int y = 0; y < h; ++y) {
            float maxval = 0;
            for (int b = bands[std::size_t(y)].first; b <= bands[std::size_t(y)].second; ++b)
                maxval = std::max(maxval, line[std::size_t(b)]);
            const int intensity = std::clamp(int(256 * (maxval * upscale) / 8388608), 0, 255);
            std::uint8_t *p = image.data() + (std::size_t(h - y - 1) * w + x) * 4;
            p[2] = palette[std::size_t(intensity) * 3];
            p[1] = palette[std::size_t(intensity) * 3 + 1];
            p[0] = palette[std::size_t(intensity) * 3 + 2];
        }
    }
    return image;
}

std::vector<std::uint8_t> rendered(AudioSpectrum &spectrum, const DisplayAudio &audio, std::int64_t start,
                                   std::int64_t end, int w, int h, int percent)
{
    std::vector<std::uint8_t> image(std::size_t(w) * h * 4, 0);
    spectrum.render(audio, start, end, image.data(), w, w, h, percent);
    return image;
}

const auto kPalette = legacySpectrumPalette(0xFF000000, 0xFF674FD7, 0xFFF4F4F4);

} // namespace

// Legacy GFFT<2048, float>: the magnitudes of a direct DFT, to float's precision.
TEST(AudioSpectrum, TheTransformIsADft)
{
    const auto signal = testSignal(2048);
    std::vector<float> line(1024);
    legacySpectrumLine(signal.data(), line.data());
    double largest = 0;
    for (int k = 0; k < 1024; ++k) {
        std::complex<double> sum = 0;
        for (int i = 0; i < 2048; ++i)
            sum += double(signal[std::size_t(i)]) * std::polar(1.0, -2 * std::numbers::pi * k * i / 2048);
        largest = std::max(largest, std::abs(sum));
        EXPECT_NEAR(line[std::size_t(k)], std::abs(sum), 1e-4 * std::abs(sum) + 64) << "bin " << k;
    }
    EXPECT_GT(largest, 1e6);
}

// A tone on bin 64 (1500 Hz at 48 kHz) has the magnitude A x 1024 there and
// next to nothing elsewhere; no window is applied.
TEST(AudioSpectrum, ABinCentredToneHasItsAmplitudeTimes1024)
{
    std::vector<std::int16_t> tone(2048);
    for (int i = 0; i < 2048; ++i)
        tone[std::size_t(i)] = static_cast<std::int16_t>(std::lround(201 * std::cos(2 * std::numbers::pi * 64 * i / 2048)));
    std::vector<float> line(1024);
    legacySpectrumLine(tone.data(), line.data());
    EXPECT_NEAR(line[64], 201 * 1024, 300);
    EXPECT_LT(line[63], 400);
    EXPECT_LT(line[65], 400);
    EXPECT_NEAR(line[0], 0, 400);
}

// Legacy ChangeColours: background to echo over the first half, echo to inner
// up to 128, the inner colour from there on.
TEST(AudioSpectrum, ThePaletteStopsAtTheInnerColourHalfway)
{
    auto rgb = [](int j) {
        return std::array<int, 3>{kPalette[std::size_t(j) * 3], kPalette[std::size_t(j) * 3 + 1], kPalette[std::size_t(j) * 3 + 2]};
    };
    EXPECT_EQ(rgb(0), (std::array<int, 3>{0, 0, 0}));
    EXPECT_EQ(rgb(32), (std::array<int, 3>{51, 39, 107}));   // half of #674FD7, truncated
    EXPECT_EQ(rgb(64), (std::array<int, 3>{0x67, 0x4F, 0xD7}));
    EXPECT_EQ(rgb(96), (std::array<int, 3>{173, 161, 229}));  // halfway to #F4F4F4
    for (int j : {128, 129, 200, 255})
        EXPECT_EQ(rgb(j), (std::array<int, 3>{0xF4, 0xF4, 0xF4})) << j;
}

// Legacy newOverlaps and SpectrumCache::CreateCache's line positions.
TEST(AudioSpectrum, OverlapsAndLinePositions)
{
    // 500 columns of 1440 samples (zoom 50 at 48 kHz): ceil(2048 / 1440 x 0.5) + 1
    EXPECT_EQ(AudioSpectrum::overlapsFor(500, 0, 500 * 1440, 50), 2);
    EXPECT_EQ(AudioSpectrum::overlapsFor(500, 0, 500 * 11520, 100), 1);
    EXPECT_EQ(AudioSpectrum::overlapsFor(500, 0, 500 * 288, 10), 8);
    EXPECT_EQ(AudioSpectrum::overlapsFor(500, 0, 500 * 1, 1), 24);
    // whole multiples of 16 x 2048 per sub-cache, 2048 / overlaps (truncated) apart in one
    EXPECT_EQ(AudioSpectrum::lineSample(0, 3), 0);
    EXPECT_EQ(AudioSpectrum::lineSample(1, 3), 682);
    EXPECT_EQ(AudioSpectrum::lineSample(47, 3), 47 * 682);
    EXPECT_EQ(AudioSpectrum::lineSample(48, 3), 32768); // not 48 x 682
    EXPECT_EQ(AudioSpectrum::lineSample(17, 1), 17 * 2048);
}

// RenderRange's pixels at zooms 100, 50 and 10 (overlaps 1, 2 and 8), in the
// linear and the speech (non-linear) band layouts, at another scale, from a
// later position and past the end of the audio.
TEST(AudioSpectrum, RendersLegacyPixels)
{
    const auto signal = testSignal(48000 * 6);
    const auto audio = displayAudio(signal);
    struct Case {
        std::int64_t start;
        int w, h, samples, percent;
        float scale;
        bool nonLinear;
    };
    for (const Case c : {Case{0, 120, 90, 2880, 100, 1.f, false}, Case{0, 120, 90, 1440, 50, 1.f, true},
                         Case{96000, 100, 64, 288, 10, 1.f, false}, Case{48000, 80, 150, 288, 10, 0.216f, true},
                         Case{48000 * 5, 100, 40, 1440, 50, 8.f, false}}) {
        SCOPED_TRACE(testing::Message() << "zoom " << c.percent << " start " << c.start << " h " << c.h);
        AudioSpectrum spectrum(3);
        spectrum.setScaling(c.scale);
        spectrum.setNonLinear(c.nonLinear);
        const std::int64_t end = c.start + std::int64_t(c.w) * c.samples;
        const auto got = rendered(spectrum, audio, c.start, end, c.w, c.h, c.percent);
        const auto want = expectedImage(audio, c.start, end, c.w, c.h, c.percent, c.scale, c.nonLinear, kPalette);
        ASSERT_EQ(got.size(), want.size());
        int differences = 0;
        for (std::size_t i = 0; i < got.size(); ++i)
            differences += got[i] != want[i];
        EXPECT_EQ(differences, 0);
        // the cache serves a second render of the same view without transforming anything
        const auto lines = spectrum.transformedLines();
        EXPECT_EQ(rendered(spectrum, audio, c.start, end, c.w, c.h, c.percent), got);
        EXPECT_EQ(spectrum.transformedLines(), lines);
    }
}

// A bin-centred tone of amplitude 201 at the default scale: 256 x 201 x 1024
// x 16 / 8388608 = 100.5, so the rows holding bin 64 take palette entry 100;
// silence past the audio is the background.
TEST(AudioSpectrum, AToneLightsItsBands)
{
    std::vector<std::int16_t> tone(48000);
    for (int i = 0; i < 48000; ++i)
        tone[std::size_t(i)] = static_cast<std::int16_t>(std::lround(201 * std::cos(2 * std::numbers::pi * 64 * i / 2048)));
    const auto audio = displayAudio(tone, 96000);
    AudioSpectrum spectrum(2);
    const int w = 40, h = 128; // 8 bins a row: rows 7 ([56, 64]) and 8 ([64, 72]) hold bin 64
    const auto image = rendered(spectrum, audio, 0, std::int64_t(w) * 2400, w, h, 100);
    auto pixel = [&](int x, int y) {
        const std::uint8_t *p = image.data() + (std::size_t(y) * w + x) * 4;
        return std::array<int, 3>{p[2], p[1], p[0]};
    };
    const std::array<int, 3> entry100{kPalette[300], kPalette[301], kPalette[302]};
    for (int x : {2, 10, 15}) {
        EXPECT_EQ(pixel(x, h - 1 - 7), entry100) << x;
        EXPECT_EQ(pixel(x, h - 1 - 8), entry100) << x;
        EXPECT_EQ(pixel(x, h - 1 - 20), (std::array<int, 3>{0, 0, 0})) << x;
    }
    // columns 20 and up (from 1 s) are past the tone: all background
    for (int y : {0, h - 1 - 7, h - 1 - 8})
        EXPECT_EQ(pixel(30, y), (std::array<int, 3>{0, 0, 0})) << y;
}

// A display 1024 rows or taller interpolates between bands; legacy read band
// 1024 (past the line) for the top row. R3, proposed A2-tall-spectrum: that
// band reads as zero.
TEST(AudioSpectrum, ATallDisplayInterpolatesAndReadsNothingPastTheLastBand)
{
    const auto signal = testSignal(48000);
    const auto audio = displayAudio(signal);
    AudioSpectrum spectrum(2);
    const int w = 4, h = 1100;
    const auto image = rendered(spectrum, audio, 0, std::int64_t(w) * 2880, w, h, 100);
    // the top row (y = h - 1 from the bottom): ideal 1024, both neighbours past the end
    for (int x = 0; x < w; ++x)
        EXPECT_EQ(image[std::size_t(x) * 4 + 2], kPalette[0]);
    // column 0 is drawn last by line 1 (columns 4 x 1 / 6 = 0 to 4 x 2 / 6 = 1),
    // and row 549 is ideal 512 exactly: band 512 alone
    std::vector<std::int16_t> samples(2048);
    std::vector<float> line(1024);
    audio.read(2048, 2048, samples.data());
    legacySpectrumLine(samples.data(), line.data());
    const int y = 549;
    const float ideal = (float)(y + 1.) / h * 1024;
    const float frac = ideal - std::floor(ideal);
    const int intensity = std::clamp(
        int(((1 - frac) * line[std::size_t(std::floor(ideal))] + frac * line[std::size_t(std::ceil(ideal))]) * 16.0 /
            8388608 * 256),
        0, 255);
    EXPECT_EQ(image[(std::size_t(h - y - 1) * w) * 4 + 2], kPalette[std::size_t(intensity) * 3]);
}

// Legacy's cache budget: three times the sub-caches a render needs (its span
// plus one per worker), reused while the view scrolls; scrolling half an
// hour of audio never grows it.
TEST(AudioSpectrum, ScrollingLongAudioKeepsTheCacheBudget)
{
    const auto audio = DisplayAudio::silence(48000, std::int64_t(48000) * 1800);
    AudioSpectrum spectrum(4);
    const int w = 500, h = 100, samples = 1440, percent = 50; // overlaps 2: 32 lines of 2048 / 2 a sub-cache
    std::vector<std::uint8_t> image(std::size_t(w) * h * 4);
    spectrum.render(audio, 0, std::int64_t(w) * samples, image.data(), w, w, h, percent);
    const std::size_t slots = spectrum.cacheSlots();
    // lines 0 .. 703 (2 x 720000 / 2048) are sub-caches 0 .. 21: (21 + 4) x 3
    EXPECT_EQ(slots, std::size_t((21 + 4) * 3));
    const std::size_t bytes = spectrum.cacheBytes();
    EXPECT_LE(bytes, slots * 32 * 1024 * sizeof(float));
    std::uint64_t before = spectrum.transformedLines();
    for (std::int64_t position = 0; position + w < 1800 * 48000 / samples; position += 450) {
        spectrum.render(audio, position * samples, (position + w) * samples, image.data(), w, w, h, percent);
        ASSERT_EQ(spectrum.cacheSlots(), slots);
        ASSERT_LE(spectrum.cacheBytes(), slots * 32 * 1024 * sizeof(float));
    }
    // every step past the first transformed new lines (the view moved on)
    EXPECT_GT(spectrum.transformedLines(), before + 1000);
    // a zoom with more overlaps starts over: the cached lines are dropped
    before = spectrum.transformedLines();
    spectrum.render(audio, 0, std::int64_t(w) * 288, image.data(), w, w, h, 10);
    EXPECT_EQ(spectrum.overlaps(), 8);
    EXPECT_GT(spectrum.transformedLines(), before);
}

// Legacy AudioDisplayScaleFromSlider and PlaybackVolumeFromSlider: both
// cubic up to 50 (100%), the player then linear to 150%, the scale cubic to 800%.
TEST(AudioGain, SliderCurves)
{
    EXPECT_FLOAT_EQ(audioScaleFromSlider(50), 1.f);
    EXPECT_FLOAT_EQ(audioScaleFromSlider(100), 8.f);
    EXPECT_FLOAT_EQ(audioScaleFromSlider(25), 0.125f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(25), 0.125f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(50), 1.f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(75), 1.25f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(100), 1.5f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(1), std::pow(1.f / 50.f, 3.f));
}

// Legacy DoUpdateImage in spectrum mode: the picture over the background,
// no waveform and no inactive Lines' waveform; the rest drawn over it.
TEST(AudioScene, SpectrumReplacesTheWaveform)
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(200, 100, 22, 16);
    WaveformColumns columns;
    columns.min.assign(200, 60);
    columns.peak.assign(200, 40);
    AudioMarks marks;
    marks.startMs = 1000;
    marks.endMs = 2000;
    marks.inactive = {{2500, 3000}};
    const AudioDisplayOptions options;
    auto picture = std::make_shared<AudioImage>();
    picture->width = 200;
    picture->height = 100;
    picture->bgra.assign(200 * 100 * 4, 0xFF);
    const auto scene = audioScene(view, columns, marks, options, [](AudioShape::Font, std::string_view) { return 30; }, picture);
    ASSERT_GE(scene.size(), 2u);
    EXPECT_EQ(scene[0].kind, AudioShape::Kind::Fill);
    EXPECT_EQ(scene[1].kind, AudioShape::Kind::Image);
    EXPECT_EQ(scene[1].image, picture);
    EXPECT_EQ(scene[1].x2, 200.f);
    for (const auto &s : scene) {
        EXPECT_FALSE(s.kind == AudioShape::Kind::Line &&
                     (s.colour == options.waveformInactive || s.colour == options.waveformSelected));
    }
    std::size_t shades = 0;
    for (const auto &s : scene)
        shades += s.kind == AudioShape::Kind::Fill && s.colour == options.inactiveBackground;
    EXPECT_EQ(shades, 2u);
    // without a picture the waveform is drawn as before
    const auto waveform = audioScene(view, columns, marks, options, [](AudioShape::Font, std::string_view) { return 30; });
    std::size_t lines = 0;
    for (const auto &s : waveform)
        lines += s.kind == AudioShape::Kind::Line && s.colour == options.waveform;
    EXPECT_GE(lines, 150u);
}
