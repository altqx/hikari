#include "hikari/core/frame_timeline.h"

#include <algorithm>

namespace hikari::core {

namespace {

constexpr std::int64_t kMicrosPerSecond = 1'000'000;

// For a constant rate num/den fps, frame position of a relative time r (µs) is
// r * num / (den * 1e6), rounded as the lookup requires.
Checked<std::int64_t> framePosition(const FrameRate &rate, std::int64_t relativeUs, Rounding rounding)
{
    const Rational &fps = rate.framesPerSecond();
    return mulDiv(relativeUs, fps.numerator(), fps.denominator(), kMicrosPerSecond, rounding);
}

Checked<VideoFrameIndex> bounded(std::int64_t index, const std::optional<std::int64_t> &frameCount)
{
    if (index < 0 || (frameCount && index >= *frameCount))
        return std::unexpected(ArithmeticError::OutOfRange);
    return VideoFrameIndex(index);
}

} // namespace

FrameTimeline FrameTimeline::constantRate(FrameRate rate, DocumentTime origin, std::optional<std::int64_t> frameCount)
{
    return FrameTimeline(Constant{rate, origin, frameCount});
}

FrameTimeline FrameTimeline::indexed(std::vector<DocumentTime> starts, std::optional<DocumentTime> lastFrameEnd)
{
    std::vector<TimelineDiagnostic> diagnostics;
    for (std::size_t i = 1; i < starts.size(); ++i)
        if (!(starts[i - 1] < starts[i]))
            diagnostics.push_back({TimelineDiagnostic::Kind::NotStrictlyIncreasing,
                                   VideoFrameIndex(static_cast<std::int64_t>(i))});
    const bool ordered = diagnostics.empty();
    FrameTimeline timeline(Indexed{std::move(starts), lastFrameEnd, ordered});
    timeline.m_diagnostics = std::move(diagnostics);
    return timeline;
}

Provenance FrameTimeline::provenance() const
{
    if (const auto *c = std::get_if<Constant>(&m_data))
        return c->rate.provenance();
    const auto &v = std::get<Indexed>(m_data);
    return v.ordered && !v.starts.empty() ? Provenance::Exact : Provenance::Estimated;
}

Checked<VideoFrameIndex> FrameTimeline::frameAtOrAfter(DocumentTime t) const
{
    if (const auto *c = std::get_if<Constant>(&m_data)) {
        auto rel = t.minus(c->origin);
        if (!rel)
            return std::unexpected(rel.error());
        if (rel->microseconds() <= 0)
            return bounded(0, c->frameCount);
        auto index = framePosition(c->rate, rel->microseconds(), Rounding::Ceil);
        if (!index)
            return std::unexpected(index.error());
        return bounded(*index, c->frameCount);
    }
    const auto &v = std::get<Indexed>(m_data);
    if (v.ordered) {
        const auto it = std::lower_bound(v.starts.begin(), v.starts.end(), t);
        if (it == v.starts.end())
            return std::unexpected(ArithmeticError::OutOfRange);
        return VideoFrameIndex(it - v.starts.begin());
    }
    // Disordered starts: keep identity, scan in presentation order.
    for (std::size_t i = 0; i < v.starts.size(); ++i)
        if (v.starts[i] >= t)
            return VideoFrameIndex(static_cast<std::int64_t>(i));
    return std::unexpected(ArithmeticError::OutOfRange);
}

Checked<VideoFrameIndex> FrameTimeline::frameContaining(DocumentTime t) const
{
    if (const auto *c = std::get_if<Constant>(&m_data)) {
        auto rel = t.minus(c->origin);
        if (!rel)
            return std::unexpected(rel.error());
        if (rel->microseconds() < 0)
            return std::unexpected(ArithmeticError::OutOfRange);
        auto index = framePosition(c->rate, rel->microseconds(), Rounding::Floor);
        if (!index)
            return std::unexpected(index.error());
        return bounded(*index, c->frameCount);
    }
    const auto &v = std::get<Indexed>(m_data);
    if (v.starts.empty() || t < v.starts.front())
        return std::unexpected(ArithmeticError::OutOfRange);
    std::size_t index = 0;
    if (v.ordered) {
        index = static_cast<std::size_t>(std::upper_bound(v.starts.begin(), v.starts.end(), t) - v.starts.begin()) - 1;
    } else {
        for (std::size_t i = 0; i < v.starts.size() && v.starts[i] <= t; ++i)
            index = i;
    }
    if (index + 1 == v.starts.size()) {
        // The final frame's extent is only known with a known end.
        const bool inside = v.lastFrameEnd ? t < *v.lastFrameEnd : t == v.starts.back();
        if (!inside)
            return std::unexpected(ArithmeticError::OutOfRange);
    }
    return VideoFrameIndex(static_cast<std::int64_t>(index));
}

Checked<VideoFrameIndex> FrameTimeline::lastFrameStartingBefore(DocumentTime end) const
{
    if (const auto *c = std::get_if<Constant>(&m_data)) {
        auto rel = end.minus(c->origin);
        if (!rel)
            return std::unexpected(rel.error());
        if (rel->microseconds() <= 0)
            return std::unexpected(ArithmeticError::OutOfRange);
        // Last i with origin + i/fps < end is ceil(position) - 1.
        auto first = framePosition(c->rate, rel->microseconds(), Rounding::Ceil);
        if (!first)
            return std::unexpected(first.error());
        std::int64_t index = *first - 1;
        if (c->frameCount && index >= *c->frameCount)
            index = *c->frameCount - 1;
        return bounded(index, c->frameCount);
    }
    const auto &v = std::get<Indexed>(m_data);
    std::optional<std::size_t> found;
    if (v.ordered) {
        const auto it = std::lower_bound(v.starts.begin(), v.starts.end(), end);
        if (it != v.starts.begin())
            found = static_cast<std::size_t>(it - v.starts.begin()) - 1;
    } else {
        for (std::size_t i = 0; i < v.starts.size(); ++i)
            if (v.starts[i] < end)
                found = i;
    }
    if (!found)
        return std::unexpected(ArithmeticError::OutOfRange);
    return VideoFrameIndex(static_cast<std::int64_t>(*found));
}

Checked<Rational> FrameTimeline::frameStartMicroseconds(VideoFrameIndex frame) const
{
    if (const auto *c = std::get_if<Constant>(&m_data)) {
        if (frame.value() < 0 || (c->frameCount && frame.value() >= *c->frameCount))
            return std::unexpected(ArithmeticError::OutOfRange);
        auto seconds = c->rate.secondsAt(frame);
        if (!seconds)
            return std::unexpected(seconds.error());
        // origin + seconds * 1e6, kept exact: (origin*den + num*1e6) / den.
        const std::int64_t den = seconds->denominator();
        auto scaledOrigin = checkedMul(c->origin.microseconds(), den);
        auto offset = checkedMul(seconds->numerator(), kMicrosPerSecond);
        if (!scaledOrigin || !offset)
            return std::unexpected(ArithmeticError::Overflow);
        auto total = checkedAdd(*scaledOrigin, *offset);
        if (!total)
            return std::unexpected(total.error());
        return Rational::make(*total, den);
    }
    const auto &v = std::get<Indexed>(m_data);
    if (frame.value() < 0 || static_cast<std::size_t>(frame.value()) >= v.starts.size())
        return std::unexpected(ArithmeticError::OutOfRange);
    return Rational::integer(v.starts[static_cast<std::size_t>(frame.value())].microseconds());
}

Checked<std::int64_t> frameOffset(const FrameTimeline &timeline, DocumentTime anchor, DocumentTime target)
{
    auto a = timeline.frameAtOrAfter(anchor);
    if (!a)
        return std::unexpected(a.error());
    auto b = timeline.frameAtOrAfter(target);
    if (!b)
        return std::unexpected(b.error());
    return b->minus(*a);
}

} // namespace hikari::core
