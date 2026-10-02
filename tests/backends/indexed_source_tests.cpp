// N1: exact indexed frames from the isolated FFMS2 media helper (M50-seek).
// Fixtures are generated at test time; each frame carries its index as a
// barcode, so a decoded frame proves its own identity.

#include "hikari/backends/ffms_indexed_source.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <cstdio>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>

using namespace hikari;
using namespace hikari::application;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

std::string fixture(const char *kind)
{
    return std::string(HIKARI_MEDIA_FIXTURES) + "/" + kind + ".mkv";
}

// Reads the 4x4 block barcode (white = 1) the fixture generator drew.
int barcode(const IndexedFrame &f)
{
    int value = 0;
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx) {
            const int x = (2 * bx + 1) * f.width / 8, y = (2 * by + 1) * f.height / 8;
            const std::byte *p = f.bgra.data() + static_cast<std::size_t>(y) * f.stride + x * 4;
            const int luma = (std::to_integer<int>(p[0]) + std::to_integer<int>(p[1]) + std::to_integer<int>(p[2])) / 3;
            if (luma > 128)
                value |= 1 << (by * 4 + bx);
        }
    return value;
}

struct Fixture : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "indexed_source_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    backends::FfmsIndexedSource source{QStringLiteral(HIKARI_MEDIA_HELPER)};

    std::expected<SourceTimeline, SourceError> open(const char *kind, std::vector<std::int64_t> *progress = nullptr)
    {
        std::optional<std::expected<SourceTimeline, SourceError>> result;
        source.open(fixture(kind), [&](std::int64_t done, std::int64_t) { if (progress) progress->push_back(done); },
                    [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    }
    std::expected<IndexedFrame, SourceError> frame(int index)
    {
        std::optional<std::expected<IndexedFrame, SourceError>> result;
        source.frame(index, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    }
    // Every frame, in a shuffled order (forward and backward seeks), decodes
    // to exactly the requested frame.
    void expectExactFrames(int count)
    {
        std::vector<int> order(static_cast<std::size_t>(count));
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), std::mt19937(42));
        for (int i : order) {
            const auto f = frame(i);
            ASSERT_TRUE(f) << "frame " << i;
            EXPECT_EQ(barcode(*f), i) << "requested frame " << i;
        }
    }
};

} // namespace

TEST_F(Fixture, ConstantRateFramesAreExact)
{
    std::vector<std::int64_t> progress;
    const auto t = open("cfr", &progress);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->pts.size(), 48u);
    EXPECT_EQ(t->fpsNumerator * 1001, t->fpsDenominator * 24000);
    EXPECT_TRUE(std::ranges::is_sorted(t->pts));
    expectExactFrames(48);
}

TEST_F(Fixture, VariableRateTimelineAndFramesAreExact)
{
    const auto t = open("vfr");
    ASSERT_TRUE(t);
    ASSERT_EQ(t->pts.size(), 48u);
    // Durations cycle 30, 50, 70 ms (the generator's pattern), in the time base.
    for (std::size_t i = 1; i < t->pts.size(); ++i) {
        const auto ms = (t->pts[i] - t->pts[i - 1]) * 1000 * t->timeBaseNumerator / t->timeBaseDenominator;
        EXPECT_EQ(ms, 30 + 20 * static_cast<std::int64_t>((i - 1) % 3)) << i;
    }
    expectExactFrames(48);
}

TEST_F(Fixture, BFrameReorderingKeepsFramesExact)
{
    const auto t = open("bframes");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->pts.size(), 48u);
    EXPECT_TRUE(std::ranges::is_sorted(t->pts));
    expectExactFrames(48);
}

TEST_F(Fixture, LongGopSeeksAreExact)
{
    const auto t = open("longgop");
    ASSERT_TRUE(t);
    ASSERT_EQ(t->pts.size(), 300u);
    // A sample across the single 300-frame GOP, including far backward seeks.
    for (int i : {299, 0, 150, 1, 298, 75, 200, 2})
        EXPECT_EQ(barcode(*frame(i)), i) << i;
}

TEST_F(Fixture, EndOfStreamAndNotOpen)
{
    EXPECT_EQ(frame(0).error(), SourceError::NotOpen);
    ASSERT_TRUE(open("cfr"));
    EXPECT_EQ(frame(48).error(), SourceError::EndOfStream);
    EXPECT_EQ(frame(-1).error(), SourceError::EndOfStream);
}

TEST_F(Fixture, ResultsFromAReplacedSourceAreStale)
{
    ASSERT_TRUE(open("cfr"));
    std::optional<std::expected<IndexedFrame, SourceError>> old;
    source.frame(10, [&](auto r) { old = std::move(r); });
    std::optional<std::expected<SourceTimeline, SourceError>> reopened;
    source.open(fixture("bframes"), {}, [&](auto r) { reopened = std::move(r); }); // before the frame arrives
    ASSERT_TRUE(waitFor([&] { return old.has_value() && reopened.has_value(); }));
    EXPECT_EQ(old->error(), SourceError::Stale);
    ASSERT_TRUE(*reopened);
    EXPECT_EQ((*reopened)->generation, source.generation());
}

TEST_F(Fixture, IndexingReportsProgressAndCanBeCancelled)
{
    std::vector<std::int64_t> progress;
    ASSERT_TRUE(open("longgop", &progress));
    EXPECT_FALSE(progress.empty()) << "indexing reports progress";
    EXPECT_TRUE(std::ranges::is_sorted(progress));

    // Cancel before the event loop runs: the Cancel follows the request.
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(fixture("longgop"), {}, [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return source.helperHost() && source.helperHost()->outstanding() > 0; }));
    source.cancelOpen();
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    ASSERT_FALSE(*result);
    EXPECT_EQ(result->error(), SourceError::Cancelled);
    EXPECT_EQ(frame(0).error(), SourceError::NotOpen) << "a cancelled open leaves nothing open";
    ASSERT_TRUE(open("cfr")) << "the helper still serves the next open";
}

TEST_F(Fixture, ASupersededOpenResolvesStale)
{
    std::optional<std::expected<SourceTimeline, SourceError>> first, second;
    source.open(fixture("longgop"), {}, [&](auto r) { first = std::move(r); });
    source.open(fixture("cfr"), {}, [&](auto r) { second = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return first.has_value() && second.has_value(); }));
    EXPECT_EQ(first->error(), SourceError::Stale);
    ASSERT_TRUE(*second);
    EXPECT_EQ((*second)->pts.size(), 48u);
}

TEST_F(Fixture, CancelledReadsResolveOnceAndLateResultsAreDropped)
{
    ASSERT_TRUE(open("cfr"));
    std::vector<std::expected<IndexedFrame, SourceError>> results;
    source.frame(3, [&](auto r) { results.push_back(std::move(r)); });
    source.frame(4, [&](auto r) { results.push_back(std::move(r)); });
    source.cancelReads();
    ASSERT_EQ(results.size(), 2u) << "resolved at once";
    EXPECT_EQ(results[0].error(), SourceError::Cancelled);
    EXPECT_EQ(results[1].error(), SourceError::Cancelled);
    const auto next = frame(5); // the helper's late answers arrive first
    ASSERT_TRUE(next);
    EXPECT_EQ(barcode(*next), 5);
    EXPECT_EQ(results.size(), 2u) << "late results are dropped";
}

TEST_F(Fixture, HelperLossIsReportedAndAReopenRecovers)
{
    ASSERT_TRUE(open("cfr"));
    std::vector<std::string> calls;
    std::optional<std::expected<IndexedFrame, SourceError>> pending;
    source.frame(5, [&](auto r) {
        calls.push_back(r ? "frame " + std::to_string(r->index) : "error " + std::to_string(static_cast<int>(r.error())));
        pending = std::move(r);
    });
    calls.push_back("stop");
    source.helperHost()->stop(); // the helper crashes
    std::string trace;
    for (const auto &c : calls)
        trace += c + "; ";
    ASSERT_TRUE(pending.has_value()) << trace;
    EXPECT_EQ(pending->error(), SourceError::HelperLost) << trace;
    ASSERT_TRUE(open("cfr")); // a fresh helper process
    EXPECT_EQ(barcode(*frame(5)), 5);
}

TEST_F(Fixture, HelperLossClosesTheGenerationUntilAnExplicitRestart)
{
    ASSERT_TRUE(open("cfr"));
    EXPECT_GT(source.lastHelperStartupMs(), 0.0);
    std::fprintf(stderr, "helper startup to handshake: %.1f ms\n", source.lastHelperStartupMs());
    const auto session = source.helperHost()->session();
    std::vector<quint64> lost;
    QObject::connect(&source, &backends::FfmsIndexedSource::helperLost, [&](quint64 g) { lost.push_back(g); });
    std::optional<std::expected<IndexedFrame, SourceError>> pending;
    source.frame(5, [&](auto r) { pending = std::move(r); });
    source.helperHost()->stop(); // the helper dies mid-request
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    EXPECT_EQ(pending->error(), SourceError::HelperLost);
    EXPECT_TRUE(source.isHelperLost());
    EXPECT_EQ(lost, std::vector<quint64>{source.generation()});
    EXPECT_EQ(frame(6).error(), SourceError::HelperLost) << "no silent restart";
    EXPECT_EQ(source.helperHost()->session(), session);

    std::optional<std::expected<SourceTimeline, SourceError>> restarted;
    source.restart({}, [&](auto r) { restarted = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return restarted.has_value(); }));
    ASSERT_TRUE(*restarted);
    EXPECT_FALSE(source.isHelperLost());
    EXPECT_GT(source.helperHost()->session(), session) << "a new helper generation";
    EXPECT_EQ(barcode(*frame(6)), 6);
}

TEST_F(Fixture, RestartBeforeAnyOpenIsRefused)
{
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    EXPECT_EQ(source.restart({}, [&](auto r) { result = std::move(r); }), 0u);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->error(), SourceError::NotOpen);
}

TEST_F(Fixture, FrameTransferObservation)
{
    ASSERT_TRUE(open("cfr"));
    QElapsedTimer timer;
    timer.start();
    std::size_t bytes = 0;
    for (int i = 0; i < 48; ++i) {
        const auto f = frame(i);
        ASSERT_TRUE(f);
        bytes += f->bgra.size();
    }
    const double ms = timer.nsecsElapsed() / 1e6;
    // An observation on an uncalibrated host, not a budget (M50-perf).
    std::fprintf(stderr, "frame transfer: 48 frames, %zu bytes, %.1f ms (%.2f ms per frame)\n", bytes, ms, ms / 48);
}

// I2: the helper converts Y'CbCr to BGRA with the stream's own matrix and
// range. Expected values follow the BT.601/BT.709 equations.
struct ColorCase {
    const char *kind;
    double kr, kb;
    bool full;
};

int colorError(Fixture &f, const ColorCase &c)
{
    static constexpr int kPatches[4][3] = {{180, 60, 200}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}};
    if (!f.open(c.kind))
        return 1000;
    const auto frame = f.frame(5);
    if (!frame)
        return 1000;
    int worst = 0;
    for (int q = 0; q < 4; ++q) {
        const double y = c.full ? kPatches[q][0] / 255.0 : (kPatches[q][0] - 16) / 219.0;
        const double pb = (kPatches[q][1] - 128) / (c.full ? 255.0 : 224.0);
        const double pr = (kPatches[q][2] - 128) / (c.full ? 255.0 : 224.0);
        const double r = y + 2 * (1 - c.kr) * pr, b = y + 2 * (1 - c.kb) * pb;
        const double g = (y - c.kr * r - c.kb * b) / (1 - c.kr - c.kb);
        auto code = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
        const int x = (q % 2 ? 3 : 1) * frame->width / 4, yy = (q / 2 ? 3 : 1) * frame->height / 4;
        const std::byte *p = frame->bgra.data() + std::size_t(yy) * frame->stride + std::size_t(x) * 4;
        const int got[3] = {std::to_integer<int>(p[2]), std::to_integer<int>(p[1]), std::to_integer<int>(p[0])};
        const int want[3] = {code(r), code(g), code(b)};
        for (int k = 0; k < 3; ++k)
            worst = std::max(worst, std::abs(got[k] - want[k]));
        std::fprintf(stderr, "%s patch %d: got %3d %3d %3d, expected %3d %3d %3d\n", c.kind, q, got[0], got[1], got[2],
                     want[0], want[1], want[2]);
    }
    return worst;
}

TEST_F(Fixture, Bt601LimitedRangeIsConvertedWithItsTags)
{
    EXPECT_LE(colorError(*this, {"color601", 0.299, 0.114, false}), 3);
}

TEST_F(Fixture, Bt709LimitedRangeIsConvertedWithItsTags)
{
    EXPECT_LE(colorError(*this, {"color709", 0.2126, 0.0722, false}), 3);
}

TEST_F(Fixture, Bt709FullRangeIsConvertedWithItsTags)
{
    EXPECT_LE(colorError(*this, {"color709full", 0.2126, 0.0722, true}), 3);
}

TEST_F(Fixture, ColorControlsDetectAWrongMatrixOrRange)
{
    // Controls: the same comparisons fail by a wide margin when the
    // expectation uses the other matrix or range.
    EXPECT_GT(colorError(*this, {"color709", 0.299, 0.114, false}), 20) << "BT.709 read as BT.601";
    // Saturated patches clip, so a range error shows less than a matrix error;
    // both stay well beyond the comparisons' tolerance of 3.
    EXPECT_GT(colorError(*this, {"color709full", 0.2126, 0.0722, false}), 10) << "full range read as limited";
}

TEST_F(Fixture, UnreadableFilesFailExplicitly)
{
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(std::string(HIKARI_MEDIA_FIXTURES) + "/missing.mkv", {}, [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_EQ(result->error(), SourceError::InvalidInput);
}

// N2: source PCM ranges. The audio fixture's left channel is the sample index
// modulo 32768 and the right channel its negation; the generator writes 94
// blocks of 1024 sample frames (96256 in total).

namespace {

std::int16_t left(const AudioBlock &b, std::int64_t i)
{
    std::int16_t v;
    std::memcpy(&v, b.samples.data() + static_cast<std::size_t>(i) * 4, 2);
    return v;
}

std::int16_t right(const AudioBlock &b, std::int64_t i)
{
    std::int16_t v;
    std::memcpy(&v, b.samples.data() + static_cast<std::size_t>(i) * 4 + 2, 2);
    return v;
}

} // namespace

struct AudioFixture : Fixture {
    std::expected<AudioInfo, SourceError> openAudio(int track)
    {
        std::optional<std::expected<AudioInfo, SourceError>> result;
        source.openAudio(track, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    }
    std::expected<AudioBlock, SourceError> audio(std::int64_t start, std::int64_t count)
    {
        std::optional<std::expected<AudioBlock, SourceError>> result;
        source.audio(start, count, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    }
};

TEST_F(AudioFixture, PcmRangesAreExact)
{
    const auto t = open("audio");
    ASSERT_TRUE(t);
    ASSERT_GE(t->firstAudioTrack, 0);
    const auto info = openAudio(t->firstAudioTrack);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->sampleRate, 48000);
    EXPECT_EQ(info->channels, 2);
    EXPECT_EQ(info->format, SampleFormat::S16);
    EXPECT_EQ(info->bitsPerSample, 16);
    EXPECT_EQ(info->sampleCount, 96256);
    EXPECT_EQ(info->originMicroseconds, 0);
    for (std::int64_t start : {0, 1000, 32760, 50000}) {
        const auto b = audio(start, 32);
        ASSERT_TRUE(b) << start;
        ASSERT_EQ(b->count, 32);
        for (std::int64_t i = 0; i < 32; ++i) {
            EXPECT_EQ(left(*b, i), static_cast<std::int16_t>((start + i) % 32768)) << start + i;
            EXPECT_EQ(right(*b, i), static_cast<std::int16_t>(-((start + i) % 32768))) << start + i;
        }
    }
}

TEST_F(AudioFixture, RangesEndAtTheSource)
{
    ASSERT_TRUE(open("audio"));
    EXPECT_EQ(audio(0, 4).error(), SourceError::NotOpen); // audio not opened yet
    ASSERT_TRUE(openAudio(open("audio")->firstAudioTrack));
    const auto tail = audio(96250, 100);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail->count, 6); // shortened at the end
    EXPECT_EQ(left(*tail, 5), static_cast<std::int16_t>(96255 % 32768));
    EXPECT_EQ(audio(96256, 10).error(), SourceError::EndOfStream);
    const auto empty = audio(10, 0);
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->count, 0);
}

TEST_F(AudioFixture, CancelledAndSupersededRangesNeverDeliverSamples)
{
    const auto t = open("audio");
    ASSERT_TRUE(t);
    ASSERT_TRUE(openAudio(t->firstAudioTrack));
    std::optional<std::expected<AudioBlock, SourceError>> cancelled;
    source.audio(0, 4800, [&](auto r) { cancelled = std::move(r); });
    source.cancelReads();
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->error(), SourceError::Cancelled);

    std::optional<std::expected<AudioBlock, SourceError>> stale;
    source.audio(0, 4800, [&](auto r) { stale = std::move(r); });
    std::optional<std::expected<SourceTimeline, SourceError>> reopened;
    source.open(fixture("audio"), {}, [&](auto r) { reopened = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return stale.has_value() && reopened.has_value(); }));
    EXPECT_EQ(stale->error(), SourceError::Stale);
    EXPECT_EQ(audio(0, 4).error(), SourceError::NotOpen) << "the new source has no audio opened yet";
}

struct PcmFixture : AudioFixture {
    std::optional<PcmStream> begin(std::int64_t start, std::int64_t count, int rate, int channels)
    {
        std::optional<std::expected<PcmStream, SourceError>> result;
        source.beginPcm(start, count, rate, channels, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        if (!result || !*result)
            return std::nullopt;
        return **result;
    }
    // The whole stream in chunks of `chunk` frames.
    std::vector<float> readAll(std::int64_t chunk, int *chunks = nullptr)
    {
        std::vector<float> all;
        for (int n = 0; n < 10'000; ++n) {
            std::optional<std::expected<PcmChunk, SourceError>> result;
            source.nextPcm(chunk, [&](auto r) { result = std::move(r); });
            EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
            if (!result || !*result)
                break;
            all.insert(all.end(), (*result)->samples.begin(), (*result)->samples.end());
            if (chunks)
                ++*chunks;
            if ((*result)->end)
                break;
        }
        return all;
    }
    void SetUp() override
    {
        const auto t = open("audio");
        ASSERT_TRUE(t);
        ASSERT_TRUE(openAudio(t->firstAudioTrack));
    }
};

TEST_F(PcmFixture, WithoutResamplingTheRangeIsSampleExact)
{
    const auto stream = begin(1000, 4800, 48000, 2);
    ASSERT_TRUE(stream);
    EXPECT_EQ(stream->totalFrames, 4800);
    int chunks = 0;
    const auto pcm = readAll(1000, &chunks);
    ASSERT_EQ(pcm.size(), 4800u * 2);
    EXPECT_EQ(chunks, 5);
    for (std::size_t k = 0; k < 4800; ++k) {
        ASSERT_EQ(pcm[2 * k], float((1000 + k) % 32768) / 32768.0f) << k;
        ASSERT_EQ(pcm[2 * k + 1], -float((1000 + k) % 32768) / 32768.0f) << k;
    }
}

TEST_F(PcmFixture, ResamplingKeepsTheExactLengthAndTheTimeline)
{
    const auto stream = begin(1000, 4800, 44100, 2);
    ASSERT_TRUE(stream);
    EXPECT_EQ(stream->totalFrames, 4410) << "0.1 s at 44.1 kHz";
    const auto pcm = readAll(4410);
    ASSERT_EQ(pcm.size(), 4410u * 2);
    // Away from the edges (the resampler's filter has no history there), the
    // output samples the ramp at source position start + k * 48000 / 44100.
    double worst = 0;
    for (std::size_t k = 64; k < 4410 - 64; ++k) {
        const double expected = (1000 + k * 48000.0 / 44100.0) / 32768.0;
        worst = std::max(worst, std::abs(pcm[2 * k] - expected));
    }
    std::fprintf(stderr, "resampled ramp: worst error %.3f source steps\n", worst * 32768);
    EXPECT_LT(worst * 32768, 1.0) << "within one source step of the timeline";
}

TEST_F(PcmFixture, ChunksJoinWithoutSeams)
{
    ASSERT_TRUE(begin(1000, 4800, 44100, 2));
    const auto whole = readAll(4410);
    ASSERT_TRUE(begin(1000, 4800, 44100, 2));
    const auto pieces = readAll(333);
    ASSERT_EQ(whole.size(), pieces.size());
    for (std::size_t i = 0; i < whole.size(); ++i)
        ASSERT_EQ(whole[i], pieces[i]) << i;
}

TEST_F(PcmFixture, ChannelsAreMappedByLayout)
{
    ASSERT_TRUE(begin(1000, 4800, 48000, 1));
    const auto mono = readAll(4800);
    ASSERT_EQ(mono.size(), 4800u);
    // The fixture's right channel is the negated left: a downmix cancels.
    for (float v : mono)
        ASSERT_LT(std::abs(v), 1e-6f);
}

TEST_F(PcmFixture, RangesEndAtTheSource)
{
    const auto stream = begin(96200, 1000, 48000, 2);
    ASSERT_TRUE(stream);
    EXPECT_EQ(stream->count, 56);
    EXPECT_EQ(stream->totalFrames, 56);
    EXPECT_EQ(readAll(1000).size(), 56u * 2);
    std::optional<std::expected<PcmStream, SourceError>> past;
    source.beginPcm(96256, 10, 48000, 2, [&](auto r) { past = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return past.has_value(); }));
    EXPECT_EQ(past->error(), SourceError::EndOfStream);
}

TEST_F(AudioFixture, HelperLossDuringAudioRequestsIsExplicit)
{
    const auto t = open("audio");
    ASSERT_TRUE(t);
    ASSERT_TRUE(openAudio(t->firstAudioTrack));
    std::optional<std::expected<AudioBlock, SourceError>> pending;
    source.audio(0, 48000, [&](auto r) { pending = std::move(r); });
    source.helperHost()->stop();
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    EXPECT_EQ(pending->error(), SourceError::HelperLost);
    EXPECT_EQ(audio(0, 10).error(), SourceError::HelperLost);
}

TEST_F(AudioFixture, SourcesWithoutAudioReportIt)
{
    const auto t = open("cfr");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->firstAudioTrack, -1);
    EXPECT_EQ(openAudio(0).error(), SourceError::Unsupported); // track 0 is video
}
