#include "hikari/application/vocal_onsets.h"

#include "hikari/application/audio_spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace hikari::application {

namespace {

constexpr double kBandLowHz = 150, kBandHighHz = 4000;
constexpr int kWindowMs = 40;
constexpr int kLag = 2;                // frames
constexpr double kLogGamma = 100;
constexpr int kPeakSpan = 3;           // frames each side
constexpr int kMeanSpan = 10;          // frames each side
constexpr int kMedianSpan = 25;        // frames each side
constexpr double kDelta = 0.12;        // of the flux's scale
constexpr double kMinDelta = 0.08;     // log magnitude per band
constexpr double kQuietRms = 0.004;    // of full scale, about -48 dBFS
constexpr int kBlockFrames = 200;      // 2 s

// Per 10 ms frame: the flux, and the energy as mean square (full scale 1).
struct Envelope {
    std::vector<double> flux;
    std::vector<double> energy;
};

std::optional<Envelope> envelope(const DisplayAudio &audio, const std::atomic<bool> *cancel)
{
    const int rate = audio.sampleRate();
    const std::int64_t samples =
        std::min({audio.sampleCount(), audio.decoded(), kOnsetMaxMs / 1000 * std::int64_t(rate)});
    const std::int64_t frames = samples * 1000 / rate / kOnsetHopMs + 1;
    const int window = std::min<int>(kSpectrumDoubleLength, rate * kWindowMs / 1000);
    const int lowBin = std::max(1, int(std::ceil(kBandLowHz * kSpectrumDoubleLength / rate)));
    const int highBin = std::min(kSpectrumLineLength - 2, int(std::floor(kBandHighHz * kSpectrumDoubleLength / rate)));
    std::vector<float> hann(static_cast<std::size_t>(window));
    for (int i = 0; i < window; ++i)
        hann[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * (i + 0.5) / window));
    // a full-scale sine's peak bin is 1
    const double scale = 1.0 / (32768.0 * window / 4);
    auto centre = [&](std::int64_t frame) { return frame * kOnsetHopMs * rate / 1000; };

    Envelope out;
    out.flux.assign(std::size_t(frames), 0);
    out.energy.assign(std::size_t(frames), 0);
    // semitone bands, at least one bin wide
    std::vector<int> bandEnd; // exclusive
    for (int k = lowBin; k <= highBin;) {
        const double hz = double(k) * rate / kSpectrumDoubleLength;
        const int end = std::max(k + 1, int(std::ceil(hz * std::pow(2.0, 1.0 / 12) * kSpectrumDoubleLength / rate)));
        bandEnd.push_back(std::min(end, highBin + 1));
        k = bandEnd.back();
    }
    const std::size_t bands = bandEnd.size();
    // a ring of the last kLag frames' log magnitudes, padded with a band each side
    std::vector<std::vector<float>> history(kLag + 1, std::vector<float>(bands + 2, 0.f));
    std::vector<std::int16_t> pcm;
    std::array<float, kSpectrumDoubleLength * 2> data{};
    for (std::int64_t block = 0; block < frames; block += kBlockFrames) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return std::nullopt;
        const std::int64_t last = std::min(frames, block + kBlockFrames);
        const std::int64_t first = centre(block) - window / 2;
        const std::int64_t end = centre(last) + window;
        pcm.resize(std::size_t(end - first));
        audio.read(first, end - first, pcm.data());
        for (std::int64_t f = block; f < last; ++f) {
            const std::int64_t c = centre(f);
            const std::int16_t *at = pcm.data() + (c - window / 2 - first);
            data.fill(0.f);
            const bool silent = std::all_of(at, at + window, [](std::int16_t v) { return v == 0; });
            if (!silent) {
                for (int i = 0; i < window; ++i)
                    data[std::size_t(i) * 2] = float(at[i]) * hann[std::size_t(i)];
                legacyFft(data.data());
            }
            auto &now = history[std::size_t(f % (kLag + 1))];
            const auto &before = history[std::size_t((f + 1) % (kLag + 1))]; // f - kLag
            double flux = 0;
            for (std::size_t b = 0, k = std::size_t(lowBin); b < bands; ++b) {
                double sum = 0;
                for (; k < std::size_t(bandEnd[b]); ++k)
                    sum += std::sqrt(double(data[k * 2]) * data[k * 2] + double(data[k * 2 + 1]) * data[k * 2 + 1]);
                now[b + 1] = float(std::log1p(kLogGamma * sum * scale));
            }
            now[0] = now[1];
            now[bands + 1] = now[bands];
            // SuperFlux: vibrato within a semitone adds little
            if (f >= kLag) {
                for (std::size_t b = 1; b <= bands; ++b)
                    flux += std::max(0.f, now[b] - std::max({before[b - 1], before[b], before[b + 1]}));
            }
            out.flux[std::size_t(f)] = flux / double(bands);
            const std::int16_t *from = pcm.data() + (c - first);
            const std::int64_t length = std::max<std::int64_t>(1, centre(f + 1) - c);
            double energy = 0;
            for (std::int64_t i = 0; i < length; ++i)
                energy += double(from[i]) * double(from[i]);
            out.energy[std::size_t(f)] = energy / double(length) / (32768.0 * 32768.0);
        }
    }
    return out;
}

double localMean(const std::vector<double> &v, std::int64_t at, int span)
{
    const std::int64_t first = std::max<std::int64_t>(0, at - span);
    const std::int64_t last = std::min<std::int64_t>(std::int64_t(v.size()) - 1, at + span);
    double sum = 0;
    for (std::int64_t i = first; i <= last; ++i)
        sum += v[std::size_t(i)];
    return sum / double(last - first + 1);
}

double localMedian(const std::vector<double> &v, std::int64_t at, int span, std::vector<double> &scratch)
{
    const std::int64_t first = std::max<std::int64_t>(0, at - span);
    const std::int64_t last = std::min<std::int64_t>(std::int64_t(v.size()) - 1, at + span);
    scratch.assign(v.begin() + first, v.begin() + last + 1);
    const auto middle = scratch.begin() + std::ptrdiff_t(scratch.size() / 2);
    std::nth_element(scratch.begin(), middle, scratch.end());
    return *middle;
}

}

std::optional<std::vector<int>> detectVocalOnsets(const DisplayAudio &audio, const std::atomic<bool> *cancel)
{
    if (cancel && cancel->load(std::memory_order_relaxed))
        return std::nullopt;
    std::vector<int> onsets;
    if (audio.sampleRate() <= 0 || std::min(audio.sampleCount(), audio.decoded()) <= 0)
        return onsets;
    const auto env = envelope(audio, cancel);
    if (!env)
        return std::nullopt;
    const auto &flux = env->flux;
    const auto &energy = env->energy;
    const auto frames = std::int64_t(flux.size());

    std::vector<double> sorted(flux);
    const auto rank = sorted.begin() + std::ptrdiff_t((sorted.size() - 1) * 99 / 100);
    std::nth_element(sorted.begin(), rank, sorted.end());
    const double fluxScale = *rank;
    if (fluxScale <= 0)
        return onsets;

    std::vector<double> scratch;
    const double quiet = kQuietRms * kQuietRms;
    for (std::int64_t f = 0; f < frames; ++f) {
        const double value = flux[std::size_t(f)];
        if (value <= 0)
            continue;
        bool peak = true;
        const std::int64_t last = std::min(frames - 1, f + kPeakSpan);
        for (std::int64_t g = std::max<std::int64_t>(0, f - kPeakSpan); peak && g <= last; ++g)
            peak = g == f || flux[std::size_t(g)] < value || (g > f && flux[std::size_t(g)] <= value);
        if (!peak)
            continue;
        const double local = std::max(localMean(flux, f, kMeanSpan), localMedian(flux, f, kMedianSpan, scratch));
        const double threshold = local + std::max(kMinDelta, kDelta * fluxScale);
        if (value < threshold)
            continue;
        // Back to the sharpest energy rise from 50 ms before to 30 ms after the peak.
        std::int64_t at = f;
        double sharpest = 2; // a rise needs to double the energy
        const double floor = quiet / 16;
        for (std::int64_t g = std::max<std::int64_t>(1, f - 5); g <= std::min(frames - 1, f + 3); ++g) {
            const double rise = (energy[std::size_t(g)] + floor) / (energy[std::size_t(g) - 1] + floor);
            if (rise > sharpest) {
                sharpest = rise;
                at = g;
            }
        }
        double loudest = 0;
        for (std::int64_t g = at; g < std::min(frames, at + 5); ++g)
            loudest = std::max(loudest, energy[std::size_t(g)]);
        if (loudest < quiet)
            continue;
        const int ms = int(at * kOnsetHopMs);
        if (!onsets.empty() && ms - onsets.back() < kOnsetMinGapMs)
            continue;
        onsets.push_back(ms);
    }
    return onsets;
}

int snapToOnset(std::span<const int> onsetsMs, int ms, int windowMs, int lowMs, int highMs)
{
    const long long low = std::max<long long>(lowMs, (long long)ms - windowMs);
    const long long high = std::min<long long>(highMs, (long long)ms + windowMs);
    int best = ms;
    long long bestDistance = -1;
    for (auto it = std::lower_bound(onsetsMs.begin(), onsetsMs.end(), low); it != onsetsMs.end() && *it <= high; ++it) {
        const long long distance = std::llabs((long long)*it - ms);
        if (bestDistance < 0 || distance < bestDistance) {
            best = *it;
            bestDistance = distance;
        }
    }
    return best;
}

}
