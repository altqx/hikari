#include "hikari/application/audio_spectrum.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace hikari::application {

namespace {

// Legacy GFFT.cpp (after the Dr. Dobb's "simple and efficient FFT"): the
// sines come from a Taylor series evaluated with legacy's mix of float and
// double, and the butterflies run in float. Ported unchanged, so the
// magnitudes are legacy's to the bit.
constexpr float kPi = 3.1415926535897932384626433832795f;

template <unsigned M, unsigned N, unsigned B, unsigned A> struct SinCosSeries {
    static double value() { return 1 - (A * kPi / B) * (A * kPi / B) / M / (M + 1) * SinCosSeries<M + 2, N, B, A>::value(); }
};
template <unsigned N, unsigned B, unsigned A> struct SinCosSeries<N, N, B, A> {
    static double value() { return 1.; }
};

template <unsigned B, unsigned A> struct Sin {
    static float value() { return (A * kPi / B) * SinCosSeries<2, 24, B, A>::value(); }
};

template <unsigned N> class DanielsonLanczos {
    DanielsonLanczos<N / 2> next;

public:
    void apply(float *data)
    {
        next.apply(data);
        next.apply(data + N);

        float wtemp, tempr, tempi, wr, wi, wpr, wpi;
        wtemp = -Sin<N, 1>::value();
        wpr = -2.0 * wtemp * wtemp;
        wpi = -Sin<N, 2>::value();
        wr = 1.0;
        wi = 0.0;
        for (unsigned i = 0; i < N; i += 2) {
            tempr = data[i + N] * wr - data[i + N + 1] * wi;
            tempi = data[i + N] * wi + data[i + N + 1] * wr;
            data[i + N] = data[i] - tempr;
            data[i + N + 1] = data[i + 1] - tempi;
            data[i] += tempr;
            data[i + 1] += tempi;

            wtemp = wr;
            wr += wr * wpr - wi * wpi;
            wi += wi * wpr + wtemp * wpi;
        }
    }
};

template <> class DanielsonLanczos<4> {
public:
    void apply(float *data)
    {
        float tr = data[2];
        float ti = data[3];
        data[2] = data[0] - tr;
        data[3] = data[1] - ti;
        data[0] += tr;
        data[1] += ti;
        tr = data[6];
        ti = data[7];
        data[6] = data[5] - ti;
        data[7] = tr - data[4];
        data[4] += tr;
        data[5] += ti;

        tr = data[4];
        ti = data[5];
        data[4] = data[0] - tr;
        data[5] = data[1] - ti;
        data[0] += tr;
        data[1] += ti;
        tr = data[6];
        ti = data[7];
        data[6] = data[2] - tr;
        data[7] = data[3] - ti;
        data[2] += tr;
        data[3] += ti;
    }
};

// Legacy GFFT::scramble: reverse-binary reindexing of `nn` complex values.
void scramble(float *data, unsigned long nn)
{
    unsigned long n, m, j, i;
    n = nn << 1;
    j = 1;
    for (i = 1; i < n; i += 2) {
        if (j > i) {
            std::swap(data[j - 1], data[i - 1]);
            std::swap(data[j], data[i]);
        }
        m = nn;
        while (m >= 2 && j > m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

std::uint8_t red(std::uint32_t argb) { return std::uint8_t(argb >> 16); }
std::uint8_t green(std::uint32_t argb) { return std::uint8_t(argb >> 8); }
std::uint8_t blue(std::uint32_t argb) { return std::uint8_t(argb); }

} // namespace

void legacyFft(float *data)
{
    scramble(data, kSpectrumDoubleLength);
    DanielsonLanczos<kSpectrumDoubleLength>().apply(data);
}

void legacySpectrumLine(const std::int16_t *samples, float *magnitudes)
{
    float output[kSpectrumDoubleLength * 2];
    for (int i = 0; i < kSpectrumDoubleLength; i++) {
        output[i * 2] = float(samples[i]);
        output[(i * 2) + 1] = 0.f;
    }
    legacyFft(output);
    for (int j = 0; j < kSpectrumLineLength; ++j) {
        const int g = (j * 2) + 1;
        magnitudes[j] = std::sqrt(output[j * 2] * output[j * 2] + output[g] * output[g]);
    }
}

// Legacy AudioSpectrum::ChangeColours.
std::array<std::uint8_t, 256 * 3> legacySpectrumPalette(std::uint32_t background, std::uint32_t echo,
                                                        std::uint32_t inner)
{
    const float r2 = red(inner), r1 = red(echo), r = red(background);
    const float g2 = green(inner), g1 = green(echo), g = green(background);
    const float b2 = blue(inner), b1 = blue(echo), b = blue(background);
    std::array<std::uint8_t, 256 * 3> palette{};
    const float div = (1.f / 128.f);
    float i = 0;
    for (int j = 0; j < 256; j++) {
        const int pointr = (i < 0.5f) ? (r - ((r - r1) * (i * 2))) : (r1 - ((r1 - r2) * ((i * 2) - 1.f)));
        const int pointg = (i < 0.5f) ? (g - ((g - g1) * (i * 2))) : (g1 - ((g1 - g2) * ((i * 2) - 1.f)));
        const int pointb = (i < 0.5f) ? (b - ((b - b1) * (i * 2))) : (b1 - ((b1 - b2) * ((i * 2) - 1.f)));
        palette[std::size_t(j) * 3] = static_cast<std::uint8_t>(pointr);
        palette[std::size_t(j) * 3 + 1] = static_cast<std::uint8_t>(pointg);
        palette[std::size_t(j) * 3 + 2] = static_cast<std::uint8_t>(pointb);
        // legacy stops at 1.0 halfway: the upper half is the inner colour
        if (j < 128)
            i += div;
    }
    return palette;
}

// Legacy AudioBox.cpp: the player gets at most 150%.
float playbackVolumeFromSlider(int position)
{
    constexpr float kMaxPlaybackVolume = 1.5f;
    if (position <= 50)
        return audioScaleFromSlider(position);
    return 1.0f + ((kMaxPlaybackVolume - 1.0f) * (float(position - 50) / 50.0f));
}

AudioSpectrum::AudioSpectrum(int threads)
{
    // legacy AudioSpectrumMultiThreading: the processors, 2 when unknown
    m_threads = threads > 0 ? threads : static_cast<int>(std::thread::hardware_concurrency());
    if (m_threads < 1)
        m_threads = 2;
    m_nullLine.assign(kSpectrumLineLength, 0.f);
    setColours(0xFF000000, 0xFF674FD7, 0xFFF4F4F4);
}

void AudioSpectrum::setColours(std::uint32_t background, std::uint32_t echo, std::uint32_t inner)
{
    m_palette = legacySpectrumPalette(background, echo, inner);
}

std::size_t AudioSpectrum::cacheBytes() const
{
    std::size_t bytes = 0;
    for (const auto &slot : m_slots)
        bytes += slot.lines.capacity() * sizeof(float);
    return bytes;
}

// Legacy RenderRange's newOverlaps: more transforms per line the closer the
// zoom, from 1 at 100% to at most 24.
int AudioSpectrum::overlapsFor(int imgwidth, std::int64_t rangeStart, std::int64_t rangeEnd, int percent)
{
    int newOverlaps = static_cast<int>(
        std::ceil(((float)imgwidth / ((float)(rangeEnd - rangeStart) / (float)kSpectrumDoubleLength)) *
                  ((100 - percent) / 100.f)) +
        1);
    if (newOverlaps < 1)
        newOverlaps = 1;
    if (newOverlaps > kMaxOverlaps)
        newOverlaps = kMaxOverlaps;
    return newOverlaps;
}

// Legacy SpectrumCache::CreateCache: a sub-cache's first line starts at its
// own multiple of 16 x 2048 samples and the next ones follow 2048 / overlaps
// (truncated) apart, so with overlaps that do not divide 2048 the lines are
// not evenly spaced across sub-caches.
std::int64_t AudioSpectrum::lineSample(std::uint64_t line, int overlaps)
{
    const std::uint64_t subcachelen = std::uint64_t(kSubCacheLength) * overlaps;
    const std::uint64_t start = line / subcachelen * subcachelen;
    const auto fftStart = static_cast<std::int64_t>((start * kSpectrumDoubleLength) / overlaps);
    const auto offset = static_cast<std::int64_t>(kSpectrumDoubleLength / overlaps);
    return fftStart + static_cast<std::int64_t>(line - start) * offset;
}

// Legacy SpectrumCache::GetLine: a line no sub-cache holds is all zero.
const float *AudioSpectrum::line(std::uint64_t i) const
{
    const std::uint64_t subcachelen = std::uint64_t(kSubCacheLength) * m_overlaps;
    const auto found = m_byIndex.find(i / subcachelen);
    if (found == m_byIndex.end())
        return m_nullLine.data();
    return m_slots[found->second].lines.data() + (i % subcachelen) * kSpectrumLineLength;
}

// Legacy AudioSpectrumMultiThreading::CreateCache: every sub-cache from
// startCache to endCache is transformed unless a slot already holds it. The
// samples are read here (legacy FFT::SetAudio, Provider::GetBuffer), then
// the workers transform the lines.
void AudioSpectrum::fill(const DisplayAudio &audio, std::uint64_t startCache, std::uint64_t endCache)
{
    const int overlaps = m_overlaps;
    const std::uint64_t subcachelen = std::uint64_t(kSubCacheLength) * overlaps;
    ++m_clock;
    std::vector<std::uint64_t> missing;
    for (std::uint64_t c = startCache; c <= endCache; ++c) {
        if (const auto found = m_byIndex.find(c); found != m_byIndex.end())
            m_slots[found->second].used = m_clock;
        else
            missing.push_back(c);
    }
    if (missing.empty())
        return;

    // the least recently used slots that this render does not need
    std::vector<std::size_t> free;
    for (std::size_t s = 0; s < m_slots.size(); ++s)
        if (m_slots[s].used != m_clock)
            free.push_back(s);
    std::sort(free.begin(), free.end(), [this](std::size_t a, std::size_t b) { return m_slots[a].used < m_slots[b].used; });
    std::vector<std::size_t> targets;
    for (std::size_t n = 0; n < missing.size(); ++n) {
        SubCache &slot = m_slots[free[n]];
        if (slot.index != ~std::uint64_t(0))
            m_byIndex.erase(slot.index);
        slot.index = missing[n];
        slot.used = m_clock;
        // legacy data.resize(subcachelen) only when it is smaller
        if (slot.lines.size() < subcachelen * kSpectrumLineLength)
            slot.lines.resize(subcachelen * kSpectrumLineLength);
        m_byIndex[slot.index] = free[n];
        targets.push_back(free[n]);
    }

    const std::int64_t from = lineSample(missing.front() * subcachelen, overlaps);
    const std::int64_t to = lineSample(missing.back() * subcachelen + subcachelen - 1, overlaps) + kSpectrumDoubleLength;
    std::vector<std::int16_t> samples(static_cast<std::size_t>(to - from));
    audio.read(from, to - from, samples.data());

    auto work = [&](std::size_t first, std::size_t step) {
        for (std::size_t n = first; n < targets.size(); n += step) {
            SubCache &slot = m_slots[targets[n]];
            for (std::uint64_t l = 0; l < subcachelen; ++l) {
                const std::int64_t at = lineSample(slot.index * subcachelen + l, overlaps) - from;
                legacySpectrumLine(samples.data() + at, slot.lines.data() + l * kSpectrumLineLength);
            }
        }
    };
    const std::size_t workers = std::min<std::size_t>(static_cast<std::size_t>(m_threads), targets.size());
    if (workers <= 1) {
        work(0, 1);
    } else {
        std::vector<std::jthread> pool;
        for (std::size_t t = 1; t < workers; ++t)
            pool.emplace_back(work, t, workers);
        work(0, workers);
    }
    m_transformed += targets.size() * subcachelen;
}

void AudioSpectrum::render(const DisplayAudio &audio, std::int64_t range_start, std::int64_t range_end,
                           std::uint8_t *img, int imgwidth, int imgpitch, int imgheight, int percent)
{
    if (imgwidth < 1 || imgheight < 1 || range_end <= range_start)
        return;
    const int newOverlaps = overlapsFor(imgwidth, range_start, range_end, percent);
    if (newOverlaps != m_overlaps) {
        // legacy: every sub-cache's start is reset
        for (auto &slot : m_slots)
            slot.index = ~std::uint64_t(0);
        m_byIndex.clear();
        m_overlaps = newOverlaps;
    }
    const int overlaps = m_overlaps;
    const std::uint64_t subcachelen = std::uint64_t(kSubCacheLength) * overlaps;
    const auto first_line = static_cast<std::uint64_t>(overlaps * range_start / kSpectrumDoubleLength);
    const auto last_line = static_cast<std::uint64_t>(overlaps * range_end / kSpectrumDoubleLength);
    const std::uint64_t startcache = first_line / subcachelen;
    const std::uint64_t endcache = last_line / subcachelen;

    // legacy's budget: the sub-caches grow to three times what a render
    // needs (its span and one per worker) and are reused, never freed
    const std::size_t neededsize = static_cast<std::size_t>(endcache - startcache) + static_cast<std::size_t>(m_threads);
    if (m_slots.size() < neededsize)
        m_slots.resize(neededsize * 3);

    const float factor = std::pow(static_cast<unsigned long>(kSpectrumLineLength), 1.f / (imgheight - 1));
    // Some scaling constants
    const int maxpower = (1 << (16 - 1)) * 256;
    const double upscale = m_powerScale * 16384 / static_cast<unsigned long>(kSpectrumLineLength);
    const int maxband = kSpectrumLineLength, minband = 0;
    const int lineLength = kSpectrumLineLength;
    fill(audio, startcache, endcache);

    auto writePixel = [&](int x, int y, int intensity) {
        if (intensity < 0)
            intensity = 0;
        if (intensity > 255)
            intensity = 255;
        std::uint8_t *pixel = img + ((std::size_t(imgheight - y - 1) * imgpitch + x) * 4);
        pixel[2] = m_palette[std::size_t(intensity) * 3 + 0];
        pixel[1] = m_palette[std::size_t(intensity) * 3 + 1];
        pixel[0] = m_palette[std::size_t(intensity) * 3 + 2];
    };
    // R3 (proposed A2-tall-spectrum): legacy's expansion reads band 1024 of
    // a 1024-band line for the top row of a display 1024 rows or taller, past
    // the line's end; that band reads as zero.
    auto band = [&](const float *line, int index) { return index < lineLength ? line[index] : 0.f; };

    // Note that here "lines" are actually bands of power data
    const std::uint64_t sampleRange = (last_line - first_line + 1);
    for (std::uint64_t i = first_line, k = 0; i <= last_line; ++i, ++k) {
        // legacy never records the last column it drew, so every line paints
        // its columns and a later line overwrites a shared one
        const int imgcol = static_cast<int>(std::uint64_t(imgwidth) * k / sampleRange);
        const float *line = this->line(i);
        // Handle horizontal expansion
        int next_line_imgcol = static_cast<int>(std::uint64_t(imgwidth) * (k + 1) / sampleRange);
        if (next_line_imgcol >= imgpitch)
            next_line_imgcol = imgpitch - 1;

        for (int x = imgcol; x <= next_line_imgcol; ++x) {
            if (maxband - minband > imgheight) {
                // more than one frequency sample per pixel: the largest per pixel
                int sample2 = 0;
                int sample1 = 0;
                float samplecounter = 1.f;
                for (int y = 0; y < imgheight; ++y) {
                    if (m_nonLinear) {
                        if (sample2 >= (int)samplecounter)
                            sample2++;
                        else
                            sample2 = (int)samplecounter;
                        samplecounter *= factor;
                        if (sample1 >= lineLength)
                            sample1 = lineLength - 1;
                        if (sample2 >= lineLength)
                            sample2 = lineLength - 1;
                    } else {
                        sample1 = std::max(0, maxband * y / imgheight + minband);
                        sample2 = std::min(lineLength - 1, maxband * (y + 1) / imgheight + minband);
                    }
                    float maxval = 0;
                    for (int samp = sample1; samp <= sample2; samp++)
                        if (line[samp] > maxval)
                            maxval = line[samp];
                    sample1 = sample2 + 1;
                    const int intensity = int(256 * (maxval * upscale) / maxpower);
                    writePixel(x, y, intensity);
                }
            } else {
                // less than one frequency sample per pixel: interpolate
                for (int y = 0; y < imgheight; ++y) {
                    const float ideal = (float)(y + 1.) / imgheight * maxband;
                    const float sample1 = band(line, (int)std::floor(ideal) + minband);
                    const float sample2 = band(line, (int)std::ceil(ideal) + minband);
                    const float frac = ideal - std::floor(ideal);
                    const int intensity = int(((1 - frac) * sample1 + frac * sample2) * upscale / maxpower * 256);
                    writePixel(x, y, intensity);
                }
            }
        }
    }
}

} // namespace hikari::application
