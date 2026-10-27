// A4: the audio box's player through the editor output, against legacy
// DirectSoundPlayer2 and ProviderFFMS2::GetPlaybackBuffer at 20d647c4. A
// device the test drives renders the real OutputEngine buffer by buffer with
// chosen stream, DAC and monotonic times, so the samples handed over and the
// position read back are exact; no sound is made.
#include "hikari/backends/audio_box_player.h"
#include "hikari/backends/audio_output_engine.h"
#include "hikari/backends/portaudio_output.h"
#include "hikari/backends/simulated_output.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <mutex>
#include <algorithm>
#include <optional>
#include <thread>

using namespace hikari;
using namespace hikari::application;
using backends::AudioBoxPlayer;

namespace {

// A device whose callbacks the test makes: 256-frame buffers, a DAC time
// 50 ms after the stream time, monotonic time = 1000 s + stream time; "now"
// is the end of the last buffer rendered plus nowOffset.
class ManualDevice : public AudioOutputPort {
public:
    std::optional<OutputFormat> refuseRateWith; // the rate the device takes instead
    bool refuseOpen = false;
    int opens = 0, closes = 0, starts = 0;
    std::vector<float> rendered;
    double stream = 0;
    // H2: the host's list (devices() is a rescan) and the ids opened.
    std::vector<OutputDevice> list;
    std::vector<std::string> refuseIds; // listed, but refusing to open
    int rescans = 0;
    std::vector<std::string> opened;

    std::vector<OutputDevice> devices() override
    {
        ++rescans;
        return list;
    }
    std::expected<OutputFormat, OutputError> open(const std::string &id, OutputFormat format) override
    {
        ++opens;
        if (refuseOpen)
            return std::unexpected(OutputError::NoDevice);
        if (std::ranges::find(refuseIds, id) != refuseIds.end())
            return std::unexpected(OutputError::DeviceUnavailable);
        opened.push_back(id);
        if (refuseRateWith)
            format.sampleRate = refuseRateWith->sampleRate;
        m_engine.emplace(format, 0.05, 0.5);
        return format;
    }
    std::expected<void, OutputError> start() override
    {
        ++starts;
        m_engine->started();
        return {};
    }
    void stop() override
    {
        if (!m_engine)
            return;
        m_engine->stopRequested();
        m_engine->streamFinished();
        m_engine->stopped();
    }
    void close() override
    {
        ++closes;
        stop();
        m_engine.reset();
    }
    OutputFormat format() const override { return m_engine ? m_engine->format() : OutputFormat{}; }
    std::size_t write(std::span<const float> s) override { return m_engine ? m_engine->write(s) : 0; }
    OutputStatus status() const override { return m_engine ? m_engine->status() : OutputStatus{}; }
    ClockEstimate clock() const override { return m_engine ? m_engine->clock(now()) : ClockEstimate{}; }

    // One callback of `frames` frames.
    void render(int frames = 256)
    {
        std::vector<float> out(static_cast<std::size_t>(frames * m_engine->format().channels));
        if (!m_engine->render(out, {stream + 0.05, stream, 1000 + stream, false}))
            m_engine->streamFinished(); // the host ends an aborted stream
        rendered.insert(rendered.end(), out.begin(), out.end());
        stream += double(frames) / m_engine->format().sampleRate;
    }
    void loseDevice() { m_engine->injectLoss(); }
    double now() const { return 1000 + stream + nowOffset; }
    double nowOffset = 0; // time since the last callback began

private:
    std::optional<backends::OutputEngine> m_engine;
};

// Stereo frames whose left sample is the frame number (mod 30000) and
// right its negation.
DisplayAudio stereoRamp(int rate, std::int64_t frames)
{
    DisplayAudio audio(rate, frames);
    std::vector<std::int16_t> data(static_cast<std::size_t>(frames * 2));
    for (std::int64_t i = 0; i < frames; i++) {
        data[static_cast<std::size_t>(2 * i)] = static_cast<std::int16_t>(i % 30000);
        data[static_cast<std::size_t>(2 * i + 1)] = static_cast<std::int16_t>(-(i % 30000));
    }
    EXPECT_TRUE(audio.appendFrames(data.data(), frames, 2));
    audio.finish();
    return audio;
}

struct BoxPlayer : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "audio_box_player_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    void SetUp() override
    {
        player.setPumping(AudioBoxPlayer::Pumping::Manual); // the test makes each callback
        player.setClock([this] { return device.now(); });
        QObject::connect(&player, &AudioBoxPlayer::failed, [this](const QString &m) { failures << m; });
    }
    // Callbacks with the player filling between them.
    void run(int buffers)
    {
        for (int i = 0; i < buffers; i++) {
            device.render();
            player.pump();
        }
    }

    ManualDevice device;
    std::optional<DisplayAudio> audio = stereoRamp(48000, 48000 * 10);
    AudioBoxPlayer player{device, [this]() -> const DisplayAudio * { return audio ? &*audio : nullptr; }};
    QStringList failures;
};

} // namespace

// The range's frames, sample for sample (S16 / 32768), then silence: the
// output is opened at the audio's rate and channels.
TEST_F(BoxPlayer, PlaysTheRangeThenSilence)
{
    // 4800 frames: legacy's 100 ms, the shortest range that loops
    player.play(1000, 4800);
    ASSERT_TRUE(player.playing());
    EXPECT_EQ(device.opens, 1);
    EXPECT_EQ(player.outputFormat(), (OutputFormat{48000, 2}));
    EXPECT_EQ(device.starts, 1);
    run(24); // 6144 frames
    ASSERT_GE(device.rendered.size(), 6144u * 2);
    for (int i = 0; i < 4800; i++) {
        ASSERT_FLOAT_EQ(device.rendered[2 * i], (1000 + i) / 32768.0f) << i;
        ASSERT_FLOAT_EQ(device.rendered[2 * i + 1], -(1000 + i) / 32768.0f) << i;
    }
    for (std::size_t i = 9600; i < device.rendered.size(); i++)
        ASSERT_EQ(device.rendered[i], 0.0f) << i;
    // the silence keeps flowing: no underrun, still playing
    EXPECT_EQ(device.status().underruns, 0u);
    EXPECT_TRUE(player.playing());
    // the same format plays again without reopening
    player.play(0, 4800);
    EXPECT_EQ(device.opens, 1);
}

// AUDIO_VOLUME's factor on the cached samples (legacy ApplyVolume).
TEST_F(BoxPlayer, AppliesTheVolume)
{
    player.setVolume(1.5);
    player.play(1001, 4800);
    run(1);
    EXPECT_FLOAT_EQ(device.rendered[0], 1502 / 32768.0f);  // 1501.5 + 0.5
    EXPECT_FLOAT_EQ(device.rendered[1], -1501 / 32768.0f); // -1501.5 + 0.5, truncated
}

// The position: the frames the device took, moved on by the time since that
// callback, less the DAC latency; the start before the output reports, 0
// once stopped; past the end into the silence.
TEST_F(BoxPlayer, PositionFollowsTheOutputClock)
{
    player.play(48000, 48000);
    EXPECT_EQ(player.position(), 48000); // nothing rendered yet
    EXPECT_EQ(player.startPosition(), 48000);
    EXPECT_EQ(player.endPosition(), 96000);
    run(20); // 5120 frames taken, the last buffer from 4864
    // at the last buffer's end, less 50 ms of latency: 5120 - 2400
    EXPECT_NEAR(player.position(), 48000 + 2720, 1);
    // 30 ms later
    device.nowOffset = 0.030;
    EXPECT_NEAR(player.position(), 48000 + 2720 + 1440, 1);
    // never past the frames taken
    device.nowOffset = 0.1;
    EXPECT_EQ(player.position(), 48000 + 5120);
    // a callback older than the stall limit: no estimate, the start
    device.nowOffset = 1.0;
    EXPECT_EQ(player.position(), 48000);
    device.nowOffset = 0;
    // a time before the first frame sounds: the start
    device.stream = 0;
    EXPECT_EQ(player.position(), 48000);
    player.stop();
    EXPECT_FALSE(player.playing());
    EXPECT_EQ(player.position(), 0);
}

TEST_F(BoxPlayer, PositionPassesTheEndIntoTheSilence)
{
    player.play(0, 4800);
    run(80); // 20480 frames
    device.nowOffset = 0.05;
    EXPECT_NEAR(player.position(), 20480, 1);
    EXPECT_GT(player.position(), player.endPosition() + 8192);
    EXPECT_TRUE(player.playing()); // stopping there is the timer's (AudioPlayback)
}

// Legacy's single-buffer playback: a range shorter than 100 ms (4800 frames
// at 48 kHz stereo) stops by itself once played; an empty range loops.
TEST_F(BoxPlayer, ShortRangeStopsItself)
{
    player.play(0, 4799);
    run(18); // 4608 frames
    EXPECT_TRUE(player.playing());
    run(1); // 4864
    EXPECT_FALSE(player.playing());
    EXPECT_EQ(player.position(), 0);

    player.play(0, 4800);
    run(40);
    EXPECT_TRUE(player.playing());
    player.play(100, 0);
    run(40);
    EXPECT_TRUE(player.playing());
}

// Legacy SetEndFrame: stopped when the frames written reach the new end.
TEST_F(BoxPlayer, SetEndPosition)
{
    player.play(0, 48000 * 5);
    run(2);
    EXPECT_GT(player.nextFrame(), 2048);
    player.setEndPosition(400000);
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(player.endPosition(), 400000);
    player.setEndPosition(1000);
    EXPECT_FALSE(player.playing());
}

// A device that refuses the audio's rate: the frames are resampled linearly
// to the rate it takes, and the position maps back to the audio's rate.
TEST_F(BoxPlayer, ResamplesToTheDeviceRate)
{
    audio = stereoRamp(24000, 24000 * 10);
    device.refuseRateWith = OutputFormat{48000, 2};
    player.play(100, 2400);
    EXPECT_EQ(player.sourceFormat(), (OutputFormat{24000, 2}));
    EXPECT_EQ(player.outputFormat(), (OutputFormat{48000, 2}));
    run(10);
    for (int k = 0; k < 400; k++)
        ASSERT_NEAR(device.rendered[2 * k], (100 + k * 0.5) / 32768.0, 1e-7) << k;
    device.nowOffset = 0.05; // 2560 output frames heard: 1280 of the audio's
    EXPECT_NEAR(player.position(), 100 + 1280, 1);
}

// One channel plays as one channel.
TEST_F(BoxPlayer, MonoAudio)
{
    audio.emplace(44100, 44100);
    std::vector<std::int16_t> data(44100, 1000);
    ASSERT_TRUE(audio->appendFrames(data.data(), 44100, 1));
    player.play(0, 44100);
    EXPECT_EQ(player.outputFormat(), (OutputFormat{44100, 1}));
    run(1);
    EXPECT_EQ(device.rendered.size(), 256u);
    EXPECT_FLOAT_EQ(device.rendered[0], 1000 / 32768.0f);
    // other audio with another format opens the output again
    audio = stereoRamp(48000, 48000);
    player.play(0, 4800);
    EXPECT_EQ(device.opens, 2);
    EXPECT_EQ(device.closes, 1);
}

// Failures are logged as legacy's player logged them.
TEST_F(BoxPlayer, FailuresAreLogged)
{
    device.refuseOpen = true;
    player.play(0, 4800);
    EXPECT_FALSE(player.playing());
    EXPECT_EQ(failures, QStringList{QStringLiteral("Failed creating audio playback device.")});
    device.refuseOpen = false;
    player.play(0, 48000);
    ASSERT_TRUE(player.playing());
    EXPECT_EQ(device.opens, 2);
    // H2: a device lost with none to reopen on fails after the retries
    // (LosingTheDeviceInUseGoesOnOnTheDefault for the recovery)
    player.setRecovery(2, 1);
    device.refuseOpen = true;
    device.loseDevice();
    device.render();
    player.pump();
    QElapsedTimer t;
    t.start();
    while (failures.size() < 2 && t.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    EXPECT_FALSE(player.playing());
    EXPECT_EQ(failures.last(), QStringLiteral("Audio player failed."));
    EXPECT_EQ(device.opens, 4);
    // the next play opens the device again
    device.refuseOpen = false;
    player.play(0, 48000);
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.opens, 5);
    // without audio nothing plays
    audio.reset();
    player.stop();
    player.play(0, 100);
    EXPECT_FALSE(player.playing());
}

// The output without a device keeps the device's pace on its own clock.
TEST_F(BoxPlayer, SimulatedOutputRunsInRealTime)
{
    backends::SimulatedOutput output;
    AudioBoxPlayer simulated(output, [this]() -> const DisplayAudio * { return &*audio; });
    simulated.play(48000, 48000);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    const auto position = simulated.position();
    // about 300 ms in, less the simulated 20 ms latency
    EXPECT_GT(position, 48000 + 48000 * 0.2);
    EXPECT_LT(position, 48000 + 48000 * 0.35);
    simulated.stop();
    EXPECT_EQ(output.status().underruns, 0u);
    EXPECT_FALSE(output.status().running);
}

// The fill runs on the player's own thread (legacy DirectSoundPlayer2Thread):
// an owner thread that does nothing for longer than the 250 ms kept ahead
// (no events processed) does not starve the output.
TEST_F(BoxPlayer, TheFillThreadFeedsTheOutputWhileTheOwnerIsBusy)
{
    backends::SimulatedOutput output;
    AudioBoxPlayer threaded(output, [this]() -> const DisplayAudio * { return &*audio; });
    threaded.play(0, 48000 * 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const auto position = threaded.position();
    EXPECT_GT(position, 48000 * 0.6);
    EXPECT_LT(position, 48000 * 1.0);
    EXPECT_GT(threaded.nextFrame(), position); // filled ahead of what is heard
    threaded.stop();
    EXPECT_EQ(output.status().underruns, 0u);
    // a range the thread plays to its end: the short one stops by itself
    threaded.play(0, 2400);
    QElapsedTimer t;
    t.start();
    while (threaded.playing() && t.elapsed() < 2000)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(threaded.playing());
}

// The audio stays readable while it plays even when its owner lets it go
// (the box closed or replaced it): the player holds it until it stops.
TEST_F(BoxPlayer, ThePlayedAudioIsHeldUntilTheStop)
{
    auto shared = std::make_shared<DisplayAudio>(stereoRamp(48000, 48000));
    std::weak_ptr<const DisplayAudio> watch = shared;
    std::shared_ptr<const DisplayAudio> box = shared;
    shared.reset();
    AudioBoxPlayer held([this]() -> AudioOutputPort & { return device; },
                        AudioBoxPlayer::SharedAudio([&box] { return box; }));
    held.setPumping(AudioBoxPlayer::Pumping::Manual);
    held.play(0, 48000);
    box.reset(); // the box lets it go
    EXPECT_FALSE(watch.expired());
    device.render();
    held.pump();
    held.stop();
    EXPECT_TRUE(watch.expired());
}

// H2 (#125, decision 60): the output follows the system's devices. A change
// while idle (the default changed, A10 in the H2 evidence: a new audition
// still played on the old default) closes the output; the next play rescans
// the host, so PortAudio sees the new default, and opens it.
TEST_F(BoxPlayer, ADefaultChangeWhileIdleReopensAtTheNextPlay)
{
    player.play(0, 4800);
    run(4);
    player.stop();
    ASSERT_EQ(device.opens, 1);
    EXPECT_EQ(device.rescans, 0);
    player.switchOutput();
    EXPECT_EQ(device.closes, 1); // let go at once
    EXPECT_FALSE(player.playing());
    player.play(0, 4800);
    EXPECT_EQ(device.rescans, 1); // the host rescanned before the open
    EXPECT_EQ(device.opens, 2);
    EXPECT_EQ(device.opened.back(), ""); // the (new) default
    run(4);
    EXPECT_FLOAT_EQ(device.rendered[device.rendered.size() - 1024 * 2 + 2], 1 / 32768.0f); // frame 1, left
    // without a change the open output is kept
    player.play(0, 4800);
    EXPECT_EQ(device.opens, 2);
    EXPECT_EQ(device.rescans, 1);
}

// A change while playing (A9/B6: the default changed during playback) reopens
// at once after a rescan and goes on from the frame being heard, with no
// failure logged.
TEST_F(BoxPlayer, ADefaultChangeWhilePlayingGoesOnFromThePosition)
{
    player.play(48000, 48000 * 5);
    run(40); // 10240 frames taken
    const auto heard = player.position();
    EXPECT_NEAR(heard, 48000 + 10240 - 2400, 1);
    const std::size_t before = device.rendered.size();
    player.switchOutput();
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.rescans, 1);
    EXPECT_EQ(device.opens, 2);
    EXPECT_EQ(device.closes, 1);
    EXPECT_EQ(device.starts, 2);
    EXPECT_EQ(player.position(), heard); // until the new output reports
    EXPECT_EQ(player.startPosition(), 48000);
    EXPECT_EQ(player.endPosition(), 48000 * 6);
    run(8);
    // the new device's first frame is the one that was being heard
    ASSERT_GE(device.rendered.size(), before + 2048 * 2);
    for (std::size_t i = 0; i < 2048; i++)
        ASSERT_FLOAT_EQ(device.rendered[before + 2 * i], float((heard + std::int64_t(i)) % 30000) / 32768.0f) << i;
    device.nowOffset = 0.05;
    EXPECT_NEAR(player.position(), heard + 2048, 1);
    EXPECT_EQ(device.status().underruns, 0u);
    EXPECT_TRUE(failures.isEmpty());
}

// The device in use goes away while playing (A11 on WASAPI: the stream ended
// and "Audio player failed." was shown): the player reopens on the default
// and goes on from where the device stopped taking frames, without a failure.
TEST_F(BoxPlayer, LosingTheDeviceInUseGoesOnOnTheDefault)
{
    player.play(0, 48000 * 5);
    run(40); // 10240 frames taken
    device.loseDevice();
    device.render(); // the host aborts the stream
    player.pump();
    EXPECT_TRUE(player.playing());
    QCoreApplication::processEvents(); // the recovery runs on the owner's thread
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.opens, 2);
    EXPECT_EQ(device.rescans, 1);
    EXPECT_TRUE(failures.isEmpty());
    const auto resumed = player.position();
    EXPECT_GE(resumed, 10240 - 2400);
    EXPECT_LE(resumed, 10240);
    const std::size_t before = device.rendered.size();
    run(4);
    EXPECT_FLOAT_EQ(device.rendered[before], float(resumed % 30000) / 32768.0f);
    EXPECT_TRUE(failures.isEmpty());
}

// No device to reopen on yet (Windows had not picked a new default): the
// player waits on the frame it stopped at and goes on when the devices
// change.
TEST_F(BoxPlayer, ALostDeviceWaitsForTheNextOne)
{
    player.setRecovery(100, 1000);
    player.play(0, 48000 * 5);
    run(40);
    device.refuseOpen = true;
    device.loseDevice();
    device.render();
    player.pump();
    QCoreApplication::processEvents();
    EXPECT_TRUE(player.playing());
    const auto waiting = player.position();
    EXPECT_GT(waiting, 0);
    device.refuseOpen = false;
    player.switchOutput(); // a device arrived
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(player.position(), waiting);
    const std::size_t before = device.rendered.size();
    run(4);
    EXPECT_FLOAT_EQ(device.rendered[before], float(waiting % 30000) / 32768.0f);
    EXPECT_TRUE(failures.isEmpty());
}

// A chosen device opens by its name in the default's host API (the system's
// name for it, Settings > Audio); while it is absent, or when it refuses, the
// default plays instead, with no failure.
TEST_F(BoxPlayer, AChosenDeviceOpensByNameElseTheDefault)
{
    device.list = {{"Windows DirectSound/3/Speakers (Razer BlackShark V2 X USB)", "Speakers (Razer BlackShark V2 X USB)",
                    "Windows DirectSound", false, 2, 48000},
                   {"Windows WASAPI/7/22E1W (NVIDIA High Definition Audio)", "22E1W (NVIDIA High Definition Audio)",
                    "Windows WASAPI", true, 2, 48000},
                   {"Windows WASAPI/8/Speakers (Razer BlackShark V2 X USB)", "Speakers (Razer BlackShark V2 X USB)",
                    "Windows WASAPI", false, 2, 48000}};
    // missing at start: the default, no failure
    player.setOutputDevice("Realtek Digital Output (Realtek(R) Audio)");
    player.play(0, 4800);
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.opened.back(), "");
    EXPECT_TRUE(failures.isEmpty());
    player.stop();
    // connected: that device, in the default's host API
    player.setOutputDevice("Speakers (Razer BlackShark V2 X USB)");
    player.switchOutput();
    player.play(0, 48000);
    EXPECT_EQ(device.opened.back(), "Windows WASAPI/8/Speakers (Razer BlackShark V2 X USB)");
    run(4);
    // unplugged while playing: the default
    device.list.pop_back();
    player.switchOutput();
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.opened.back(), "");
    // listed but refusing to open: the default
    device.list.push_back({"Windows WASAPI/9/Speakers (Razer BlackShark V2 X USB)",
                           "Speakers (Razer BlackShark V2 X USB)", "Windows WASAPI", false, 2, 48000});
    device.refuseIds = {"Windows WASAPI/9/Speakers (Razer BlackShark V2 X USB)"};
    player.switchOutput();
    EXPECT_TRUE(player.playing());
    EXPECT_EQ(device.opened.back(), "");
    // back to the system default
    device.refuseIds.clear();
    player.setOutputDevice("");
    player.switchOutput();
    EXPECT_EQ(device.opened.back(), "");
    EXPECT_TRUE(failures.isEmpty());
}

// The fill thread notices the loss; the reopen runs on the owner's thread
// while playback goes on there.
TEST_F(BoxPlayer, TheFillThreadRecoversALostDevice)
{
    backends::SimulatedOutput output;
    AudioBoxPlayer threaded(output, [this]() -> const DisplayAudio * { return &*audio; });
    QStringList threadedFailures;
    QObject::connect(&threaded, &AudioBoxPlayer::failed, [&](const QString &m) { threadedFailures << m; });
    threaded.play(0, 48000 * 3);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 200)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    output.simulateDeviceLoss();
    t.restart();
    while (t.elapsed() < 300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    EXPECT_TRUE(threaded.playing());
    EXPECT_TRUE(output.status().running);
    EXPECT_FALSE(output.status().deviceLost);
    EXPECT_GT(threaded.position(), 48000 * 0.2);
    threaded.stop();
    EXPECT_TRUE(threadedFailures.isEmpty());
}

// KA2-slowed-playback (#277): below speed 1 the frames go through SoundTouch.
namespace {

// Mono 16-bit frames of `samples`.
DisplayAudio monoAudio(int rate, const std::vector<std::int16_t> &samples)
{
    DisplayAudio audio(rate, static_cast<std::int64_t>(samples.size()));
    EXPECT_TRUE(audio.appendFrames(samples.data(), static_cast<std::int64_t>(samples.size()), 1));
    audio.finish();
    return audio;
}

// Decaying 700 Hz bursts, 25 ms long, at `onsets`.
std::vector<std::int16_t> bursts(std::int64_t frames, const std::vector<std::int64_t> &onsets)
{
    std::vector<std::int16_t> s(static_cast<std::size_t>(frames), 0);
    for (const auto x : onsets)
        for (std::int64_t i = x; i < std::min(frames, x + 1200); i++)
            s[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
                16000 * std::sin(2 * 3.14159265358979 * 700 * double(i) / 48000) * std::exp(-double(i - x) / 600));
    return s;
}

// The first rendered frame from `from` louder than 0.05.
std::int64_t firstLoud(const std::vector<float> &rendered, std::int64_t from)
{
    for (auto i = static_cast<std::size_t>(std::max<std::int64_t>(0, from)); i < rendered.size(); i++)
        if (std::fabs(rendered[i]) > 0.05f)
            return static_cast<std::int64_t>(i);
    return -1;
}

// The frequency of the rendered mono frames [from, from + count) by their
// upward zero crossings.
double crossingHz(const std::vector<float> &rendered, std::size_t from, std::size_t count, int rate)
{
    std::vector<double> ups;
    for (std::size_t i = from + 1; i < from + count && i < rendered.size(); i++)
        if (rendered[i - 1] < 0 && rendered[i] >= 0)
            ups.push_back(double(i - 1) + rendered[i - 1] / (rendered[i - 1] - rendered[i]));
    if (ups.size() < 2)
        return 0;
    return double(ups.size() - 1) * rate / (ups.back() - ups.front());
}

} // namespace

TEST_F(BoxPlayer, SpeedIsClampedAndOneLeavesTheFramesAlone)
{
    EXPECT_EQ(player.speed(), 1.0);
    player.setSpeed(0.1);
    EXPECT_EQ(player.speed(), 0.25);
    player.setSpeed(2);
    EXPECT_EQ(player.speed(), 1.0);
    player.play(1000, 4800);
    run(4);
    for (int i = 0; i < 1024; i++)
        ASSERT_FLOAT_EQ(device.rendered[2 * i], (1000 + i) / 32768.0f) << i;
}

// The range lasts 1 / speed as long and a tone keeps its pitch.
TEST_F(BoxPlayer, SlowedPlaybackKeepsThePitch)
{
    std::vector<std::int16_t> tone(48000 * 2);
    for (std::size_t i = 0; i < tone.size(); i++)
        tone[i] = static_cast<std::int16_t>(12000 * std::sin(2 * 3.14159265358979 * 440 * double(i) / 48000));
    for (const double speed : {0.25, 1.0 / 3.0, 0.5, 0.75}) {
        SCOPED_TRACE(speed);
        audio = monoAudio(48000, tone);
        device.rendered.clear();
        player.setSpeed(speed);
        player.play(0, 48000);
        run(static_cast<int>(48000 / speed / 256) + 60);
        // the second of audio sounds for 1 / speed seconds, then silence
        const auto loud = static_cast<double>(std::ranges::count_if(device.rendered, [](float v) {
            return std::fabs(v) > 0.01f;
        }));
        EXPECT_NEAR(loud / 48000, 1 / speed, 0.06 / speed);
        EXPECT_NEAR(crossingHz(device.rendered, 9600, 24000, 48000), 440, 4.4);
        player.stop();
    }
}

// The position at the frame where a sound is first heard is that sound's
// frame in the audio, within 5 ms, at every speed the panel offers.
TEST_F(BoxPlayer, SlowedPositionFindsTheSoundsInTheAudio)
{
    std::vector<std::int64_t> onsets;
    for (int k = 0; k < 8; k++)
        onsets.push_back(24000 + k * 23000 + (k * 7919) % 5000);
    const auto samples = bursts(48000 * 5, onsets);
    for (const double speed : {0.25, 1.0 / 3.0, 0.5, 0.75}) {
        SCOPED_TRACE(speed);
        audio = monoAudio(48000, samples);
        player.setSpeed(speed);
        // where each burst is first heard
        device.rendered.clear();
        player.play(0, 48000 * 5);
        run(static_cast<int>(onsets.back() / speed / 256) + 40);
        player.stop();
        std::vector<std::int64_t> heard;
        for (const auto x : onsets)
            heard.push_back(firstLoud(device.rendered, static_cast<std::int64_t>((x - 4800) / speed)));
        // the same playback again, stopping the clock at each of them
        device.rendered.clear();
        const double start = device.stream;
        player.play(0, 48000 * 5);
        std::size_t k = 0;
        while (k < onsets.size()) {
            device.render();
            player.pump();
            const auto taken = static_cast<std::int64_t>(std::llround((device.stream - start) * 48000));
            // nowOffset 0 hears the frame 50 ms (2400 frames) before the taken ones
            while (k < onsets.size() && taken - 2400 >= heard[k]) {
                device.nowOffset = double(heard[k] - (taken - 2400)) / 48000;
                EXPECT_NEAR(player.position(), onsets[k], 240) << "burst " << k;
                device.nowOffset = 0;
                k++;
            }
        }
        player.stop();
    }
}

// A change while playing goes on from the frame being heard.
TEST_F(BoxPlayer, ASpeedChangeGoesOnFromTheFrameBeingHeard)
{
    player.play(0, 48000 * 5);
    run(40); // 10240 frames taken, 7840 heard
    EXPECT_NEAR(player.position(), 7840, 1);
    player.setSpeed(0.5);
    EXPECT_TRUE(player.playing());
    EXPECT_NEAR(player.position(), 7840, 1); // the start again until the output reports
    run(80); // 20480 taken: 18080 output frames heard, half as many of the audio, plus the lead
    EXPECT_NEAR(player.position(), 7840 + 9040 + AudioBoxPlayer::stretchLeadMs(0.5) * 48, 2);
    player.setSpeed(1);
    run(40);
    EXPECT_GT(player.position(), 7840 + 9040);
}

// The calibration's beeps play instead of the box's audio, at speed 1 while
// the box's plays slowed; the next play is the box's again.
TEST_F(BoxPlayer, AClipPlaysAtSpeedOne)
{
    player.setSpeed(0.5);
    auto clip = std::make_shared<DisplayAudio>(stereoRamp(48000, 4800));
    ASSERT_TRUE(player.playClip(clip));
    run(4);
    for (int i = 0; i < 1024; i++)
        ASSERT_FLOAT_EQ(device.rendered[2 * i], i / 32768.0f) << i;
    EXPECT_EQ(player.endPosition(), 4800);
    EXPECT_FALSE(player.playClip(nullptr));
    device.rendered.clear();
    player.play(1000, 48000);
    run(40);
    EXPECT_EQ(player.speed(), 0.5);
    EXPECT_GT(std::ranges::count_if(device.rendered, [](float v) { return v != 0; }), 0);
    // stretched: not the ramp's frames one for one
    EXPECT_NE(device.rendered[2 * 5000], (1000 + 5000) / 32768.0f);
}

// FR1: Test sound stops its own tone, never a later playback; a clip longer
// than the wanted latency plays on into silence until it is stopped.
TEST_F(BoxPlayer, PlayingClipNamesTheClipThatPlays)
{
    auto clip = std::make_shared<DisplayAudio>(stereoRamp(48000, 9600)); // 200 ms
    ASSERT_TRUE(player.playClip(clip));
    EXPECT_TRUE(player.playingClip(clip.get()));
    run(40); // past its end
    EXPECT_TRUE(player.playing());
    EXPECT_TRUE(player.playingClip(clip.get()));
    player.play(0, 48000);
    EXPECT_FALSE(player.playingClip(clip.get()));
    player.stop();
    EXPECT_FALSE(player.playingClip(clip.get()));
}

// Through a real stream: the CI null PCM, or a sound device on a desktop or
// the Windows VM, named by HIKARI_TEST_AUDIO_DEVICE (the tone is quiet but
// audible, so never the default speakers unasked). At half speed the stream
// takes two seconds of tone for the second of audio, at its pitch. The time it
// took is printed, not checked: the null PCM has no clock (it takes frames as
// fast as they come, so the tap keeps the first three seconds only).
namespace {

class TappedOutput : public AudioOutputPort {
public:
    explicit TappedOutput(AudioOutputPort &inner) : m_inner(inner) {}
    std::vector<OutputDevice> devices() override { return m_inner.devices(); }
    std::expected<OutputFormat, OutputError> open(const std::string &id, OutputFormat format) override
    {
        auto opened = m_inner.open(id, format);
        if (opened) {
            std::lock_guard lock(m_mutex);
            m_format = *opened;
        }
        return opened;
    }
    std::expected<void, OutputError> start() override { return m_inner.start(); }
    void stop() override { m_inner.stop(); }
    void close() override { m_inner.close(); }
    OutputFormat format() const override { return m_inner.format(); }
    std::size_t write(std::span<const float> interleaved) override
    {
        const std::size_t taken = m_inner.write(interleaved);
        std::lock_guard lock(m_mutex);
        const auto room = std::size_t(3 * m_format.sampleRate * m_format.channels) - m_taken.size();
        const auto kept = static_cast<std::ptrdiff_t>(std::min(taken, room));
        m_taken.insert(m_taken.end(), interleaved.begin(), interleaved.begin() + kept);
        if (std::size_t(kept) == room)
            m_full = true;
        return taken;
    }
    bool full() const { return m_full; }
    OutputFormat opened() const
    {
        std::lock_guard lock(m_mutex);
        return m_format;
    }
    OutputStatus status() const override { return m_inner.status(); }
    ClockEstimate clock() const override { return m_inner.clock(); }
    // The first channel of what the stream took.
    std::vector<float> taken(int channels) const
    {
        std::lock_guard lock(m_mutex);
        std::vector<float> out;
        for (std::size_t i = 0; i < m_taken.size(); i += static_cast<std::size_t>(channels))
            out.push_back(m_taken[i]);
        return out;
    }

private:
    AudioOutputPort &m_inner;
    mutable std::mutex m_mutex;
    OutputFormat m_format{48000, 1};
    std::vector<float> m_taken;
    std::atomic<bool> m_full = false;
};

} // namespace

TEST_F(BoxPlayer, SlowedPlaybackThroughADevice)
{
    const char *wanted = std::getenv("HIKARI_TEST_AUDIO_DEVICE");
    if (!wanted || !*wanted)
        GTEST_SKIP() << "set HIKARI_TEST_AUDIO_DEVICE to run against a real stream";
    backends::PortAudioOutput output;
    std::string name, hostApi;
    for (const auto &d : output.devices())
        if (d.name.find(wanted) != std::string::npos) {
            name = d.name;
            hostApi = d.hostApi;
        }
    ASSERT_FALSE(name.empty()) << "the requested test device is missing";
    TappedOutput tapped(output);
    std::vector<std::int16_t> tone(48000);
    for (std::size_t i = 0; i < tone.size(); i++)
        tone[i] = static_cast<std::int16_t>(3000 * std::sin(2 * 3.14159265358979 * 440 * double(i) / 48000));
    const DisplayAudio audio = monoAudio(48000, tone);
    AudioBoxPlayer real(tapped, [&]() -> const DisplayAudio * { return &audio; });
    QStringList realFailures;
    QObject::connect(&real, &AudioBoxPlayer::failed, [&](const QString &m) { realFailures << m; });
    real.setOutputDevice(name);
    real.setSpeed(0.5);
    QElapsedTimer t;
    t.start();
    real.play(0, 48000);
    ASSERT_TRUE(real.playing()) << realFailures.join(u'\n').toStdString();
    while (real.playing() && !tapped.full() && t.elapsed() < 6000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    const double seconds = t.elapsed() / 1000.0;
    real.stop();
    const auto format = tapped.opened();
    EXPECT_TRUE(realFailures.isEmpty()) << realFailures.join(u'\n').toStdString();
    const auto taken = tapped.taken(format.channels);
    // from the first loud frame to the last
    const auto isLoud = [](float v) { return std::fabs(v) > 0.005f; };
    const auto first = std::ranges::find_if(taken, isLoud);
    const auto last = std::ranges::find_if(taken.rbegin(), taken.rend(), isLoud);
    const double loud = first == taken.end() ? 0.0 : double(std::distance(first, last.base()));
    const double hz = crossingHz(taken, static_cast<std::size_t>(format.sampleRate / 5),
                                 static_cast<std::size_t>(format.sampleRate), format.sampleRate);
    std::fprintf(stderr,
                 "Slowed playback through %s (%s): %d Hz x %d, the tone %.3f s long at %.2f Hz, played %.3f s, "
                 "underruns %llu\n",
                 name.c_str(), hostApi.c_str(), format.sampleRate, format.channels, loud / format.sampleRate, hz,
                 seconds, static_cast<unsigned long long>(output.status().underruns));
    EXPECT_NEAR(loud / format.sampleRate, 2.0, 0.12);
    EXPECT_NEAR(hz, 440, 4.4);
}
