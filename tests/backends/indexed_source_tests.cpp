// N1: exact indexed frames from the isolated FFMS2 media helper (M50-seek).
// Fixtures are generated at test time; each frame carries its index as a
// barcode, so a decoded frame proves its own identity.

#include "hikari/backends/ffms_indexed_source.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <algorithm>
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

TEST_F(Fixture, HelperLossIsReportedAndAReopenRecovers)
{
    ASSERT_TRUE(open("cfr"));
    std::optional<std::expected<IndexedFrame, SourceError>> pending;
    source.frame(5, [&](auto r) { pending = std::move(r); });
    source.helperHost()->stop(); // the helper crashes
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->error(), SourceError::HelperLost);
    ASSERT_TRUE(open("cfr")); // a fresh helper process
    EXPECT_EQ(barcode(*frame(5)), 5);
}

TEST_F(Fixture, UnreadableFilesFailExplicitly)
{
    std::optional<std::expected<SourceTimeline, SourceError>> result;
    source.open(std::string(HIKARI_MEDIA_FIXTURES) + "/missing.mkv", {}, [&](auto r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_EQ(result->error(), SourceError::InvalidInput);
}
