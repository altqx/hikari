// A4: the audio box's playback, pinned against legacy AudioBox.cpp's play
// handlers, AudioDisplay::Play, Stop and UpdateTimer, ProviderFFMS2's
// ReadCache and ApplyVolume and PlaybackVolumeFromSlider at 20d647c4. The
// ranges are the frames handed to the player; expected values are worked
// out from legacy's integer arithmetic.

#include "hikari/application/audio_playback.h"

#include <gtest/gtest.h>

#include <vector>

using namespace hikari::application;

namespace {

// The output owner's side: what each play handed over, and a position the
// test moves.
struct FakePlayer : AudioPlayerPort {
    std::vector<PlayRange> plays;
    bool on = false;
    std::int64_t pos = 0, start = 0, end = 0;
    int stops = 0;
    double volume = 1;

    void play(std::int64_t s, std::int64_t count) override
    {
        plays.push_back({s, count});
        on = true;
        start = pos = s;
        end = s + count;
    }
    void stop() override
    {
        on = false;
        ++stops;
    }
    bool playing() const override { return on; }
    std::int64_t position() const override { return on ? pos : 0; }
    std::int64_t startPosition() const override { return start; }
    std::int64_t endPosition() const override { return end; }
    void setEndPosition(std::int64_t e) override { end = e; }
    void setVolume(double v) override { volume = v; }
};

constexpr int kRate = 44100;
constexpr std::int64_t kCount = 441000; // 10 s

PlayRange playMode(PlayMode mode, int start, int end, std::optional<int> mark = std::nullopt, int markPlay = 1000)
{
    FakePlayer player;
    AudioPlayback playback(player);
    const auto request = legacyPlayRequest(mode, start, end, mark, markPlay);
    if (!request)
        return {-1, -1};
    playback.play(kRate, kCount, request->startMs, request->endMs);
    EXPECT_EQ(player.plays.size(), 1u);
    return player.plays.at(0);
}

// 48 kHz, 60 s; 1440 samples a column, 1000 columns wide (30 s in view).
AudioView minuteView()
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(1000, 150, 22, 16);
    return view;
}

} // namespace

// Legacy AudioBox's handlers: the ms each one passes to AudioDisplay::Play.
TEST(AudioPlayback, RequestPerMode)
{
    EXPECT_EQ(legacyPlayRequest(PlayMode::Selection, 1000, 3000, {}, 1000), (PlayRequest{1000, 3000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::Line, 1000, 3000, {}, 1000), (PlayRequest{1000, 3000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::Before500, 1000, 3000, {}, 1000), (PlayRequest{500, 1000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::After500, 1000, 3000, {}, 1000), (PlayRequest{3000, 3500}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::First500, 1000, 3000, {}, 1000), (PlayRequest{1000, 1500}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::Last500, 1000, 3000, {}, 1000), (PlayRequest{2500, 3000}));
    // shorter than 500 ms: the other end is the limit
    EXPECT_EQ(legacyPlayRequest(PlayMode::First500, 1000, 1200, {}, 1000), (PlayRequest{1000, 1200}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::Last500, 1000, 1200, {}, 1000), (PlayRequest{1000, 1200}));
    // a reversed selection: First takes the end, Last the start
    EXPECT_EQ(legacyPlayRequest(PlayMode::First500, 3000, 1000, {}, 1000), (PlayRequest{3000, 1000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::Last500, 3000, 1000, {}, 1000), (PlayRequest{3000, 1000}));
    // the mark and AUDIO_MARK_PLAY_TIME
    EXPECT_EQ(legacyPlayRequest(PlayMode::BeforeMark, 1000, 3000, 2000, 1000), (PlayRequest{1000, 2000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::AfterMark, 1000, 3000, 2000, 1000), (PlayRequest{2000, 3000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::BeforeMark, 1000, 3000, 2000, 250), (PlayRequest{1750, 2000}));
    EXPECT_EQ(legacyPlayRequest(PlayMode::AfterMark, 1000, 3000, 2000, 250), (PlayRequest{2000, 2250}));
    EXPECT_FALSE(legacyPlayRequest(PlayMode::BeforeMark, 1000, 3000, std::nullopt, 1000));
    EXPECT_FALSE(legacyPlayRequest(PlayMode::AfterMark, 1000, 3000, std::nullopt, 1000));
    EXPECT_EQ(legacyPlayRequest(PlayMode::ToEnd, 1000, 3000, {}, 1000), (PlayRequest{1000, -1}));
}

// The frames each mode hands the player: selection 1234..5678 ms, mark at
// 3000 ms, 44.1 kHz (ms * 44100 / 1000, truncated), 10 s of audio.
TEST(AudioPlayback, RangeHandedToThePlayerPerMode)
{
    EXPECT_EQ(playMode(PlayMode::Selection, 1234, 5678), (PlayRange{54419, 250399 - 54419}));
    EXPECT_EQ(playMode(PlayMode::Line, 1234, 5678), (PlayRange{54419, 250399 - 54419}));
    EXPECT_EQ(playMode(PlayMode::Before500, 1234, 5678), (PlayRange{32369, 54419 - 32369}));
    EXPECT_EQ(playMode(PlayMode::After500, 1234, 5678), (PlayRange{250399, 272449 - 250399}));
    EXPECT_EQ(playMode(PlayMode::First500, 1234, 5678), (PlayRange{54419, 76469 - 54419}));
    EXPECT_EQ(playMode(PlayMode::Last500, 1234, 5678), (PlayRange{228349, 250399 - 228349}));
    EXPECT_EQ(playMode(PlayMode::BeforeMark, 1234, 5678, 3000), (PlayRange{88200, 44100}));
    EXPECT_EQ(playMode(PlayMode::AfterMark, 1234, 5678, 3000), (PlayRange{132300, 44100}));
    // to the end: the last frame is never played
    EXPECT_EQ(playMode(PlayMode::ToEnd, 1234, 5678), (PlayRange{54419, kCount - 1 - 54419}));
}

// AudioDisplay::Play's clamping.
TEST(AudioPlayback, PlayRangeClamps)
{
    EXPECT_EQ(legacyPlayRange(48000, 480000, 1000, 3000), (PlayRange{48000, 96000}));
    EXPECT_EQ(legacyPlayRange(44100, 441000, 1, 1001), (PlayRange{44, 44144 - 44}));
    // before the audio: from 0 (500 ms before a Line at 200 ms)
    EXPECT_EQ(legacyPlayRange(48000, 480000, -300, 200), (PlayRange{0, 9600}));
    EXPECT_EQ(legacyPlayRange(48000, 480000, -500, 0), (PlayRange{0, 0}));
    // past the end: the last frame, nothing played
    EXPECT_EQ(legacyPlayRange(48000, 480000, 20000, 21000), (PlayRange{479999, 0}));
    EXPECT_EQ(legacyPlayRange(48000, 480000, 9000, 21000), (PlayRange{432000, 479999 - 432000}));
    // an end before the start plays nothing
    EXPECT_EQ(legacyPlayRange(48000, 480000, 3000, 1000), (PlayRange{144000, 0}));
    // only exactly -1 means the end; another negative end is before the start
    EXPECT_EQ(legacyPlayRange(48000, 480000, 1000, -1), (PlayRange{48000, 479999 - 48000}));
    EXPECT_EQ(legacyPlayRange(48000, 480000, 1000, -2), (PlayRange{48000, 0}));
}

// Legacy playingToEnd follows any negative end, though only -1 plays to the end.
TEST(AudioPlayback, PlayingToEndFollowsANegativeEnd)
{
    FakePlayer player;
    AudioPlayback playback(player);
    playback.play(kRate, kCount, 1000, -1);
    EXPECT_TRUE(playback.playingToEnd());
    playback.play(kRate, kCount, 1000, 2000);
    EXPECT_FALSE(playback.playingToEnd());
    playback.play(kRate, kCount, 1000, -2);
    EXPECT_TRUE(playback.playingToEnd());
    EXPECT_EQ(player.plays.back(), (PlayRange{44100, 0}));
    EXPECT_TRUE(playback.timerRunning());
}

// AudioDisplay::Stop: while playing it remembers where (GetMSAtSample) and
// stops; otherwise it plays from there to the last end again.
TEST(AudioPlayback, StopRemembersAndReplays)
{
    FakePlayer player;
    AudioPlayback playback(player);
    // nothing played yet: legacy's defaults, 0 to 5000 ms
    auto replay = playback.stop(kRate, kCount);
    ASSERT_TRUE(replay);
    EXPECT_EQ(*replay, (PlayRange{0, 220500}));
    EXPECT_EQ(player.stops, 0);

    playback.play(kRate, kCount, 1234, 5678);
    player.pos = 66150; // 1.5 s
    EXPECT_FALSE(playback.stop(kRate, kCount));
    EXPECT_EQ(player.stops, 1);
    EXPECT_FALSE(player.on);
    EXPECT_EQ(playback.lastPositionMs(), 1500);
    EXPECT_FALSE(playback.timerRunning());
    // Stop again: from 1500 ms to the last end
    replay = playback.stop(kRate, kCount);
    ASSERT_TRUE(replay);
    EXPECT_EQ(*replay, (PlayRange{66150, 250399 - 66150}));
    EXPECT_TRUE(player.on);

    // a position past the end (the silence after it) replays nothing
    player.pos = 260000; // 5895.69 ms
    playback.stop(kRate, kCount);
    EXPECT_EQ(playback.lastPositionMs(), 5895);
    replay = playback.stop(kRate, kCount);
    EXPECT_EQ(*replay, (PlayRange{259969, 0}));

    // after Play to the end, Stop's replay plays to the end too
    playback.play(kRate, kCount, 2000, -1);
    player.pos = 132300;
    playback.stop(kRate, kCount);
    replay = playback.stop(kRate, kCount);
    EXPECT_EQ(*replay, (PlayRange{132300, kCount - 1 - 132300}));
    EXPECT_TRUE(playback.playingToEnd());

    // a player that stopped by itself is not stopped again: Stop replays
    player.on = false;
    const int stops = player.stops;
    replay = playback.stop(kRate, kCount);
    ASSERT_TRUE(replay);
    EXPECT_EQ(player.stops, stops);
}

// UpdateTimer: the cursor at the position inside the range and the view.
TEST(AudioPlayback, CursorFollowsThePosition)
{
    FakePlayer player;
    AudioPlayback playback(player);
    AudioView view = minuteView();
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::None); // no timer before a play
    playback.play(48000, 48000 * 60, 1000, 40000); // frames 48000..1920000

    // at the start itself: not drawn (the position must pass the start)
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Cursor);
    EXPECT_FALSE(playback.cursorPainted());

    player.pos = 144000; // 3 s: column 100
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Cursor);
    EXPECT_TRUE(playback.cursorPainted());
    EXPECT_FLOAT_EQ(playback.cursorX(), 100.f);
    player.pos = 144720;
    playback.tick(view, 16);
    EXPECT_FLOAT_EQ(playback.cursorX(), 100.5f);

    // within 50 columns of the right edge: the view moves to 50 columns
    // before the position; the image is drawn with the cursor still at -1
    player.pos = 1400000; // column 972.2
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Image);
    EXPECT_EQ(view.position(), 1328000 / 1440);
    EXPECT_TRUE(playback.cursorPainted());
    EXPECT_FLOAT_EQ(playback.cursorX(), -1.f);
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Cursor);
    EXPECT_NEAR(playback.cursorX(), 1400000.0 / 1440 - 922, 1e-3);
}

// Near the audio's start the view cannot move left of 0: legacy moves it to
// 0 on every tick and never draws the cursor in the first 50 columns.
TEST(AudioPlayback, CursorNotDrawnInTheFirstFiftyColumns)
{
    FakePlayer player;
    AudioPlayback playback(player);
    AudioView view = minuteView();
    playback.play(48000, 48000 * 60, 0, 10000);
    player.pos = 48000; // column 33.3
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Image);
    EXPECT_EQ(view.position(), 0);
    EXPECT_FLOAT_EQ(playback.cursorX(), -1.f);
    player.pos = 72000 + 1440; // column 51
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Cursor);
    EXPECT_FLOAT_EQ(playback.cursorX(), 51.f);
}

// What happens at the end: no cursor from the end on; the player is
// stopped once more than 8192 frames past it, and the timer with it.
TEST(AudioPlayback, StopsPastTheEnd)
{
    FakePlayer player;
    AudioPlayback playback(player);
    AudioView view = minuteView();
    playback.play(48000, 48000 * 60, 1000, 3000); // 48000..144000
    player.pos = 100000;
    playback.tick(view, 16);
    EXPECT_TRUE(playback.cursorPainted());
    player.pos = 144000;
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::Cursor);
    EXPECT_FALSE(playback.cursorPainted());
    EXPECT_TRUE(player.on);
    player.pos = 144000 + 8192;
    playback.tick(view, 16);
    EXPECT_TRUE(player.on);
    EXPECT_TRUE(playback.timerRunning());
    player.pos = 144000 + 8193;
    playback.tick(view, 16);
    EXPECT_FALSE(player.on);
    EXPECT_FALSE(playback.timerRunning());
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::None);
    // the stop at the end is not a Stop: the remembered position stays
    EXPECT_EQ(playback.lastPositionMs(), 0);
}

// A player that stopped by itself: the cursor flag is cleared, no redraw.
TEST(AudioPlayback, StoppedPlayerClearsTheCursorWithoutARedraw)
{
    FakePlayer player;
    AudioPlayback playback(player);
    AudioView view = minuteView();
    playback.play(48000, 48000 * 60, 1000, 1050);
    player.pos = 49000;
    playback.tick(view, 16);
    ASSERT_TRUE(playback.cursorPainted());
    player.on = false;
    EXPECT_EQ(playback.tick(view, 16), AudioPlayback::Redraw::None);
    EXPECT_FALSE(playback.cursorPainted());
    EXPECT_TRUE(playback.timerRunning());
}

// PlaybackVolumeFromSlider and ApplyVolume.
TEST(AudioPlayback, Volume)
{
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(0), 0.f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(25), 0.125f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(50), 1.f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(75), 1.25f);
    EXPECT_FLOAT_EQ(playbackVolumeFromSlider(100), 1.5f);

    std::vector<std::int16_t> s{1000, -1001, -1000, 30000, -30000, 3};
    auto one = s;
    applyLegacyVolume(one.data(), static_cast<std::int64_t>(one.size()), 1.0);
    EXPECT_EQ(one, s);
    auto loud = s;
    applyLegacyVolume(loud.data(), static_cast<std::int64_t>(loud.size()), 1.5);
    EXPECT_EQ(loud, (std::vector<std::int16_t>{1500, -1501, -1499, 32767, -32768, 5}));
    // + 0.5 then truncated towards zero: negative halves go up
    auto soft = s;
    applyLegacyVolume(soft.data(), static_cast<std::int64_t>(soft.size()), 0.3);
    EXPECT_EQ(soft, (std::vector<std::int16_t>{300, -299, -299, 9000, -8999, 1}));
}

// ReadCache for playback: interleaved frames, zero past the audio's end and
// past what is decoded.
TEST(AudioPlayback, CachedFramesForPlayback)
{
    DisplayAudio audio(48000, 6);
    const std::vector<std::int16_t> frames{1, -1, 2, -2, 3, -3, 4, -4};
    ASSERT_TRUE(audio.appendFrames(frames.data(), 4, 2));
    EXPECT_EQ(audio.channels(), 2);
    std::vector<std::int16_t> out(12, 99);
    audio.readFrames(2, 6, out.data()); // frames 2, 3 decoded; 4, 5 not yet; 6, 7 past the end
    EXPECT_EQ(out, (std::vector<std::int16_t>{3, -3, 4, -4, 0, 0, 0, 0, 0, 0, 0, 0}));
    out.assign(4, 99);
    audio.readFrames(-1, 2, out.data());
    EXPECT_EQ(out, (std::vector<std::int16_t>{0, 0, 1, -1}));
    // more appended than the audio's length (a positive delay's tail): not played
    ASSERT_TRUE(audio.appendFrames(frames.data(), 4, 2));
    out.assign(6, 99);
    audio.readFrames(5, 3, out.data());
    EXPECT_EQ(out, (std::vector<std::int16_t>{2, -2, 0, 0, 0, 0}));
    // blank audio: one channel of silence
    const DisplayAudio blank = DisplayAudio::silence(44100, 1000);
    EXPECT_EQ(blank.channels(), 1);
    out.assign(3, 99);
    blank.readFrames(0, 3, out.data());
    EXPECT_EQ(out, (std::vector<std::int16_t>{0, 0, 0}));
}
