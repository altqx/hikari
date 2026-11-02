// Ported from libaegisub/common/vfr.cpp (Aegisub 3.5.1):
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "hikari/application/aegisub_frame_times.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace hikari::application {

namespace {

constexpr std::int64_t kDenominator = 1'000'000'000;
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

}

std::optional<AegisubFrameTimes> AegisubFrameTimes::fromFrameStarts(std::vector<std::int64_t> startsMs)
{
    if (startsMs.size() < 2 || !std::ranges::is_sorted(startsMs) || startsMs.back() <= startsMs.front())
        return std::nullopt;
    return AegisubFrameTimes(std::move(startsMs));
}

AegisubFrameTimes::AegisubFrameTimes(std::vector<std::int64_t> startsMs) : m_starts(std::move(startsMs))
{
    const std::int64_t first = m_starts.front();
    for (auto &start : m_starts)
        start -= first;
    const auto frames = static_cast<std::int64_t>(m_starts.size()) - 1;
    m_numerator = frames * kDenominator * 1000 / m_starts.back();
    m_last = frames * kDenominator * 1000;
}

std::int64_t AegisubFrameTimes::frameAt(std::int64_t ms) const
{
    if (ms < 0)
        return (ms * m_numerator / kDenominator - 999) / 1000;
    if (ms > m_starts.back())
        return ((ms + 1) * m_numerator - m_last - m_numerator / 2 + (1000 * kDenominator - 1)) / (1000 * kDenominator) +
               static_cast<std::int64_t>(m_starts.size()) - 2;
    return std::ranges::upper_bound(m_starts, ms) - m_starts.begin() - 1;
}

std::int64_t AegisubFrameTimes::timeAt(std::int64_t frame) const
{
    if (frame < 0)
        return frame * kDenominator * 1000 / m_numerator;
    const auto size = static_cast<std::int64_t>(m_starts.size());
    if (frame >= size)
        return ((frame - size + 1) * 1000 * kDenominator + m_last + m_numerator / 2) / m_numerator;
    return m_starts[static_cast<std::size_t>(frame)];
}

std::int64_t AegisubFrameTimes::frameFromMs(std::int64_t ms) const
{
    const std::int64_t reach = kMax / m_numerator - 2;
    return frameAt(std::clamp(ms, -reach, reach) - 1) + 1;
}

std::int64_t AegisubFrameTimes::msFromFrame(std::int64_t frame) const
{
    const std::int64_t reach = (kMax - m_last - m_numerator) / (1000 * kDenominator) - 1;
    frame = std::clamp(frame, -reach, static_cast<std::int64_t>(m_starts.size()) + reach - 1);
    const std::int64_t prev = timeAt(frame - 1), cur = timeAt(frame);
    return prev + (cur - prev + 1) / 2;
}

}
