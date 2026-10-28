// I3 (M50-audio, automated parts): a Line's source range through the editor
// output. A simulated device drives the real OutputEngine at a real-time pace
// and records every sample it renders, so sample accuracy and the tail/drain
// report are checked on every host; the PortAudio run needs an output device.
#include "hikari/backends/audio_output_engine.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/line_audition.h"
#include "hikari/backends/portaudio_output.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>

using namespace hikari;
using namespace hikari::application;
using backends::AuditionReport;
using backends::LineAudition;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A device with a 50 ms reported latency rendering 256-frame buffers.
class SimulatedDevice : public AudioOutputPort {
public:
    explicit SimulatedDevice(OutputFormat format) : m_format(format), m_engine(format, 0.05, 0.5)
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(int(256 * 1000 / format.sampleRate));
        QObject::connect(&m_timer, &QTimer::timeout, [this] {
            std::vector<float> out(256 * std::size_t(m_format.channels));
            m_engine.render(out, {m_stream + 0.05, m_stream, now(), false});
            m_stream += 256.0 / m_format.sampleRate;
            rendered.insert(rendered.end(), out.begin(), out.end());
        });
    }
    std::vector<OutputDevice> devices() override { return {}; }
    std::expected<OutputFormat, OutputError> open(const std::string &, OutputFormat) override { return m_format; }
    std::expected<void, OutputError> start() override
    {
        m_engine.started();
        m_timer.start();
        return {};
    }
    void stop() override
    {
        m_engine.stopRequested();
        m_timer.stop();
        m_engine.stopped();
    }
    void close() override { stop(); }
    OutputFormat format() const override { return m_format; }
    std::size_t write(std::span<const float> s) override { return m_engine.write(s); }
    OutputStatus status() const override { return m_engine.status(); }
    ClockEstimate clock() const override { return m_engine.clock(now()); }

    std::vector<float> rendered;

private:
    OutputFormat m_format;
    backends::OutputEngine m_engine;
    QTimer m_timer;
    double m_stream = 100;
};

struct Audition : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "line_audition_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    void SetUp() override
    {
        std::optional<std::expected<SourceTimeline, SourceError>> opened;
        source.open(std::string(HIKARI_MEDIA_FIXTURES) + "/audio.mkv", {}, [&](auto r) { opened = std::move(r); });
        ASSERT_TRUE(waitFor([&] { return opened.has_value(); }));
        ASSERT_TRUE(*opened);
        std::optional<std::expected<AudioInfo, SourceError>> audio;
        source.openAudio((*opened)->firstAudioTrack, [&](auto r) { audio = std::move(r); });
        ASSERT_TRUE(waitFor([&] { return audio.has_value(); }));
        ASSERT_TRUE(*audio);
    }
    std::optional<AuditionReport> play(AudioOutputPort &output, std::int64_t start, std::int64_t count,
                                       std::function<void(LineAudition &)> during = {})
    {
        LineAudition audition(source, output);
        std::optional<AuditionReport> report;
        QObject::connect(&audition, &LineAudition::finished, [&](const AuditionReport &r) { report = r; });
        QObject::connect(&audition, &LineAudition::failed, [&](const QString &why) { ADD_FAILURE() << why.toStdString(); });
        EXPECT_TRUE(audition.play(start, count));
        if (during)
            during(audition);
        waitFor([&] { return report.has_value(); });
        return report;
    }
    backends::FfmsIndexedSource source{QStringLiteral(HIKARI_MEDIA_HELPER)};
};

float ramp(std::int64_t index)
{
    return float(index % 32768) / 32768.0f;
}

} // namespace

TEST_F(Audition, TheDeviceReceivesExactlyTheRange)
{
    SimulatedDevice device({48000, 2});
    const auto report = play(device, 1000, 4800);
    ASSERT_TRUE(report);
    EXPECT_TRUE(report->drained);
    EXPECT_EQ(report->outputFrames, 4800);
    ASSERT_GE(device.rendered.size(), 4800u * 2);
    for (std::size_t k = 0; k < 4800; ++k) {
        ASSERT_EQ(device.rendered[2 * k], ramp(1000 + std::int64_t(k))) << "frame " << k;
        ASSERT_EQ(device.rendered[2 * k + 1], -ramp(1000 + std::int64_t(k))) << "frame " << k;
    }
    for (std::size_t i = 4800 * 2; i < device.rendered.size(); ++i)
        ASSERT_EQ(device.rendered[i], 0.0f) << "silence after the range";
}

TEST_F(Audition, StopTailAndDrainAreSeparateMoments)
{
    SimulatedDevice device({48000, 2});
    const auto report = play(device, 1000, 4800);
    ASSERT_TRUE(report);
    ASSERT_TRUE(report->drained);
    EXPECT_LE(report->startedAt, report->logicalStopAt);
    EXPECT_LE(report->logicalStopAt, report->lastFrameConsumedAt) << "handed over before the device took it";
    EXPECT_GT(report->audibleEndAt, report->lastFrameConsumedAt);
    // The DAC end of the last buffer: the 50 ms latency plus its queued frames.
    EXPECT_GE(report->outputLatency, 0.05);
    EXPECT_LE(report->outputLatency, 0.05 + 256.0 / 48000 + 1e-9);
    std::fprintf(stderr, "tail after logical stop: %.1f ms, latency %.1f ms\n",
                 (report->audibleEndAt - report->logicalStopAt) * 1000, report->outputLatency * 1000);
}

TEST_F(Audition, ResampledOutputKeepsTheRangeDuration)
{
    SimulatedDevice device({44100, 2});
    const auto report = play(device, 1000, 4800);
    ASSERT_TRUE(report);
    EXPECT_TRUE(report->drained);
    EXPECT_EQ(report->outputFrames, 4410);
    std::size_t last = 0;
    for (std::size_t i = 0; i < device.rendered.size(); i += 2)
        if (device.rendered[i] != 0.0f)
            last = i / 2;
    EXPECT_LE(last, 4410u);
    EXPECT_GE(last, 4400u);
}

TEST_F(Audition, AStopBeforeTheEndIsNotADrain)
{
    SimulatedDevice device({48000, 2});
    const auto report = play(device, 0, 96000, [](LineAudition &audition) {
        QTimer::singleShot(80, &audition, [&audition] { audition.stop(); });
    });
    ASSERT_TRUE(report);
    EXPECT_FALSE(report->drained);
    EXPECT_GT(report->logicalStopAt, 0.0);
    EXPECT_EQ(report->lastFrameConsumedAt, 0.0);
}

TEST_F(Audition, ThroughPortAudio)
{
    // The fixture is a loud ramp, so this runs only on a named device (the CI
    // null PCM), never on a desktop's default speakers.
    const char *wanted = std::getenv("HIKARI_TEST_AUDIO_DEVICE");
    if (!wanted || !*wanted)
        GTEST_SKIP() << "set HIKARI_TEST_AUDIO_DEVICE to run against a real stream";
    backends::PortAudioOutput output;
    std::string id;
    for (const auto &d : output.devices())
        if (d.name.find(wanted) != std::string::npos)
            id = d.id;
    ASSERT_FALSE(id.empty()) << "the requested test device is missing";
    const auto format = output.open(id, {48000, 2});
    ASSERT_TRUE(format);
    const auto report = play(output, 0, 9600);
    ASSERT_TRUE(report);
    EXPECT_TRUE(report->drained);
    EXPECT_EQ(report->outputFrames, 9600 * format->sampleRate / 48000);
    std::fprintf(stderr, "PortAudio audition: %lld frames, tail %.1f ms, latency %.1f ms, underruns %llu\n",
                 static_cast<long long>(report->outputFrames), (report->audibleEndAt - report->logicalStopAt) * 1000,
                 report->outputLatency * 1000, static_cast<unsigned long long>(report->underruns));
}
