#pragma once

// Hidden-tag projection of a Line's raw ASS text (docs/qt/ux/ass-editor.md).
// Raw text stays authoritative: the projection maps every visible UTF-16
// span back to its source span, and an edit in the projection rewrites only
// the selected visible source spans. It never strips tags and rebuilds ASS.
//
// Accepted defaults: text typed at a hidden-tag boundary goes after the tags,
// and a replacement across hidden tags keeps those exact tokens in order.
// Drawing payloads and malformed braces are protected: an edit touching them
// is refused and the raw text is left unchanged.

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

enum class SpanKind {
    Text,
    HiddenOverride, // "{...}", shown as nothing
    HardBreak,      // "\N", shown as a newline
    SoftBreak,      // "\n", shown as a space
    HardSpace,      // "\h", shown as NBSP
    DrawingPayload, // text while a top-level \pN (N != 0) is active
    Malformed,      // unclosed or nested "{", unmatched "}"
};

struct ProjectionSpan {
    std::size_t rawStart = 0, rawEnd = 0;         // UTF-16 offsets in the raw text
    std::size_t displayStart = 0, displayEnd = 0; // UTF-16 offsets in the projection
    SpanKind kind = SpanKind::Text;
    bool isProtected() const { return kind == SpanKind::DrawingPayload || kind == SpanKind::Malformed; }
};

// Shown for each protected span: a visible reserved marker.
inline constexpr char16_t kProtectedMarker = u'\u25A1';

struct Projection {
    std::u16string text;
    std::vector<ProjectionSpan> spans;
};

Projection project(std::u16string_view raw);

enum class MapRefusal {
    SplitsCharacter, // start or end is inside a grapheme (or a surrogate pair)
    AssSyntax,       // the inserted text contains braces, backslashes or the marker
    Protected,       // the edit touches a drawing payload or malformed span
    SplitsToken,     // the selection cuts an escape such as "\N"
    Reinterpreted,   // the result would read differently (for example as drawing)
};

// Replaces the projection range [start, end) with `inserted`, returning the new
// raw text. `graphemeBoundaries` lists the projection's grapheme boundaries
// (the UI computes them); empty means code point boundaries. Pasted newlines
// become "\N" and NBSP becomes "\h".
std::expected<std::u16string, MapRefusal> mappedReplace(std::u16string_view raw, std::size_t start,
                                                        std::size_t end, std::u16string_view inserted,
                                                        std::span<const std::size_t> graphemeBoundaries = {});

// Raw offset of a projection offset. A boundary between visible text and
// hidden tags maps after the tags (`afterTags`, the insertion default) or
// before them (the end of a selection, so following tags stay outside it).
std::size_t rawOffset(const Projection &projection, std::size_t displayOffset, bool afterTags);
// Projection offset of a raw offset; inside a hidden or protected span it is
// the span's display start.
std::size_t displayOffset(const Projection &projection, std::size_t rawOffset);

std::u16string toUtf16(std::u8string_view text);
std::u8string toUtf8(std::u16string_view text);

} // namespace hikari::core
