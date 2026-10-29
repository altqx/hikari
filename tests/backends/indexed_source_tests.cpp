#include "hikari/backends/ffms_indexed_source.h"
#include "open_files.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cstdio>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
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

}

TEST_F(Fixture, ConstantRateFramesAreExact)
{
    std::vector<std::int64_t> progress;
    const auto t = open("cfr", &progress);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->pts.size(), 48u);
    ASSERT_FALSE(t->keyframes.empty());
    EXPECT_EQ(t->keyframes.front(), 0);
    EXPECT_TRUE(std::is_sorted(t->keyframes.begin(), t->keyframes.end()));
    EXPECT_LT(t->keyframes.back(), 48);
    EXPECT_EQ(t->fpsNumerator * 1001, t->fpsDenominator * 24000);
    EXPECT_TRUE(std::ranges::is_sorted(t->pts));
    EXPECT_EQ(t->width, 320);
    EXPECT_EQ(t->height, 240);
    EXPECT_TRUE(t->sarNum == 0 || t->sarNum == t->sarDen) << t->sarNum << ":" << t->sarDen;
    expectExactFrames(48);
}

TEST_F(Fixture, OpenGivesTheFramesSizeAndSampleAspect)
{
    const auto t = open("sar");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->width, 320);
    EXPECT_EQ(t->height, 240);
    EXPECT_EQ(t->sarNum, 32);
    EXPECT_EQ(t->sarDen, 27);
}

TEST_F(Fixture, VariableRateTimelineAndFramesAreExact)
{
    const auto t = open("vfr");
    ASSERT_TRUE(t);
    ASSERT_EQ(t->pts.size(), 48u);
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
    EXPECT_EQ(t->keyframes, std::vector<int>{0});
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
    source.open(fixture("bframes"), {}, [&](auto r) { reopened = std::move(r); });
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

TEST_F(Fixture, IndexingProgressComesOnceAThousandth)
{
    std::vector<std::pair<std::int64_t, std::int64_t>> reports;
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(fixture("long"), [&](std::int64_t done, std::int64_t total) { reports.emplace_back(done, total); },
                [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }, 60000));
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->pts.size(), 24000u);
    EXPECT_GT(reports.size(), 10u);
    EXPECT_LE(reports.size(), 1001u);
    for (std::size_t i = 1; i < reports.size(); ++i)
        EXPECT_NE(progressPermille(reports[i - 1].first, reports[i - 1].second),
                  progressPermille(reports[i].first, reports[i].second))
            << "report " << i;
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
    const auto next = frame(5);
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
    source.helperHost()->stop();
    std::string trace;
    for (const auto &c : calls)
        trace += c + "; ";
    ASSERT_TRUE(pending.has_value()) << trace;
    EXPECT_EQ(pending->error(), SourceError::HelperLost) << trace;
    ASSERT_TRUE(open("cfr"));
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
    source.helperHost()->stop();
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

TEST_F(Fixture, CloseLetsGoOfTheFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString clip = folder.filePath(QStringLiteral("clip.mkv"));
    ASSERT_TRUE(QFile::copy(QString::fromStdString(fixture("cfr")), clip));
    const auto openClip = [&] {
        std::optional<std::expected<SourceTimeline, SourceError>> result;
        source.open(clip.toStdString(), {}, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    };
    ASSERT_TRUE(openClip());
    EXPECT_EQ(barcode(*frame(3)), 3);
    EXPECT_FALSE(openfiles::nothingHolds(clip));
    std::optional<std::expected<IndexedFrame, SourceError>> pending;
    source.frame(7, [&](auto r) { pending = std::move(r); });
    source.close();
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->error(), SourceError::Cancelled);
    EXPECT_TRUE(waitFor([&] { return openfiles::nothingHolds(clip); }, 5000)) << openfiles::under(folder.path()).join(u'\n').toStdString();
    EXPECT_EQ(frame(0).error(), SourceError::NotOpen);
    std::optional<std::expected<SourceTimeline, SourceError>> restarted;
    EXPECT_EQ(source.restart({}, [&](auto r) { restarted = std::move(r); }), 0u);
    ASSERT_TRUE(restarted);
    EXPECT_EQ(restarted->error(), SourceError::NotOpen);
    ASSERT_TRUE(openClip());
    EXPECT_EQ(barcode(*frame(5)), 5);
}

TEST_F(Fixture, CloseDuringAnOpenLetsGoOfTheFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString clip = folder.filePath(QStringLiteral("clip.mkv"));
    ASSERT_TRUE(QFile::copy(QString::fromStdString(fixture("longgop")), clip));
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(clip.toStdString(), {}, [&](auto r) { result = std::move(r); });
    source.close();
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_FALSE(*result);
    EXPECT_TRUE(waitFor([&] { return openfiles::nothingHolds(clip); }, 5000)) << openfiles::under(folder.path()).join(u'\n').toStdString();
    EXPECT_EQ(frame(0).error(), SourceError::NotOpen);
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
    std::fprintf(stderr, "frame transfer: 48 frames, %zu bytes, %.1f ms (%.2f ms per frame)\n", bytes, ms, ms / 48);
}

struct ColorCase {
    const char *kind;
    double kr, kb;
    bool full;
};

int frameColorError(const IndexedFrame &frame, const ColorCase &c)
{
    static constexpr int kPatches[4][3] = {{180, 60, 200}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}};
    int worst = 0;
    for (int q = 0; q < 4; ++q) {
        const double y = c.full ? kPatches[q][0] / 255.0 : (kPatches[q][0] - 16) / 219.0;
        const double pb = (kPatches[q][1] - 128) / (c.full ? 255.0 : 224.0);
        const double pr = (kPatches[q][2] - 128) / (c.full ? 255.0 : 224.0);
        const double r = y + 2 * (1 - c.kr) * pr, b = y + 2 * (1 - c.kb) * pb;
        const double g = (y - c.kr * r - c.kb * b) / (1 - c.kr - c.kb);
        auto code = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
        const int x = (q % 2 ? 3 : 1) * frame.width / 4, yy = (q / 2 ? 3 : 1) * frame.height / 4;
        const std::byte *p = frame.bgra.data() + std::size_t(yy) * frame.stride + std::size_t(x) * 4;
        const int got[3] = {std::to_integer<int>(p[2]), std::to_integer<int>(p[1]), std::to_integer<int>(p[0])};
        const int want[3] = {code(r), code(g), code(b)};
        for (int k = 0; k < 3; ++k)
            worst = std::max(worst, std::abs(got[k] - want[k]));
        std::fprintf(stderr, "%s patch %d: got %3d %3d %3d, expected %3d %3d %3d\n", c.kind, q, got[0], got[1], got[2],
                     want[0], want[1], want[2]);
    }
    return worst;
}

int colorError(Fixture &f, const ColorCase &c)
{
    if (!f.open(c.kind))
        return 1000;
    const auto frame = f.frame(5);
    if (!frame)
        return 1000;
    return frameColorError(*frame, c);
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
    EXPECT_GT(colorError(*this, {"color709", 0.299, 0.114, false}), 20) << "BT.709 read as BT.601";
    EXPECT_GT(colorError(*this, {"color709full", 0.2126, 0.0722, false}), 10) << "full range read as limited";
}

TEST_F(Fixture, OpenReportsTheFramesMatrixAndRange)
{
    auto t = open("color709");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->colorSpace, 1); // FFMS_CS_BT709
    EXPECT_EQ(t->colorRange, 1); // FFMS_CR_MPEG
    t = open("color709full");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->colorRange, 2); // FFMS_CR_JPEG
    t = open("color601");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->colorSpace, 6); // FFMS_CS_SMPTE170M
    t = open("colorhd");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->colorSpace, 2); // FFMS_CS_UNSPECIFIED
    EXPECT_EQ(t->width, 1280);
}

TEST_F(Fixture, InputMatrixConvertsTheFollowingFrames)
{
    ASSERT_TRUE(open("color709"));
    auto before = frame(5);
    ASSERT_TRUE(before);
    EXPECT_LE(frameColorError(*before, {"color709", 0.2126, 0.0722, false}), 3);
    std::optional<std::expected<void, SourceError>> set;
    source.setInputMatrix(5, 1, [&](auto r) { set = r; });
    ASSERT_TRUE(waitFor([&] { return set.has_value(); }));
    ASSERT_TRUE(set->has_value());
    const auto as601 = frame(5);
    ASSERT_TRUE(as601);
    EXPECT_LE(frameColorError(*as601, {"color709 as BT.601", 0.299, 0.114, false}), 3);
    EXPECT_GT(frameColorError(*as601, {"color709 as BT.601, BT.709 control", 0.2126, 0.0722, false}), 20);
    set.reset();
    source.setInputMatrix(1, 1, [&](auto r) { set = r; });
    ASSERT_TRUE(waitFor([&] { return set.has_value(); }));
    const auto again = frame(5);
    ASSERT_TRUE(again);
    EXPECT_LE(frameColorError(*again, {"color709 again", 0.2126, 0.0722, false}), 3);
}

TEST_F(Fixture, UntaggedHdTakesTheGuessedMatrixOnlyWhenSet)
{
    ASSERT_TRUE(open("colorhd"));
    const auto plain = frame(5);
    ASSERT_TRUE(plain);
    EXPECT_LE(frameColorError(*plain, {"colorhd default", 0.299, 0.114, false}), 3);
    std::optional<std::expected<void, SourceError>> set;
    source.setInputMatrix(1, 0, [&](auto r) { set = r; });
    ASSERT_TRUE(waitFor([&] { return set.has_value(); }));
    ASSERT_TRUE(set->has_value());
    const auto as709 = frame(5);
    ASSERT_TRUE(as709);
    EXPECT_LE(frameColorError(*as709, {"colorhd as BT.709", 0.2126, 0.0722, false}), 3);
}

TEST_F(Fixture, InputMatrixNeedsAnOpenVideo)
{
    std::optional<std::expected<void, SourceError>> set;
    source.setInputMatrix(5, 1, [&](auto r) { set = r; });
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->error(), SourceError::NotOpen);
}

TEST_F(Fixture, UnreadableFilesFailExplicitly)
{
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(std::string(HIKARI_MEDIA_FIXTURES) + "/missing.mkv", {}, [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_EQ(result->error(), SourceError::InvalidInput);
    const auto failure = source.openFailure();
    ASSERT_TRUE(failure);
    EXPECT_EQ(failure->stage, OpenStage::Indexer);
    EXPECT_FALSE(failure->message.empty());
    ASSERT_TRUE(open("cfr"));
    EXPECT_FALSE(source.openFailure()) << "a later open that succeeds has no failure";
}

struct FailingHelperFixture : Fixture {
    backends::FfmsIndexedSource failing{QStringLiteral(HIKARI_FAILING_MEDIA_HELPER)};
};

TEST_F(FailingHelperFixture, EachStageAndItsTextReachThePort)
{
    const struct {
        const char *file;
        OpenStage stage;
        SourceError error;
        const char *text;
    } cases[] = {{"/clips/indexer.mkv", OpenStage::Indexer, SourceError::InvalidInput, "fake indexer error"},
                 {"/clips/indexing.mkv", OpenStage::Indexing, SourceError::BackendFailure, "fake indexing error"},
                 {"/clips/source.mkv", OpenStage::Source, SourceError::BackendFailure, "fake source error"},
                 {"/clips/convert.mkv", OpenStage::Convert, SourceError::BackendFailure, "fake convert error"},
                 {"/clips/other.mkv", OpenStage::Host, SourceError::BackendFailure, "no such stage"}};
    for (const auto &c : cases) {
        std::optional<std::expected<SourceTimeline, SourceError>> result;
        std::optional<OpenFailure> seen;
        failing.open(c.file, {}, [&](auto r) {
            seen = failing.openFailure();
            result = std::move(r);
        });
        ASSERT_TRUE(waitFor([&] { return result.has_value(); })) << c.file;
        ASSERT_FALSE(*result) << c.file;
        EXPECT_EQ(result->error(), c.error) << c.file;
        ASSERT_TRUE(seen) << c.file;
        EXPECT_EQ(seen->stage, c.stage) << c.file;
        EXPECT_EQ(seen->message, c.text) << c.file;
    }
}

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

}

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
    EXPECT_EQ(audio(0, 4).error(), SourceError::NotOpen);
    ASSERT_TRUE(openAudio(open("audio")->firstAudioTrack));
    const auto tail = audio(96250, 100);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail->count, 6);
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
    EXPECT_EQ(openAudio(0).error(), SourceError::Unsupported);
}

struct DisplayAudioFixture : AudioFixture {
    std::expected<MediaProbe, AudioFailure> probe(const std::string &path)
    {
        std::optional<std::expected<MediaProbe, AudioFailure>> result;
        source.probe(path, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(AudioFailure{}));
    }
    std::expected<DisplayAudioOpened, AudioFailure> openDisplayPath(backends::FfmsIndexedSource &from,
                                                                    const std::string &path, int track,
                                                                    const std::string &indexFile,
                                                                    std::vector<std::int64_t> *progress = nullptr)
    {
        std::optional<std::expected<DisplayAudioOpened, AudioFailure>> result;
        from.openDisplayAudio(path, track, indexFile,
                              [&](std::int64_t done, std::int64_t) { if (progress) progress->push_back(done); },
                              [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(AudioFailure{}));
    }
    std::expected<AudioInfo, AudioFailure> openDisplay(const char *kind, int track = 1,
                                                       std::vector<std::int64_t> *progress = nullptr)
    {
        const auto opened = openDisplayPath(source, fixture(kind), track, {}, progress);
        if (!opened)
            return std::unexpected(opened.error());
        EXPECT_TRUE(opened->newIndex);
        return opened->info;
    }
    std::expected<SourceTimeline, SourceError> openIndexed(const std::string &path, int track,
                                                           const std::string &indexFile,
                                                           std::vector<std::int64_t> *progress = nullptr)
    {
        std::optional<std::expected<SourceTimeline, SourceError>> result;
        source.openIndexed(path, IndexRequest{track, indexFile},
                           [&](std::int64_t done, std::int64_t) { if (progress) progress->push_back(done); },
                           [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(SourceError::BackendFailure));
    }
    std::expected<AudioBlock, AudioFailure> display(std::int64_t start, std::int64_t count)
    {
        return displayBlock({start, count, count, false});
    }
    std::expected<AudioBlock, AudioFailure> displayBlock(const BlockRead &read)
    {
        std::optional<std::expected<AudioBlock, AudioFailure>> result;
        source.displayAudio(read, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(AudioFailure{}));
    }
};

TEST_F(DisplayAudioFixture, AudioWithoutVideoOpensInTheLegacyFormat)
{
    const auto tracks = probe(fixture("audioonly"));
    ASSERT_TRUE(tracks);
    EXPECT_FALSE(tracks->hasVideo);
    ASSERT_EQ(tracks->audio.size(), 1u);
    std::vector<std::int64_t> progress;
    const auto info = openDisplay("audioonly", tracks->audio[0].index, &progress);
    ASSERT_TRUE(info) << info.error().message;
    EXPECT_EQ(info->format, SampleFormat::S16);
    EXPECT_EQ(info->bitsPerSample, 16);
    EXPECT_EQ(info->channels, 2);
    EXPECT_EQ(info->channelLayout, 3); // front left and right
    EXPECT_EQ(info->sampleRate, 48000);
    EXPECT_EQ(info->sampleCount, 96256);
    EXPECT_EQ(info->originMicroseconds, 0);
    for (std::int64_t start : {0, 1771, 50000, 96200}) {
        const auto b = display(start, 56);
        ASSERT_TRUE(b) << start;
        ASSERT_EQ(b->count, 56);
        for (std::int64_t i = 0; i < 56; ++i) {
            const auto v = static_cast<std::int16_t>((((start + i) * 37) % 65536) - 32768);
            ASSERT_EQ(left(*b, i), v) << start + i;
            ASSERT_EQ(right(*b, i), static_cast<std::int16_t>(v / 2)) << start + i;
        }
    }
    EXPECT_EQ(frame(0).error(), SourceError::NotOpen);
    EXPECT_EQ(audio(0, 4).error(), SourceError::NotOpen);
    EXPECT_EQ(display(96256, 4).error().error, SourceError::EndOfStream);
}

TEST_F(DisplayAudioFixture, SampleZeroIsTheFirstVideoFrame)
{
    const auto info = openDisplay("audiodelay");
    ASSERT_TRUE(info);
    EXPECT_EQ(info->sampleCount, 96256 + 24000);
    const auto before = display(23990, 10);
    ASSERT_TRUE(before);
    for (std::int64_t i = 0; i < 10; ++i)
        EXPECT_EQ(left(*before, i), 0);
    const auto b = display(24000, 40);
    ASSERT_TRUE(b);
    for (std::int64_t i = 0; i < 40; ++i) {
        EXPECT_EQ(left(*b, i), static_cast<std::int16_t>(i % 32768));
        EXPECT_EQ(right(*b, i), static_cast<std::int16_t>(-(i % 32768)));
    }
}

TEST_F(DisplayAudioFixture, ProbeListsTheAudioTracks)
{
    const auto tracks = probe(fixture("tracks"));
    ASSERT_TRUE(tracks);
    EXPECT_TRUE(tracks->hasVideo);
    ASSERT_EQ(tracks->audio.size(), 2u);
    EXPECT_EQ(tracks->audio[0].index, 1);
    EXPECT_TRUE(tracks->audio[0].hasName);
    EXPECT_EQ(tracks->audio[0].name, "Main");
    EXPECT_EQ(tracks->audio[0].language, "eng");
    EXPECT_EQ(tracks->audio[0].codec, "pcm_s16le");
    EXPECT_EQ(tracks->audio[1].index, 2);
    EXPECT_EQ(tracks->audio[1].name, "Commentary");
    EXPECT_EQ(tracks->audio[1].language, "jpn");
    const auto second = openDisplay("tracks", 2);
    ASSERT_TRUE(second);
    const auto b = display(1000, 8);
    ASSERT_TRUE(b);
    EXPECT_EQ(left(*b, 0), 0);
    EXPECT_TRUE(openDisplay("tracks", 1));
    EXPECT_EQ(left(*display(1000, 8), 0), 1000);
}

// The helper stops first: Windows does not remove a file a process has open.
struct IndexFolder {
    std::filesystem::path dir;
    backends::FfmsIndexedSource &holder;
    IndexFolder(const char *name, backends::FfmsIndexedSource &source)
        : dir(std::filesystem::temp_directory_path() / ("hikari-a1-index-" + std::string(name))), holder(source)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~IndexFolder()
    {
        if (auto *host = holder.helperHost())
            host->stop();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (ec)
            ADD_FAILURE() << "cannot remove " << dir << ": " << ec.message();
    }
    std::string copy(const char *kind)
    {
        const auto to = dir / (std::string(kind) + ".mkv");
        std::filesystem::copy_file(fixture(kind), to);
        return to.string();
    }
    std::string index(const char *name) const { return (dir / "Indices" / name).string(); }
};

TEST_F(DisplayAudioFixture, TheVideosIndexFileIsWrittenAndReadBack)
{
    IndexFolder folder("video", source);
    const auto path = folder.copy("tracks");
    const auto file = folder.index("tracks_2.ffindex");
    std::vector<std::int64_t> progress;
    const auto first = openIndexed(path, 2, file, &progress);
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->newIndex);
    EXPECT_FALSE(progress.empty());
    EXPECT_TRUE(std::filesystem::exists(file));
    EXPECT_TRUE(first->handoffIndexFile.empty());
    EXPECT_EQ(first->firstAudioTrack, 1);
    EXPECT_EQ(first->audioTracks, (std::vector<int>{1, 2}));
    ASSERT_TRUE(openAudio(2));
    progress.clear();
    const auto again = openIndexed(path, 2, file, &progress);
    ASSERT_TRUE(again);
    EXPECT_FALSE(again->newIndex);
    EXPECT_TRUE(progress.empty());
    EXPECT_EQ(again->pts, first->pts);
    ASSERT_TRUE(frame(3));
    EXPECT_EQ(barcode(*frame(3)), 3);
}

TEST_F(DisplayAudioFixture, TheBoxReadsTheVideosIndexInItsOwnHelper)
{
    IndexFolder folder("box", source);
    const auto path = folder.copy("tracks");
    const auto file = folder.index("tracks_1.ffindex");
    ASSERT_TRUE(openIndexed(path, 1, file));
    backends::FfmsIndexedSource box{QStringLiteral(HIKARI_MEDIA_HELPER)};
    std::vector<std::int64_t> progress;
    const auto opened = openDisplayPath(box, path, 1, file, &progress);
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_FALSE(opened->newIndex);
    EXPECT_TRUE(progress.empty());
    EXPECT_EQ(opened->info.sampleCount, 96256);
    std::optional<std::expected<AudioBlock, AudioFailure>> block;
    box.displayAudio({1000, 4, 4, false}, [&](auto r) { block = std::move(r); });
    std::optional<std::expected<IndexedFrame, SourceError>> shown;
    source.frame(5, [&](auto r) { shown = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return block.has_value() && shown.has_value(); }));
    ASSERT_TRUE(*block);
    EXPECT_EQ(left(**block, 0), 1000);
    ASSERT_TRUE(*shown);
    EXPECT_EQ(barcode(**shown), 5);
    EXPECT_NE(box.helperHost(), source.helperHost());
    const auto other = openDisplayPath(box, path, 2, folder.index("tracks_2.ffindex"));
    ASSERT_TRUE(other);
    EXPECT_TRUE(other->newIndex);
    EXPECT_TRUE(std::filesystem::exists(folder.index("tracks_2.ffindex")));
}

TEST_F(DisplayAudioFixture, AStaleOrForeignIndexFileIsIndexedAgain)
{
    IndexFolder folder("stale", source);
    const auto path = folder.copy("tracks");
    const auto file = folder.index("tracks_1.ffindex");
    ASSERT_TRUE(openIndexed(path, 1, file));
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
    const auto stale = openIndexed(path, 1, file);
    ASSERT_TRUE(stale);
    EXPECT_TRUE(stale->newIndex);
    const auto otherPath = folder.copy("audio");
    ASSERT_TRUE(openIndexed(otherPath, 1, file));
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(path) + std::chrono::seconds(5));
    const auto foreign = openIndexed(path, 1, file);
    ASSERT_TRUE(foreign);
    EXPECT_TRUE(foreign->newIndex);
    EXPECT_EQ(foreign->audioTracks, (std::vector<int>{1, 2}));
    const auto plain = open("tracks");
    ASSERT_TRUE(plain);
    EXPECT_TRUE(plain->newIndex);
    EXPECT_TRUE(openAudio(1));
    EXPECT_TRUE(openAudio(2));
}

TEST_F(DisplayAudioFixture, BlocksAreReadIntoTheBlockBufferAsLegacyReadThem)
{
    const auto tracks = probe(fixture("audioonly"));
    ASSERT_TRUE(tracks);
    ASSERT_EQ(tracks->audio.size(), 1u);
    const int track = tracks->audio[0].index;
    ASSERT_TRUE(openDisplay("audioonly", track));
    const auto value = [](std::int64_t i) { return static_cast<std::int16_t>(((i * 37) % 65536) - 32768); };
    const auto first = display(0, 56);
    ASSERT_TRUE(first);
    const auto second = displayBlock({1000, 56, 10, false});
    ASSERT_TRUE(second) << second.error().message;
    ASSERT_EQ(second->count, 56);
    for (std::int64_t i = 0; i < 10; ++i)
        EXPECT_EQ(left(*second, i), value(1000 + i)) << i;
    for (std::int64_t i = 10; i < 28; ++i)
        EXPECT_EQ(left(*second, i), 0) << i;
    for (std::int64_t i = 28; i < 56; ++i) {
        EXPECT_EQ(left(*second, i), value(i)) << i;
        EXPECT_EQ(right(*second, i), static_cast<std::int16_t>(value(i) / 2)) << i;
    }
    const auto fresh = displayBlock({1000, 56, 10, true});
    ASSERT_TRUE(fresh);
    EXPECT_EQ(left(*fresh, 0), value(1000));
    for (std::int64_t i = 28; i < 56; ++i)
        EXPECT_EQ(left(*fresh, i), 0) << i;
    const auto nothing = displayBlock({0, 56, 4, false});
    ASSERT_TRUE(nothing);
    EXPECT_EQ(left(*nothing, 3), value(3));
    EXPECT_EQ(left(*nothing, 4), 0);
    EXPECT_EQ(right(*nothing, 27), 0);
    EXPECT_EQ(left(*nothing, 40), value(40));
    const auto failed = displayBlock({96250, 56, 56, false});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().stage, AudioStage::Read);
    EXPECT_FALSE(failed.error().message.empty());
    EXPECT_EQ(failed.error().samples, nothing->samples);
    ASSERT_TRUE(openDisplay("audioonly", track));
    const auto reopened = displayBlock({0, 56, 0, false});
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(std::all_of(reopened->samples.begin(), reopened->samples.end(), [](std::byte b) { return b == std::byte{0}; }));
}

TEST_F(DisplayAudioFixture, TheIndexIsHandedOverWhenItsFileCannotBeWritten)
{
    IndexFolder folder("handoff", source);
    const auto path = folder.copy("tracks");
    const auto indices = folder.dir / "Indices";
    std::filesystem::create_directories(indices);
    const auto file = folder.index("tracks_1.ffindex");
#ifdef _WIN32
    // A read-only folder still takes new files on Windows, so a folder takes the index file's name.
    std::filesystem::create_directories(file);
#else
    std::filesystem::permissions(indices, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    struct Restore {
        std::filesystem::path dir;
        ~Restore() { std::filesystem::permissions(dir, std::filesystem::perms::owner_all); }
    } restore{indices};
    if (FILE *probe = std::fopen((indices / "probe").c_str(), "wb")) { // root writes anyway
        std::fclose(probe);
        GTEST_SKIP() << "the Indices folder stays writable for this user";
    }
#endif
    std::vector<std::int64_t> progress;
    const auto video = openIndexed(path, 1, file, &progress);
    ASSERT_TRUE(video);
    EXPECT_TRUE(video->newIndex);
    EXPECT_FALSE(progress.empty());
    EXPECT_FALSE(std::filesystem::is_regular_file(file));
    const auto handoff = video->handoffIndexFile;
    ASSERT_FALSE(handoff.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(handoff));
    EXPECT_NE(std::filesystem::path(handoff).parent_path(), indices);

    backends::FfmsIndexedSource box{QStringLiteral(HIKARI_MEDIA_HELPER)};
    progress.clear();
    const auto opened = openDisplayPath(box, path, 1, handoff, &progress);
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_FALSE(opened->newIndex);
    EXPECT_TRUE(progress.empty());
    std::optional<std::expected<AudioBlock, AudioFailure>> block;
    box.displayAudio({1000, 4, 4, false}, [&](auto r) { block = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return block.has_value(); }));
    ASSERT_TRUE(*block);
    EXPECT_EQ(left(**block, 0), 1000);

    source.releaseIndexHandoff(handoff + ".other");
    EXPECT_TRUE(std::filesystem::exists(handoff));
    source.releaseIndexHandoff(handoff);
    EXPECT_FALSE(std::filesystem::exists(handoff));
    const auto again = openIndexed(path, 1, file);
    ASSERT_TRUE(again);
    ASSERT_FALSE(again->handoffIndexFile.empty());
    EXPECT_NE(again->handoffIndexFile, handoff);
    EXPECT_TRUE(std::filesystem::exists(again->handoffIndexFile));
    const auto plain = openIndexed(folder.copy("cfr"), -1, (folder.dir / "elsewhere" / "cfr_-1.ffindex").string());
    ASSERT_TRUE(plain);
    EXPECT_TRUE(plain->handoffIndexFile.empty());
    EXPECT_FALSE(std::filesystem::exists(again->handoffIndexFile));
    std::optional<backends::FfmsIndexedSource> ending;
    ending.emplace(QStringLiteral(HIKARI_MEDIA_HELPER));
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    ending->openIndexed(path, IndexRequest{1, file}, nullptr, [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    ASSERT_TRUE(*result);
    const auto last = (*result)->handoffIndexFile;
    ASSERT_FALSE(last.empty());
    EXPECT_TRUE(std::filesystem::exists(last));
    ending.reset();
    EXPECT_FALSE(std::filesystem::exists(last));
}

TEST_F(DisplayAudioFixture, FailuresCarryTheStageAndFfms2sText)
{
    const auto missing = probe("/nonexistent/missing.wav");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().stage, AudioStage::Indexer);
    EXPECT_FALSE(missing.error().message.empty());
    const auto noAudio = openDisplay("cfr", 0);
    ASSERT_FALSE(noAudio);
    EXPECT_EQ(noAudio.error().error, SourceError::Unsupported);
    EXPECT_TRUE(openDisplay("audio"));
}

TEST_F(DisplayAudioFixture, ClosingTheBoxLetsGoOfTheFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString clip = folder.filePath(QStringLiteral("audio.mka"));
    ASSERT_TRUE(QFile::copy(QString::fromStdString(fixture("audioonly")), clip));
    const auto tracks = probe(clip.toStdString());
    ASSERT_TRUE(tracks);
    ASSERT_EQ(tracks->audio.size(), 1u);
    const int track = tracks->audio[0].index;
    const auto opened = openDisplayPath(source, clip.toStdString(), track, {});
    ASSERT_TRUE(opened) << opened.error().message;
    ASSERT_TRUE(display(0, 56));
    EXPECT_FALSE(openfiles::nothingHolds(clip));
    source.closeDisplay();
    EXPECT_TRUE(waitFor([&] { return openfiles::nothingHolds(clip); }, 5000)) << openfiles::under(folder.path()).join(u'\n').toStdString();
    EXPECT_EQ(display(0, 56).error().error, SourceError::NotOpen);
    EXPECT_TRUE(openDisplayPath(source, clip.toStdString(), track, {}));
}
