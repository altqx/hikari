#include "hikari/application/audio_spectrum.h"

#include <cmath>
#include <utility>

namespace hikari::application {

namespace {

// Legacy GFFT/GFFT.cpp (the Dr. Dobb's template FFT) for legacy's one size,
// float data: the series, the recursion and the scramble as written, so the
// constants and roundings are legacy's own.
#define HIKARI_GFFT_PI 3.1415926535897932384626433832795f

template <unsigned M, unsigned N, unsigned B, unsigned A> struct SinCosSeries {
    static double value()
    {
        return 1 - (A * HIKARI_GFFT_PI / B) * (A * HIKARI_GFFT_PI / B) / M / (M + 1) *
                       SinCosSeries<M + 2, N, B, A>::value();
    }
};

template <unsigned N, unsigned B, unsigned A> struct SinCosSeries<N, N, B, A> {
    static double value() { return 1.; }
};

template <unsigned B, unsigned A> struct Sin {
    static float value() { return (A * HIKARI_GFFT_PI / B) * SinCosSeries<2, 24, B, A>::value(); }
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

#undef HIKARI_GFFT_PI

// GFFT<P>::scramble: reverse-binary reindexing of nn complex values.
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

// Legacy config.h MID(0, value, line_length - 1): the comparison is made in
// unsigned long, so a negative value becomes the last band.
int bandIndex(int value)
{
    const unsigned long last = kSpectrumLineLength - 1;
    return static_cast<int>(static_cast<unsigned long>(value) < last ? static_cast<unsigned long>(value) : last);
}

} // namespace

SpectrumLine legacySpectrumLine(const std::int16_t *samples)
{
    // FFT::Transform: the samples as the real parts, then GFFT<doublelen>::fft.
    static DanielsonLanczos<kSpectrumDoubleLength> recursion;
    std::vector<float> output(kSpectrumDoubleLength * 2);
    for (unsigned long i = 0; i < kSpectrumDoubleLength; i++) {
        output[i * 2] = static_cast<float>(samples[i]);
        output[(i * 2) + 1] = 0.f;
    }
    scramble(output.data(), kSpectrumDoubleLength);
    recursion.apply(output.data());
    // SpectrumCache::CreateCache: the first half's magnitudes.
    SpectrumLine line{};
    for (std::size_t j = 0; j < kSpectrumLineLength; ++j) {
        const std::size_t g = (j * 2) + 1;
        line[j] = std::sqrt(output[j * 2] * output[j * 2] + output[g] * output[g]);
    }
    return line;
}

FrequencyPeaks legacyFrequencyPeaks(const DisplayAudio &audio, long long timeStart, long long timeEnd, int freqStart,
                                    int freqEnd, int peek)
{
    FrequencyPeaks out;
    const int sampleRate = audio.sampleRate();
    if (sampleRate / kSpectrumDoubleLength == 0)
        return out; // legacy divided by zero here (R3): no peaks
    const long long range_start = timeStart * sampleRate / 1000;
    const long long range_end = timeEnd * sampleRate / 1000;
    const unsigned long first_line = static_cast<unsigned long>(range_start / kSpectrumDoubleLength);
    const unsigned long last_line = static_cast<unsigned long>(range_end / kSpectrumDoubleLength);
    int indexStart = static_cast<int>(freqStart / (sampleRate / kSpectrumDoubleLength));
    indexStart = bandIndex(indexStart);
    int indexEnd = static_cast<int>(freqEnd / (sampleRate / kSpectrumDoubleLength));
    indexEnd = bandIndex(indexEnd);
    if (indexEnd < indexStart)
        indexEnd = indexStart;

    const int maxpower = (1 << (16 - 1)) * 100;
    const double upscale = 16384 / kSpectrumLineLength;
    std::vector<std::int16_t> samples(kSpectrumDoubleLength);

    long long lasttime = -1;
    int lastintensity = 0;
    int lastintensitytime = 0;
    for (unsigned long i = first_line; i <= last_line; ++i) {
        // The cache line: the mixdown's samples from i * doublelen (zero past
        // the audio, as legacy ReadCache), transformed.
        audio.read(static_cast<std::int64_t>(i) * static_cast<std::int64_t>(kSpectrumDoubleLength),
                   static_cast<std::int64_t>(kSpectrumDoubleLength), samples.data());
        const SpectrumLine line = legacySpectrumLine(samples.data());
        long long lli = i;
        long long time = (lli * kSpectrumDoubleLength * 1000) / sampleRate;

        bool reached = false;
        if (lasttime < time) {
            for (int band = indexStart; band <= indexEnd; band++) {
                int intensity = int(100 * (line[static_cast<std::size_t>(band)] * upscale) / maxpower);
                if (!peek) {
                    if (lastintensity < intensity)
                        lastintensity = intensity;
                    if (band == indexEnd) {
                        out.times.push_back(static_cast<int>(time));
                        out.intensities.push_back(lastintensity);
                        lastintensity = 0;
                    }
                } else if (intensity >= peek) {
                    if (lastintensity < intensity) {
                        lastintensity = intensity;
                        lastintensitytime = static_cast<int>(time);
                    }
                    reached = true;
                }
            }
            if (lastintensity && !reached) {
                out.times.push_back(lastintensitytime);
                lastintensity = 0;
            }
        }
        lasttime = time;
        if (i == last_line)
            break; // i <= last_line never ends when last_line is the largest value
    }
    return out;
}

} // namespace hikari::application
