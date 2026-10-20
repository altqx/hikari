// KA2 (#277): tap-to-time karaoke timing (KA2-tap-to-time). The tap state
// machine over one Line's syllables in Hold and Press mode, undo-rewind,
// stop and resume, the song time a key stands for at each speed, and the
// latency calibration's maths.

#include "hikari/application/karaoke_tap.h"

#include <gtest/gtest.h>

#include <random>

using namespace hikari::application;
using Tap = KaraokeTap;

namespace {

constexpr int kSpace = 32, kJ = 74;

std::vector<Tap::Syllable> kimi()
{
    return {{u"ki", u"k"}, {u"mi", u"k"}, {u"no", u"kf"}};
}

} // namespace

// Key-down starts the next syllable, key-up ends it; the first press is the
// Line's start and the last release its end.
TEST(KaraokeTap, HoldTapsEachSyllableAndTheLine)
{
    Tap tap;
    tap.begin(kimi(), 1000);
    EXPECT_EQ(tap.press(kSpace, 1200), Tap::Event::Started);
    EXPECT_EQ(tap.states(), (std::vector{Tap::SyllableState::Held, Tap::SyllableState::Waiting,
                                         Tap::SyllableState::Waiting}));
    EXPECT_EQ(tap.release(kSpace, 1500), Tap::Event::Ended);
    EXPECT_EQ(tap.states()[1], Tap::SyllableState::Next);
    tap.press(kSpace, 1500);
    tap.release(kSpace, 1800);
    tap.press(kSpace, 1900); // 100 ms after the release: a gap
    EXPECT_EQ(tap.spans(2000), (std::vector<Tap::Span>{{1200, 1500}, {1500, 1800}, {1900, 2000}}));
    EXPECT_EQ(tap.release(kSpace, 2250), Tap::Event::LineDone);
    EXPECT_TRUE(tap.done());
    const Tap::Result r = tap.result();
    EXPECT_EQ(r.startMs, 1200);
    EXPECT_EQ(r.endMs, 2250);
    // the gap is an empty syllable with the tag of the one before it
    EXPECT_EQ(r.syllables, (std::vector<Tap::Timed>{
                               {u"ki", u"k", 1500}, {u"mi", u"k", 1800}, {u"", u"k", 1900}, {u"no", u"kf", 2250}}));
    // further keys do nothing
    EXPECT_EQ(tap.press(kSpace, 2400), Tap::Event::None);
}

// A gap shorter than 30 ms joins the syllable before it.
TEST(KaraokeTap, ShortGapsJoinTheSyllableBefore)
{
    Tap tap;
    tap.begin(kimi(), 0);
    tap.press(kSpace, 100);
    tap.release(kSpace, 300);
    tap.press(kSpace, 320); // 20 ms
    tap.release(kSpace, 500);
    tap.press(kSpace, 530); // 30 ms: a gap
    tap.release(kSpace, 700);
    EXPECT_EQ(tap.result().syllables, (std::vector<Tap::Timed>{
                                          {u"ki", u"k", 320}, {u"mi", u"k", 500}, {u"", u"k", 530}, {u"no", u"kf", 700}}));
}

// The held key's auto-repeat and other keys' releases change nothing; another
// key pressed while one is held ends the held syllable and starts the next.
TEST(KaraokeTap, AnotherKeyRollsOnWhileOneIsHeld)
{
    Tap tap;
    tap.begin(kimi(), 0);
    tap.press(kSpace, 100);
    EXPECT_EQ(tap.press(kSpace, 150), Tap::Event::None);
    EXPECT_EQ(tap.release(kJ, 180), Tap::Event::None);
    EXPECT_EQ(tap.press(kJ, 200), Tap::Event::Started);
    EXPECT_EQ(tap.release(kSpace, 220), Tap::Event::None); // no longer the held key
    EXPECT_EQ(tap.release(kJ, 260), Tap::Event::Ended);
    EXPECT_EQ(tap.spans(0), (std::vector<Tap::Span>{{100, 200}, {200, 260}}));
}

// Press mode: each press starts the next syllable and ends the one before;
// the press after the last ends the Line; releases do nothing.
TEST(KaraokeTap, PressLeavesNoGaps)
{
    Tap tap;
    tap.setMode(Tap::Mode::Press);
    tap.begin(kimi(), 0);
    EXPECT_EQ(tap.press(kSpace, 100), Tap::Event::Started);
    EXPECT_EQ(tap.release(kSpace, 150), Tap::Event::None);
    EXPECT_EQ(tap.press(kSpace, 400), Tap::Event::Started);
    EXPECT_EQ(tap.press(kSpace, 700), Tap::Event::Started);
    EXPECT_FALSE(tap.done());
    EXPECT_EQ(tap.press(kSpace, 950), Tap::Event::LineDone);
    EXPECT_EQ(tap.result(), (Tap::Result{100, 950, {{u"ki", u"k", 400}, {u"mi", u"k", 700}, {u"no", u"kf", 950}}}));
}

// A tap before the last boundary kept (the replay after a rewind) lands on
// it; a syllable is at least 10 ms long.
TEST(KaraokeTap, TimeNeverRunsBackwards)
{
    Tap tap;
    tap.begin(kimi(), 0);
    tap.press(kSpace, 500);
    tap.release(kSpace, 400);
    tap.press(kSpace, 300);
    tap.release(kSpace, 900);
    EXPECT_EQ(tap.spans(0), (std::vector<Tap::Span>{{500, 510}, {510, 900}}));
}

// Backspace takes back the held syllable and the tapped one before it and
// plays from 2 s before the earliest; with nothing tapped, from 2 s before
// the Line. Shift+Backspace takes back the Line.
TEST(KaraokeTap, UndoRewinds)
{
    Tap tap;
    tap.begin(kimi(), 10000);
    EXPECT_EQ(tap.undo(), 8000);
    tap.press(kSpace, 10200);
    tap.release(kSpace, 10500);
    tap.press(kSpace, 10600);
    tap.release(kSpace, 10900);
    tap.press(kSpace, 11000); // "no" held
    EXPECT_EQ(tap.undo(), 8600); // "no" and "mi" back, from 2 s before "mi"
    EXPECT_EQ(tap.tapped(), 1);
    EXPECT_FALSE(tap.holding());
    EXPECT_EQ(tap.states()[1], Tap::SyllableState::Next);
    EXPECT_EQ(tap.undo(), 8200); // "ki"
    EXPECT_EQ(tap.tapped(), 0);
    tap.press(kSpace, 9900);
    tap.release(kSpace, 10100);
    EXPECT_EQ(tap.undoLine(), 7900); // before the first tap, earlier than the Line
    EXPECT_EQ(tap.tapped(), 0);
    EXPECT_EQ(tap.undoLine(), 8000);
    // never before the audio's start
    tap.begin(kimi(), 500);
    EXPECT_EQ(tap.undo(), 0);
}

// Esc drops a held syllable and keeps the tapped ones; Resume plays from
// 2 s before the last tapped one's end, or 1 s before the Line; Revert Line
// forgets every tap.
TEST(KaraokeTap, StopResumeAndRevert)
{
    Tap tap;
    tap.begin(kimi(), 5000);
    EXPECT_EQ(tap.resumeMs(), 4000);
    tap.press(kSpace, 5100);
    tap.release(kSpace, 5400);
    tap.press(kSpace, 5500);
    tap.stop();
    EXPECT_FALSE(tap.holding());
    EXPECT_EQ(tap.tapped(), 1);
    EXPECT_EQ(tap.resumeMs(), 3400);
    tap.revert();
    EXPECT_EQ(tap.tapped(), 0);
    EXPECT_EQ(tap.resumeMs(), 4000);
}

// The syllables to tap: earlier gaps (empty syllables) are dropped, blank
// ones join the syllable before; a Line of one syllable needs splitting.
TEST(KaraokeTap, TargetsFromTheKaraokeModel)
{
    AudioKaraoke karaoke;
    karaoke.split({u"{\\k20}ki{\\k30}mi {\\k5}{\\k20} {\\kf20}no", 0, 950}, false, false);
    ASSERT_EQ(karaoke.count(), 5);
    const auto targets = Tap::targets(karaoke);
    ASSERT_EQ(targets.size(), 3u);
    EXPECT_EQ(targets[0], (Tap::Syllable{u"ki", u"k"}));
    EXPECT_EQ(targets[1].text, u"mi  ");
    EXPECT_EQ(targets[2], (Tap::Syllable{u"no", u"kf"}));
    Tap tap;
    tap.begin(targets, 0);
    EXPECT_FALSE(tap.needsSplit());
    karaoke.split({u"hibiku", 0, 950}, false, false);
    tap.begin(Tap::targets(karaoke), 0);
    EXPECT_TRUE(tap.needsSplit());
}

// A tap's song time is the position less the latency, which is wall-clock
// time: at half speed 85 ms of reaction is 42.5 ms of song. Nearest 10 ms.
TEST(KaraokeTap, SongTimeAllowsForTheLatencyAtEachSpeed)
{
    EXPECT_EQ(tapSongMs(10'000, 0, 1), 10'000);
    EXPECT_EQ(tapSongMs(10'004, 0, 1), 10'000);
    EXPECT_EQ(tapSongMs(10'005, 0, 1), 10'010);
    EXPECT_EQ(tapSongMs(10'000, 85, 1), 9'920); // 9915 to the nearest 10
    EXPECT_EQ(tapSongMs(10'000, 85, 0.5), 9'960); // 9957.5
    EXPECT_EQ(tapSongMs(10'000, 80, 0.25), 9'980);
    EXPECT_EQ(tapSongMs(10'000, -40, 0.5), 10'020); // pressing early
    EXPECT_EQ(tapSongMs(30, 85, 1), 0);
}

// Tapping a Line on test audio through the latency: presses a person makes
// 85 ms (wall clock) after each syllable starts, at half speed, land on the
// syllables' own times once the calibrated latency is taken off.
TEST(KaraokeTap, TapsLandWithinTheCalibratedLatency)
{
    const std::vector<int> starts{12'000, 12'310, 12'580, 12'900}, ends{12'260, 12'540, 12'800, 13'300};
    const double speed = 0.5;
    const int reactionMs = 85;
    std::mt19937 rng(277);
    std::normal_distribution<double> jitter(0, 8);
    // calibration on beeps, the same person
    std::vector<double> beats, presses;
    for (int i = 0; i < kCalibrationBeats; i++) {
        beats.push_back(1000.0 + i * kCalibrationIntervalMs);
        presses.push_back(beats.back() + reactionMs + jitter(rng));
    }
    const auto cal = calibrateLatency(beats, presses);
    ASSERT_TRUE(cal.enough);
    EXPECT_NEAR(cal.latencyMs, reactionMs, 6);
    // a press `reactionMs` of wall clock after a sound is heard comes when the
    // playhead is reactionMs * speed of song further on
    Tap tap;
    tap.begin({{u"a", u"k"}, {u"b", u"k"}, {u"c", u"k"}, {u"d", u"k"}}, 12'000);
    for (std::size_t i = 0; i < starts.size(); i++) {
        tap.press(kSpace, tapSongMs(starts[i] + (reactionMs + jitter(rng)) * speed, cal.latencyMs, speed));
        tap.release(kSpace, tapSongMs(ends[i] + (reactionMs + jitter(rng)) * speed, cal.latencyMs, speed));
    }
    ASSERT_TRUE(tap.done());
    const auto spans = tap.spans(0);
    for (std::size_t i = 0; i < starts.size(); i++) {
        EXPECT_NEAR(spans[i].startMs, starts[i], 20) << i;
        EXPECT_NEAR(spans[i].endMs, ends[i], 20) << i;
    }
}

// Practice beats and presses far from any beat are not counted, one press
// per beat; outliers past three median absolute deviations are dropped.
TEST(KaraokeTap, CalibrationCountsAndDropsOutliers)
{
    std::vector<double> beats;
    for (int i = 0; i < 16; i++)
        beats.push_back(i * 600.0);
    std::vector<double> presses{20, 640, 1150}; // practice: not counted
    const std::vector<double> delays{90, 80, 85, 95, 88, 82, 400, 86, 84, 87, 91, 79};
    for (std::size_t i = 0; i < delays.size(); i++)
        presses.push_back(beats[i + 4] + (delays[i] > 300 ? -250 : delays[i]));
    presses.push_back(beats[15] + 90); // a second press on the last beat: not counted
    presses.push_back(beats[15] + 95);
    auto cal = calibrateLatency(beats, presses);
    // -250 is an outlier; the second press on beat 15 is dropped
    EXPECT_EQ(cal.counted, 11);
    EXPECT_TRUE(cal.enough);
    EXPECT_EQ(cal.latencyMs, 86);
    EXPECT_EQ(cal.spreadMs, 4);
    // too few presses
    cal = calibrateLatency(beats, std::vector<double>{beats[5] + 80, beats[6] + 90});
    EXPECT_FALSE(cal.enough);
    EXPECT_EQ(cal.counted, 2);
    EXPECT_EQ(cal.latencyMs, 85);
    EXPECT_EQ(calibrateLatency(beats, {}), LatencyCalibration{});
}
