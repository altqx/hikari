// C1: typed time values and frame lookup (docs/qt/time-semantics.md).
// Fixture-driven cases read the approved-departure inputs and the recorded
// legacy observations, so each approved change keeps its old result beside it.

#include "hikari/core/frame_timeline.h"
#include "hikari/core/time.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <limits>
#include <random>

using namespace hikari::core;

namespace {

constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
constexpr auto kMin = std::numeric_limits<std::int64_t>::min();

QJsonObject loadJson(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString fixture(const char *name)
{
    return QStringLiteral(HIKARI_FIXTURE_DIR "/inputs/") + QLatin1String(name);
}

DocumentTime ms(std::int64_t v)
{
    return *DocumentTime::fromMilliseconds(v);
}

FrameTimeline vfr(std::initializer_list<std::int64_t> startsMs)
{
    std::vector<DocumentTime> starts;
    for (auto s : startsMs)
        starts.push_back(ms(s));
    return FrameTimeline::indexed(std::move(starts));
}

} // namespace

// ---- checked arithmetic -------------------------------------------------

TEST(Checked, AdditionAndSubtractionNeverWrap)
{
    EXPECT_EQ(DocumentTime(kMax).plus(TimeDelta(1)).error(), ArithmeticError::Overflow);
    EXPECT_EQ(DocumentTime(kMin).plus(TimeDelta(-1)).error(), ArithmeticError::Overflow);
    EXPECT_EQ(DocumentTime(kMin).minus(DocumentTime(1)).error(), ArithmeticError::Overflow);
    EXPECT_EQ(TimeDelta(kMin).negated().error(), ArithmeticError::Overflow);
    EXPECT_EQ(DocumentTime::fromMilliseconds(kMax / 1000 + 1).error(), ArithmeticError::Overflow);
    EXPECT_EQ(DocumentTime(kMax).plus(TimeDelta(0))->microseconds(), kMax);
}

TEST(Checked, SignedSubtractionGivesSignedDelta)
{
    EXPECT_EQ(ms(10).minus(ms(30))->microseconds(), -20'000);
    EXPECT_EQ(ms(30).minus(ms(10))->microseconds(), 20'000);
}

TEST(Checked, MulDivRoundsAsRequested)
{
    EXPECT_EQ(*mulDiv(7, 1, 2, 1, Rounding::Floor), 3);
    EXPECT_EQ(*mulDiv(7, 1, 2, 1, Rounding::Ceil), 4);
    EXPECT_EQ(*mulDiv(7, 1, 2, 1, Rounding::NearestTiesAway), 4);   // 3.5 -> 4
    EXPECT_EQ(*mulDiv(-7, 1, 2, 1, Rounding::NearestTiesAway), -4); // -3.5 -> -4
    EXPECT_EQ(*mulDiv(-7, 1, 2, 1, Rounding::Floor), -4);
    EXPECT_EQ(*mulDiv(-7, 1, 2, 1, Rounding::Ceil), -3);
    EXPECT_EQ(*mulDiv(kMax, kMax, kMax, 1, Rounding::Floor), kMax);  // 128-bit intermediate
    EXPECT_EQ(mulDiv(kMax, 2, 1, 1, Rounding::Floor).error(), ArithmeticError::Overflow);
    EXPECT_EQ(*mulDiv(kMin, 1, 1, 1, Rounding::Floor), kMin);
    EXPECT_EQ(mulDiv(1, 1, 0, 1, Rounding::Floor).error(), ArithmeticError::DivisionByZero);
}

TEST(Checked, PortablePathMatchesNative)
{
    std::mt19937_64 rng(20260929);
    std::uniform_int_distribution<std::int64_t> any(kMin, kMax);
    std::uniform_int_distribution<std::int64_t> small(-1'000'000'000, 1'000'000'000);
    std::uniform_int_distribution<std::int64_t> positive(1, kMax);
    for (int i = 0; i < 20000; ++i) {
        const std::int64_t a = i % 2 ? any(rng) : small(rng);
        const std::int64_t b = i % 3 ? small(rng) : any(rng);
        const std::int64_t c = i % 5 ? positive(rng) % 2'000'000'000 + 1 : positive(rng);
        const std::int64_t d = i % 7 ? 1 : positive(rng) % 1'000'000 + 1;
        for (auto r : {Rounding::Floor, Rounding::Ceil, Rounding::NearestTiesAway})
            ASSERT_EQ(mulDiv(a, b, c, d, r), detail::mulDivPortable(a, b, c, d, r))
                << a << '*' << b << '/' << c << '*' << d;
        ASSERT_EQ(compareProducts(a, b, c, d), detail::compareProductsPortable(a, b, c, d));
    }
}

// ---- C01-equality ------------------------------------------------------

TEST(C01Equality, EqualTimesSatisfyBothComparisons)
{
    const auto fx = loadJson(fixture("equality.json"));
    for (const char *group : {"legacy_representable_cases", "new_typed_range_cases"}) {
        for (const auto &v : fx[QLatin1String(group)].toArray()) {
            const auto c = v.toObject();
            const DocumentTime l(c["lhs"].toString().toLongLong());
            const DocumentTime r(c["rhs"].toString().toLongLong());
            const auto e = c["expected"].toObject();
            EXPECT_EQ(l >= r, e["ge"].toBool()) << group;
            EXPECT_EQ(l <= r, e["le"].toBool()) << group;
            EXPECT_EQ(l == r, e["eq"].toBool()) << group;
            EXPECT_EQ(l > r, e["gt"].toBool()) << group;
            EXPECT_EQ(l < r, e["lt"].toBool()) << group;
        }
    }
}

TEST(C01Equality, LegacyObservationIsTheDepartedBehavior)
{
    // Recorded legacy result (run 36591631319): equal times gave >= and <= false.
    const auto obs = loadJson(QStringLiteral(HIKARI_LEGACY_OBSERVATIONS));
    int checked = 0;
    for (const auto &cv : obs["cases"].toArray()) {
        const auto c = cv.toObject();
        if (c["id"].toString() != "C01-equality")
            continue;
        for (const auto &ov : c["observations"].toArray()) {
            const auto o = ov.toObject()["observed"].toObject();
            EXPECT_EQ(o["ge"].toInt(), 0);
            EXPECT_EQ(o["le"].toInt(), 0);
            EXPECT_EQ(o["eq"].toInt(), 1);
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3);
    EXPECT_TRUE(DocumentTime(1000) >= DocumentTime(1000)); // approved, differs from the above
}

// ---- intervals ---------------------------------------------------------

TEST(TimeRange, HalfOpenEmptyAndReversed)
{
    const TimeRange r{ms(40), ms(81)};
    EXPECT_TRUE(r.contains(ms(40)));
    EXPECT_FALSE(r.contains(ms(81)));
    EXPECT_TRUE((TimeRange{ms(5), ms(5)}).isEmpty());
    EXPECT_FALSE((TimeRange{ms(5), ms(5)}).contains(ms(5)));
    const TimeRange reversed{ms(30), ms(10)};
    EXPECT_TRUE(reversed.isReversed());
    EXPECT_FALSE(reversed.contains(ms(20)));
    EXPECT_EQ(reversed.start, ms(30)); // kept as authored, not swapped
}

// ---- rationals, rates and media timestamps -------------------------------

TEST(Rational, NormalizedAndExactlyCompared)
{
    EXPECT_EQ(*Rational::make(2, -4), *Rational::make(-1, 2));
    EXPECT_EQ(Rational::make(1, 0).error(), ArithmeticError::DivisionByZero);
    EXPECT_LT(*Rational::make(1, 3), *Rational::make(333333333, 999999998));
    // n/(n-1) shrinks as n grows; the cross products need all 128 bits.
    EXPECT_LT(*Rational::make(kMax, kMax - 1), *Rational::make(kMax - 1, kMax - 2));
    EXPECT_EQ(*Rational::make(kMax, kMax - 1) <=> *Rational::make(kMax, kMax - 1), std::strong_ordering::equal);
    EXPECT_LT(*Rational::make(-kMax, 1), *Rational::make(-kMax + 1, 1));
}

TEST(FrameRate, KeepsTheExactFraction)
{
    const auto ntsc = *FrameRate::make(24000, 1001);
    EXPECT_EQ(ntsc.framesPerSecond().numerator(), 24000);
    EXPECT_EQ(ntsc.framesPerSecond().denominator(), 1001);
    EXPECT_EQ(FrameRate::make(0, 1).error(), ArithmeticError::InvalidRate);
    EXPECT_EQ(FrameRate::make(-24, 1).error(), ArithmeticError::InvalidRate);
    // Frame 1 starts at 1001/24 ms = 1001/24000 s, retained as that fraction.
    EXPECT_EQ(*ntsc.secondsAt(VideoFrameIndex(1)), *Rational::make(1001, 24000));
}

TEST(MediaTimestamp, NearestMicrosecondTiesAwayAndReportsLoss)
{
    // 1/3 s -> 333333.33 µs -> 333333, inexact.
    auto third = toDocumentTime({1, *Rational::make(1, 3)});
    ASSERT_TRUE(third);
    EXPECT_EQ(third->time.microseconds(), 333'333);
    EXPECT_FALSE(third->exact);
    // 1 tick of 1/2000000 s = 0.5 µs -> ties away from zero -> 1 and -1.
    EXPECT_EQ(toDocumentTime({1, *Rational::make(1, 2'000'000)})->time.microseconds(), 1);
    EXPECT_EQ(toDocumentTime({-1, *Rational::make(1, 2'000'000)})->time.microseconds(), -1);
    // 90 kHz ticks are exact at whole milliseconds.
    auto exact = toDocumentTime({90'000, *Rational::make(1, 90'000)});
    EXPECT_EQ(exact->time.microseconds(), 1'000'000);
    EXPECT_TRUE(exact->exact);
    EXPECT_EQ(toDocumentTime({kMax, Rational::integer(1)}).error(), ArithmeticError::Overflow);
}

// ---- frame lookup ------------------------------------------------------

TEST(FrameLookup, SpecExampleVariableRate)
{
    const auto tl = vfr({0, 40, 81, 120});
    EXPECT_EQ(tl.provenance(), Provenance::Exact);
    EXPECT_EQ(tl.frameAtOrAfter(ms(41))->value(), 2);
    EXPECT_EQ(tl.frameContaining(ms(41))->value(), 1);
    EXPECT_EQ(tl.frameContaining(ms(81))->value(), 2); // exactly 81 ms shows frame 2
    // [40,81) covers frame 1 only.
    EXPECT_EQ(tl.frameAtOrAfter(ms(40))->value(), 1);
    EXPECT_EQ(tl.lastFrameStartingBefore(ms(81))->value(), 1);
}

TEST(FrameLookup, OutOfRangeIsAnErrorNotAClamp)
{
    const auto tl = vfr({0, 40, 81, 120});
    EXPECT_EQ(tl.frameAtOrAfter(ms(121)).error(), ArithmeticError::OutOfRange);
    EXPECT_EQ(tl.frameContaining(ms(-1)).error(), ArithmeticError::OutOfRange);
    // Final frame without a known end: only its exact start is inside.
    EXPECT_EQ(tl.frameContaining(ms(120))->value(), 3);
    EXPECT_EQ(tl.frameContaining(ms(130)).error(), ArithmeticError::OutOfRange);
    const auto ended = FrameTimeline::indexed({ms(0), ms(40)}, ms(80));
    EXPECT_EQ(ended.frameContaining(ms(79))->value(), 1);
    EXPECT_EQ(ended.frameContaining(ms(80)).error(), ArithmeticError::OutOfRange);
}

TEST(FrameLookup, DisorderedStartsKeepIdentityAndAreEstimated)
{
    const auto tl = vfr({0, 40, 40, 30, 120});
    EXPECT_EQ(tl.provenance(), Provenance::Estimated);
    ASSERT_EQ(tl.diagnostics().size(), 2u);
    EXPECT_EQ(tl.diagnostics()[0].frame.value(), 2);
    EXPECT_EQ(tl.diagnostics()[1].frame.value(), 3);
    EXPECT_EQ(tl.frameAtOrAfter(ms(35))->value(), 1); // first start >= 35 in presentation order
    EXPECT_EQ(tl.frameStartMicroseconds(VideoFrameIndex(3))->numerator(), 30'000); // not renumbered
}

TEST(FrameLookup, ConstantRateIsExactAtNtscBoundaries)
{
    const auto tl = FrameTimeline::constantRate(*FrameRate::make(24000, 1001), DocumentTime(0));
    // Frame 1 starts at 1001/24 ms = 41708.33.. µs.
    EXPECT_EQ(*tl.frameStartMicroseconds(VideoFrameIndex(1)), *Rational::make(1'001'000'000, 24'000));
    EXPECT_EQ(tl.frameAtOrAfter(DocumentTime(41'708))->value(), 1);
    EXPECT_EQ(tl.frameAtOrAfter(DocumentTime(41'709))->value(), 2);
    EXPECT_EQ(tl.frameContaining(DocumentTime(41'708))->value(), 0);
    EXPECT_EQ(tl.frameContaining(DocumentTime(41'709))->value(), 1);
    EXPECT_EQ(tl.lastFrameStartingBefore(DocumentTime(41'709))->value(), 1);
    EXPECT_EQ(tl.lastFrameStartingBefore(DocumentTime(41'708))->value(), 0);
}

TEST(FrameLookup, ConstantRateOriginAndBounds)
{
    const auto tl = FrameTimeline::constantRate(*FrameRate::make(25, 1), ms(1000), 10);
    EXPECT_EQ(tl.frameAtOrAfter(ms(0))->value(), 0);
    EXPECT_EQ(tl.frameContaining(ms(999)).error(), ArithmeticError::OutOfRange);
    EXPECT_EQ(tl.frameContaining(ms(1040))->value(), 1);
    EXPECT_EQ(tl.frameContaining(ms(1400)).error(), ArithmeticError::OutOfRange); // frame 10 does not exist
    EXPECT_EQ(tl.lastFrameStartingBefore(ms(5000))->value(), 9);
}

// ---- C01-fps-isolation -------------------------------------------------

TEST(C01FpsIsolation, EachDocumentRateIsIndependentAndExact)
{
    const auto fx = loadJson(fixture("fps-isolation.json"));
    const VideoFrameIndex start(fx["raw_start_frame"].toInt());
    const VideoFrameIndex end(fx["raw_end_frame"].toInt());
    std::optional<FrameRate> rateA, rateB;
    std::optional<Rational> aStart;
    for (const auto &sv : fx["steps"].toArray()) {
        const auto s = sv.toObject();
        auto &rate = s["document"].toString() == "A" ? rateA : rateB;
        if (s["rate"].isNull()) {
            rate.reset(); // B's time is unresolved; its raw frames stay 24/48
            EXPECT_FALSE(rateB.has_value());
            EXPECT_EQ(start.value(), 24);
            EXPECT_EQ(end.value(), 48);
        } else {
            const auto r = s["rate"].toObject();
            rate = *FrameRate::make(r["numerator"].toInt(), r["denominator"].toInt());
            const auto ex = s["expected_exact_seconds"].toObject();
            auto pair = [](const QJsonValue &v) {
                return *Rational::make(v.toArray()[0].toInt(), v.toArray()[1].toInt());
            };
            EXPECT_EQ(*rate->secondsAt(start), pair(ex["start"]));
            EXPECT_EQ(*rate->secondsAt(end), pair(ex["end"]));
        }
        if (rateA) {
            if (!aStart)
                aStart = *rateA->secondsAt(start);
            EXPECT_EQ(*rateA->secondsAt(start), *aStart); // A never changes with B
        }
    }
}

// ---- T42-A -------------------------------------------------------------

TEST(T42A, OffsetIsTheDifferenceOfFrameIndices)
{
    const auto fx = loadJson(fixture("audio-frame-offset.json"));
    const auto obs = loadJson(QStringLiteral(HIKARI_LEGACY_OBSERVATIONS));
    std::vector<int> legacy;
    for (const auto &cv : obs["cases"].toArray())
        if (cv.toObject()["id"].toString() == "T42-A")
            for (const auto &ov : cv.toObject()["observations"].toArray())
                legacy.push_back(ov.toObject()["observed"].toObject()["legacy"].toInt());
    const auto cases = fx["cases"].toArray();
    ASSERT_EQ(legacy.size(), static_cast<std::size_t>(cases.size()));
    int departures = 0;
    for (qsizetype i = 0; i < cases.size(); ++i) {
        const auto c = cases[i].toObject();
        std::vector<DocumentTime> starts;
        for (const auto &s : c["starts"].toArray())
            starts.push_back(DocumentTime(s.toInteger()));
        const auto tl = FrameTimeline::indexed(std::move(starts));
        const auto offset = frameOffset(tl, DocumentTime(c["anchor"].toInteger()), DocumentTime(c["target"].toInteger()));
        ASSERT_TRUE(offset) << i;
        EXPECT_EQ(*offset, c["expected_offset"].toInt()) << "case " << i;
        if (*offset != legacy[static_cast<std::size_t>(i)])
            ++departures;
    }
    // Recorded legacy offsets 2,0,0,1,1 vs approved 1,-1,1,1,0: all but the fourth change.
    EXPECT_EQ(departures, 4);
}
