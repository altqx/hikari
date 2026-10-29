#include "hikari/application/vocal_onsets.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace hikari::application;

namespace {

struct Note {
    int startMs = 0, lengthMs = 0;
    double f0 = 220;
    double level = 0.3;
    int attackMs = 10;
    double vibratoCents = 30;
    int harmonics = 16;
};

class Track {
public:
    explicit Track(int ms, int rate = 48000) : m_rate(rate), m_samples(std::size_t(std::int64_t(ms) * rate / 1000)) {}

    void add(const Note &note)
    {
        const std::int64_t first = std::int64_t(note.startMs) * m_rate / 1000;
        const std::int64_t length = std::int64_t(note.lengthMs) * m_rate / 1000;
        const double attack = note.attackMs * m_rate / 1000.0, release = 30 * m_rate / 1000.0;
        double phase = 0;
        for (std::int64_t i = 0; i < length && first + i < std::int64_t(m_samples.size()); ++i) {
            const double t = double(i) / m_rate;
            const double vibrato = note.vibratoCents / 1200 * std::sin(2 * std::numbers::pi * 5.5 * t);
            const double f = note.f0 * std::pow(2.0, vibrato);
            phase += 2 * std::numbers::pi * f / m_rate;
            double v = 0;
            for (int k = 1; k <= note.harmonics; ++k) {
                if (note.f0 * k > m_rate / 2.0 - 500)
                    break;
                const double formant = 1 + 2 * std::exp(-std::pow((note.f0 * k - 700) / 300, 2)) +
                                       1.5 * std::exp(-std::pow((note.f0 * k - 1200) / 300, 2)) +
                                       std::exp(-std::pow((note.f0 * k - 2600) / 500, 2));
                v += formant / k * std::sin(k * phase);
            }
            double envelope = 1;
            if (i < attack)
                envelope = i / attack;
            else if (i > length - release)
                envelope = std::max(0.0, (length - i) / release);
            m_samples[std::size_t(first + i)] += note.level * 0.35 * envelope * v;
        }
    }
    void noise(double level)
    {
        std::uint32_t seed = 4242;
        for (double &s : m_samples) {
            seed = seed * 1103515245u + 12345u;
            s += level * (double((seed >> 8) & 0xFFFF) / 32768.0 - 1);
        }
    }
    void sine(int startMs, int lengthMs, double f, double level)
    {
        const std::int64_t first = std::int64_t(startMs) * m_rate / 1000;
        const std::int64_t length = std::int64_t(lengthMs) * m_rate / 1000;
        const double ramp = 0.02 * m_rate;
        for (std::int64_t i = 0; i < length && first + i < std::int64_t(m_samples.size()); ++i) {
            const double envelope = std::min({1.0, i / ramp, (length - i) / ramp});
            m_samples[std::size_t(first + i)] += level * envelope * std::sin(2 * std::numbers::pi * f * i / m_rate);
        }
    }

    DisplayAudio audio() const
    {
        std::vector<std::int16_t> pcm(m_samples.size());
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = static_cast<std::int16_t>(std::lround(std::clamp(m_samples[i], -1.0, 1.0) * 32767));
        DisplayAudio audio(m_rate, std::int64_t(pcm.size()));
        audio.appendFrames(pcm.data(), std::int64_t(pcm.size()), 1);
        audio.finish();
        return audio;
    }

private:
    int m_rate;
    std::vector<double> m_samples;
};

std::vector<int> onsets(const Track &track)
{
    const auto found = detectVocalOnsets(track.audio());
    EXPECT_TRUE(found.has_value());
    return found.value_or(std::vector<int>{});
}

void expectOnsets(const std::vector<int> &found, const std::vector<int> &expected, int toleranceMs)
{
    ASSERT_EQ(found.size(), expected.size()) << ::testing::PrintToString(found);
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_NEAR(found[i], expected[i], toleranceMs) << "onset " << i << " of " << ::testing::PrintToString(found);
}

}

TEST(VocalOnsets, NotesAfterSilenceStartWhereTheVoiceDoes)
{
    Track track(4000);
    const std::vector<int> starts{300, 900, 1450, 2230, 3010};
    const double pitches[] = {220, 262, 196, 294, 247};
    for (std::size_t i = 0; i < starts.size(); ++i)
        track.add({.startMs = starts[i], .lengthMs = 400, .f0 = pitches[i]});
    track.noise(0.001);
    const auto found = onsets(track);
    expectOnsets(found, starts, 10);
    for (const int ms : found)
        EXPECT_EQ(ms % kOnsetHopMs, 0);
}

TEST(VocalOnsets, SyllablesJoinedByAShortDip)
{
    Track track(3000);
    const std::vector<int> starts{200, 640, 1010, 1490, 2000};
    const double pitches[] = {220, 247, 262, 220, 196};
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const int length = (i + 1 < starts.size() ? starts[i + 1] : 2600) - starts[i] - 20;
        track.add({.startMs = starts[i], .lengthMs = length, .f0 = pitches[i], .attackMs = 15});
    }
    track.noise(0.002);
    expectOnsets(onsets(track), starts, 20);
}

TEST(VocalOnsets, AVoiceOverAHeldChord)
{
    Track track(4000);
    for (const double f : {110.0, 138.6, 164.8})
        track.add({.startMs = 100, .lengthMs = 3800, .f0 = f, .level = 0.12, .attackMs = 40, .vibratoCents = 0});
    const std::vector<int> starts{800, 1400, 2050, 2900};
    const double pitches[] = {330, 294, 392, 349};
    for (std::size_t i = 0; i < starts.size(); ++i)
        track.add({.startMs = starts[i], .lengthMs = 450, .f0 = pitches[i]});
    track.noise(0.001);
    auto expected = starts;
    expected.insert(expected.begin(), 100);
    expectOnsets(onsets(track), expected, 20);
}

TEST(VocalOnsets, AHeldNoteWithVibratoStartsOnce)
{
    Track track(4000);
    track.add({.startMs = 500, .lengthMs = 3000, .f0 = 330, .vibratoCents = 60});
    track.noise(0.001);
    expectOnsets(onsets(track), {500}, 10);
}

TEST(VocalOnsets, BassBelowTheVoiceStartsNone)
{
    Track track(3000);
    track.sine(200, 2500, 55, 0.4);
    track.add({.startMs = 1200, .lengthMs = 600, .f0 = 262});
    track.noise(0.001);
    expectOnsets(onsets(track), {1200}, 10);
}

TEST(VocalOnsets, SilenceAndNoiseStartNothing)
{
    Track quiet(2000);
    EXPECT_TRUE(onsets(quiet).empty());
    Track noisy(2000);
    noisy.noise(0.003);
    EXPECT_TRUE(onsets(noisy).empty());
    const auto none = detectVocalOnsets(DisplayAudio::silence(48000, 0));
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->empty());
}

TEST(VocalOnsets, OtherSampleRates)
{
    for (const int rate : {44100, 22050}) {
        Track track(2500, rate);
        track.add({.startMs = 400, .lengthMs = 500, .f0 = 220});
        track.add({.startMs = 1300, .lengthMs = 500, .f0 = 294});
        track.noise(0.001);
        SCOPED_TRACE(rate);
        expectOnsets(onsets(track), {400, 1300}, 10);
    }
}

TEST(VocalOnsets, AudioPastTheCapIsNotLookedAt)
{
    const auto found = detectVocalOnsets(DisplayAudio::silence(48000, std::int64_t(3000000000000000)));
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->empty());
}

TEST(VocalOnsets, CancelEndsTheDetection)
{
    Track track(3000);
    track.add({.startMs = 300, .lengthMs = 400});
    const std::atomic<bool> cancel = true;
    EXPECT_EQ(detectVocalOnsets(track.audio(), &cancel), std::nullopt);
}

TEST(VocalOnsets, SnapTakesTheNearestOnsetInTheWindowAndBounds)
{
    const std::vector<int> found{1000, 1200, 1300, 2000};
    EXPECT_EQ(snapToOnset(found, 1040, 100), 1000);
    EXPECT_EQ(snapToOnset(found, 1180, 100), 1200);
    EXPECT_EQ(snapToOnset(found, 1250, 100), 1200);
    EXPECT_EQ(snapToOnset(found, 1260, 100), 1300);
    EXPECT_EQ(snapToOnset(found, 1500, 100), 1500);
    EXPECT_EQ(snapToOnset(found, 1900, 100), 2000);
    EXPECT_EQ(snapToOnset(found, 1899, 100), 1899);
    EXPECT_EQ(snapToOnset(found, 900, 100), 1000);
    EXPECT_EQ(snapToOnset(found, 2101, 100), 2101);
    EXPECT_EQ(snapToOnset({}, 1040, 100), 1040);
    EXPECT_EQ(snapToOnset(found, 1240, 100, 1210, INT_MAX), 1300);
    EXPECT_EQ(snapToOnset(found, 1240, 100, INT_MIN, 1290), 1200);
    EXPECT_EQ(snapToOnset(found, 1040, 100, 1010, 1190), 1040);
}
