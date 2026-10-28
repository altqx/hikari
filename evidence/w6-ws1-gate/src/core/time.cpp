#include "hikari/core/time.h"

#include <limits>
#include <numeric>

namespace hikari::core {

namespace {

constexpr std::int64_t kMicrosPerSecond = 1'000'000;

} // namespace

Checked<TimeDelta> TimeDelta::fromMilliseconds(std::int64_t milliseconds)
{
    return checkedMul(milliseconds, 1000).transform([](std::int64_t us) { return TimeDelta(us); });
}

Checked<TimeDelta> TimeDelta::plus(TimeDelta other) const
{
    return checkedAdd(m_us, other.m_us).transform([](std::int64_t us) { return TimeDelta(us); });
}

Checked<TimeDelta> TimeDelta::minus(TimeDelta other) const
{
    return checkedSub(m_us, other.m_us).transform([](std::int64_t us) { return TimeDelta(us); });
}

Checked<TimeDelta> TimeDelta::negated() const
{
    return checkedSub(0, m_us).transform([](std::int64_t us) { return TimeDelta(us); });
}

Checked<DocumentTime> DocumentTime::fromMilliseconds(std::int64_t milliseconds)
{
    return checkedMul(milliseconds, 1000).transform([](std::int64_t us) { return DocumentTime(us); });
}

Checked<DocumentTime> DocumentTime::plus(TimeDelta delta) const
{
    return checkedAdd(m_us, delta.microseconds()).transform([](std::int64_t us) { return DocumentTime(us); });
}

Checked<TimeDelta> DocumentTime::minus(DocumentTime other) const
{
    return checkedSub(m_us, other.m_us).transform([](std::int64_t us) { return TimeDelta(us); });
}

Checked<std::int64_t> VideoFrameIndex::minus(VideoFrameIndex other) const
{
    return checkedSub(m_index, other.m_index);
}

Checked<Rational> Rational::make(std::int64_t numerator, std::int64_t denominator)
{
    if (denominator == 0)
        return std::unexpected(ArithmeticError::DivisionByZero);
    // Negating INT64_MIN would overflow; such a value cannot be normalized.
    if (numerator == std::numeric_limits<std::int64_t>::min() ||
        denominator == std::numeric_limits<std::int64_t>::min())
        return std::unexpected(ArithmeticError::Overflow);
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    const std::int64_t g = std::gcd(numerator, denominator);
    return Rational(numerator / g, denominator / g);
}

std::strong_ordering Rational::operator<=>(const Rational &other) const
{
    // a/b <=> c/d with b, d > 0 is exactly a*d <=> c*b.
    return compareProducts(m_num, other.m_den, other.m_num, m_den);
}

Checked<FrameRate> FrameRate::make(std::int64_t numerator, std::int64_t denominator, Provenance provenance)
{
    auto fps = Rational::make(numerator, denominator);
    if (!fps)
        return std::unexpected(fps.error());
    if (fps->numerator() <= 0)
        return std::unexpected(ArithmeticError::InvalidRate);
    return FrameRate(*fps, provenance);
}

Checked<Rational> FrameRate::secondsAt(VideoFrameIndex index) const
{
    // index / (num/den) = index * den / num
    auto top = checkedMul(index.value(), m_fps.denominator());
    if (!top)
        return std::unexpected(top.error());
    return Rational::make(*top, m_fps.numerator());
}

Checked<DocumentTimeConversion> toDocumentTime(const MediaTimestamp &timestamp)
{
    const Rational &tb = timestamp.secondsPerTick;
    if (tb.numerator() <= 0)
        return std::unexpected(ArithmeticError::InvalidRate);
    // ticks * num * 1e6 / den, rounded to nearest with ties away from zero.
    auto scaled = checkedMul(tb.numerator(), kMicrosPerSecond);
    if (!scaled)
        return std::unexpected(scaled.error());
    auto us = mulDiv(timestamp.ticks, *scaled, tb.denominator(), 1, Rounding::NearestTiesAway);
    if (!us)
        return std::unexpected(us.error());
    auto floor = mulDiv(timestamp.ticks, *scaled, tb.denominator(), 1, Rounding::Floor);
    auto ceil = mulDiv(timestamp.ticks, *scaled, tb.denominator(), 1, Rounding::Ceil);
    return DocumentTimeConversion{DocumentTime(*us), floor && ceil && *floor == *ceil};
}

} // namespace hikari::core
