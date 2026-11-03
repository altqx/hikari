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

#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/indexed_source.h"
#include "hikari/application/legacy_timebase.h"

#include <cstdint>
#include <expected>
#include <optional>

namespace hikari::application {

struct KeyColour {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const KeyColour &) const = default;
};

constexpr int kDefaultAlignTolerance = 20;
constexpr int kMaxAlignTolerance = 255;

// nullopt outside a BGRA frame.
std::optional<KeyColour> pixelColour(const IndexedFrame &frame, int x, int y);

// Within `tolerance` of each other in CIELAB.
bool coloursMatch(KeyColour a, KeyColour b, int tolerance);

struct KeyPoint {
    int x = 0, y = 0;
    KeyColour colour;
    int tolerance = kDefaultAlignTolerance;
};

// The nearest pixels left, right, above and below the key point whose colour
// does not match it, or the frame's edge (-1, width, -1, height).
struct KeyShape {
    int left = 0, right = 0, up = 0, down = 0;
    bool operator==(const KeyShape &) const = default;
};

// nullopt when the key point's own pixel does not match.
std::optional<KeyShape> keyShape(const IndexedFrame &frame, const KeyPoint &point);

// The key point matches and each side of its shape lies within 5 px of `reference`.
bool keyPointHolds(const IndexedFrame &frame, const KeyPoint &point, const KeyShape &reference);

// Checks one frame at a time: backwards from the picked frame to the first one
// where the key point holds, then forwards to the last.
class AlignSearch {
public:
    AlignSearch(int picked, int frameCount);

    // nullopt once both ends are found.
    std::optional<int> next() const;
    // Whether the key point holds on next().
    void report(bool holds);
    int first() const { return m_first; }
    int last() const { return m_last; }
    int checked() const { return m_checked; }

private:
    int m_frameCount;
    int m_first, m_last;
    bool m_backwards = true;
    int m_checked = 0;
};

// The active Line starts at the first frame and ends after the last in one
// step. No change records nothing and still succeeds. The pending draft is
// committed first. Refused (Invalid) without an active Line, with an empty
// timebase or with last before first.
std::expected<void, CommandRefusal> alignActiveLine(EditSession &session, const LegacyTimebase &timebase, int first, int last);

}
