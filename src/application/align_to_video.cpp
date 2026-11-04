// Copyright (c) 2019, Charlie Jiang
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "hikari/application/align_to_video.h"

#include <cmath>
#include <cstdlib>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;
constexpr int kShapeSlack = 5;

struct Lab {
    double l = 0, a = 0, b = 0;
};

double labCurve(double t)
{
    return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

Lab lab(KeyColour c)
{
    const double x = (0.412453 * c.r + 0.357580 * c.g + 0.180423 * c.b) / 255.0 / 0.950456;
    const double y = (0.212671 * c.r + 0.715160 * c.g + 0.072169 * c.b) / 255.0;
    const double z = (0.019334 * c.r + 0.119193 * c.g + 0.950227 * c.b) / 255.0 / 1.088854;
    const double fx = labCurve(x), fy = labCurve(y), fz = labCurve(z);
    return {y > 0.008856 ? 116.0 * fy - 16.0 : 903.3 * y, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

class Matcher {
public:
    Matcher(KeyColour key, int tolerance) : m_key(lab(key)), m_tolerance(tolerance) {}
    bool operator()(KeyColour c) const
    {
        const Lab p = lab(c);
        return std::hypot(p.l - m_key.l, p.a - m_key.a, p.b - m_key.b) <= m_tolerance;
    }

private:
    Lab m_key;
    int m_tolerance;
};

KeyColour at(const IndexedFrame &frame, int x, int y)
{
    const auto *p = reinterpret_cast<const std::uint8_t *>(frame.bgra.data()) + static_cast<std::size_t>(y) * frame.stride +
                    static_cast<std::size_t>(x) * 4;
    return {p[2], p[1], p[0]};
}

bool readable(const IndexedFrame &frame)
{
    return !frame.ycbcr && frame.stride >= frame.width * 4 &&
           frame.bgra.size() >= static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(frame.height);
}

}

std::optional<KeyColour> pixelColour(const IndexedFrame &frame, int x, int y)
{
    if (!readable(frame) || x < 0 || y < 0 || x >= frame.width || y >= frame.height)
        return std::nullopt;
    return at(frame, x, y);
}

bool coloursMatch(KeyColour a, KeyColour b, int tolerance)
{
    return Matcher(a, tolerance)(b);
}

std::optional<KeyShape> keyShape(const IndexedFrame &frame, const KeyPoint &point)
{
    const auto own = pixelColour(frame, point.x, point.y);
    const Matcher matches(point.colour, point.tolerance);
    if (!own || !matches(*own))
        return std::nullopt;
    KeyShape shape{-1, frame.width, -1, frame.height};
    for (int x = point.x + 1; x < frame.width; ++x)
        if (!matches(at(frame, x, point.y))) {
            shape.right = x;
            break;
        }
    for (int x = point.x - 1; x >= 0; --x)
        if (!matches(at(frame, x, point.y))) {
            shape.left = x;
            break;
        }
    for (int y = point.y + 1; y < frame.height; ++y)
        if (!matches(at(frame, point.x, y))) {
            shape.down = y;
            break;
        }
    for (int y = point.y - 1; y >= 0; --y)
        if (!matches(at(frame, point.x, y))) {
            shape.up = y;
            break;
        }
    return shape;
}

bool keyPointHolds(const IndexedFrame &frame, const KeyPoint &point, const KeyShape &reference)
{
    const auto shape = keyShape(frame, point);
    const auto near = [](int a, int b) { return std::abs(a - b) <= kShapeSlack; };
    return shape && near(shape->left, reference.left) && near(shape->right, reference.right) &&
           near(shape->up, reference.up) && near(shape->down, reference.down);
}

AlignSearch::AlignSearch(int picked, int frameCount) : m_frameCount(frameCount), m_first(picked), m_last(picked) {}

std::optional<int> AlignSearch::next() const
{
    if (m_backwards && m_first > 0)
        return m_first - 1;
    if (m_last >= 0 && m_last + 1 < m_frameCount)
        return m_last + 1;
    return std::nullopt;
}

void AlignSearch::report(bool holds)
{
    ++m_checked;
    if (m_backwards && m_first > 0) {
        if (holds)
            --m_first;
        else
            m_backwards = false;
        return;
    }
    if (holds)
        ++m_last;
    else
        m_frameCount = m_last + 1;
}

std::expected<void, CommandRefusal> alignActiveLine(EditSession &session, const LegacyTimebase &timebase, int first, int last)
{
    if (session.draftLine())
        session.commitDraft();
    const auto active = session.selection().active;
    if (!active || timebase.empty() || first < 0 || last < first)
        return std::unexpected(CommandRefusal::Invalid);
    const auto start = core::DocumentTime(std::int64_t{timebase.startTimeFor(first)} * kUsPerMs);
    const auto end = core::DocumentTime(std::int64_t{timebase.endTimeFor(last)} * kUsPerMs);
    const auto selection = session.selection();
    bool changed = false;
    const auto ran = session.run(Command{"Aligning to video", session.revision(), {*active}, [&](core::Document &d) {
                                             return d.editLine(*active, [&](core::LineRecord &l) {
                                                 changed = l.start.value != start || l.end.value != end;
                                                 l.start.value = start;
                                                 l.end.value = end;
                                             }) && changed;
                                         }});
    if (!ran)
        return !changed && ran.error() == CommandRefusal::Invalid ? std::expected<void, CommandRefusal>{}
                                                                    : std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

}
