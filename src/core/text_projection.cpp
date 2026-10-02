#include "hikari/core/text_projection.h"

#include <algorithm>
#include <optional>

namespace hikari::core {

namespace {

bool isHighSurrogate(char16_t c)
{
    return c >= 0xD800 && c <= 0xDBFF;
}
bool isLowSurrogate(char16_t c)
{
    return c >= 0xDC00 && c <= 0xDFFF;
}

// The drawing state after an override block: only a top-level literal \pN
// counts (one inside \t(...) is not interpreted).
bool drawingAfter(std::u16string_view tag, bool drawing)
{
    int depth = 0;
    for (std::size_t i = 0; i < tag.size(); ++i) {
        if (tag[i] == u'(') {
            ++depth;
        } else if (tag[i] == u')') {
            depth = std::max(0, depth - 1);
        } else if (tag[i] == u'\\' && depth == 0 && i + 1 < tag.size() && tag[i + 1] == u'p') {
            std::size_t j = i + 2;
            long value = 0;
            bool digits = false;
            while (j < tag.size() && tag[j] >= u'0' && tag[j] <= u'9') {
                value = value * 10 + (tag[j] - u'0');
                digits = true;
                ++j;
            }
            if (digits)
                drawing = value != 0;
        }
    }
    return drawing;
}

} // namespace

Projection project(std::u16string_view raw)
{
    Projection p;
    std::size_t i = 0;
    bool drawing = false;
    while (i < raw.size()) {
        const std::size_t begin = i;
        SpanKind kind = SpanKind::Text;
        if (raw[i] == u'{') {
            const auto close = raw.find(u'}', i + 1);
            if (close == std::u16string_view::npos) {
                i = raw.size();
                kind = SpanKind::Malformed;
            } else if (raw.substr(i + 1, close - i - 1).find(u'{') != std::u16string_view::npos) {
                i = close + 1;
                kind = SpanKind::Malformed;
            } else {
                i = close + 1;
                kind = SpanKind::HiddenOverride;
                drawing = drawingAfter(raw.substr(begin, i - begin), drawing);
            }
        } else if (drawing) {
            const auto open = raw.find(u'{', i);
            i = open == std::u16string_view::npos ? raw.size() : open;
            kind = SpanKind::DrawingPayload;
        } else if (raw[i] == u'}') {
            ++i;
            kind = SpanKind::Malformed;
        } else if (raw[i] == u'\\' && i + 1 < raw.size() && (raw[i + 1] == u'N' || raw[i + 1] == u'n' ||
                                                              raw[i + 1] == u'h')) {
            kind = raw[i + 1] == u'N' ? SpanKind::HardBreak : raw[i + 1] == u'n' ? SpanKind::SoftBreak
                                                                                 : SpanKind::HardSpace;
            i += 2;
        } else {
            i += isHighSurrogate(raw[i]) && i + 1 < raw.size() && isLowSurrogate(raw[i + 1]) ? 2 : 1;
        }
        std::u16string value;
        switch (kind) {
        case SpanKind::Text: value = std::u16string(raw.substr(begin, i - begin)); break;
        case SpanKind::HiddenOverride: break;
        case SpanKind::HardBreak: value = u"\n"; break;
        case SpanKind::SoftBreak: value = u" "; break;
        case SpanKind::HardSpace: value = u" "; break;
        case SpanKind::DrawingPayload:
        case SpanKind::Malformed: value = std::u16string(1, kProtectedMarker); break;
        }
        const std::size_t display = p.text.size();
        p.text += value;
        p.spans.push_back({begin, i, display, p.text.size(), kind});
    }
    return p;
}

namespace {

// Source of every hidden or protected span, in order: what an edit must keep.
std::vector<std::u16string> keptTokens(std::u16string_view raw, const Projection &p)
{
    std::vector<std::u16string> out;
    for (const auto &s : p.spans)
        if (s.kind == SpanKind::HiddenOverride || s.isProtected())
            out.emplace_back(raw.substr(s.rawStart, s.rawEnd - s.rawStart));
    return out;
}

} // namespace

std::expected<std::u16string, MapRefusal> mappedReplace(std::u16string_view raw, std::size_t start,
                                                        std::size_t end, std::u16string_view insertedText,
                                                        std::span<const std::size_t> graphemeBoundaries)
{
    const Projection p = project(raw);
    if (start > end)
        std::swap(start, end);
    if (end > p.text.size())
        return std::unexpected(MapRefusal::SplitsCharacter);
    auto isBoundary = [&](std::size_t at) {
        if (at == 0 || at == p.text.size())
            return true;
        if (!graphemeBoundaries.empty())
            return std::ranges::find(graphemeBoundaries, at) != graphemeBoundaries.end();
        return !isLowSurrogate(p.text[at]);
    };
    if (!isBoundary(start) || !isBoundary(end))
        return std::unexpected(MapRefusal::SplitsCharacter);

    std::u16string inserted;
    for (std::size_t k = 0; k < insertedText.size(); ++k) {
        const char16_t c = insertedText[k];
        if (c == u'\r') {
            inserted += u'\n';
            if (k + 1 < insertedText.size() && insertedText[k + 1] == u'\n')
                ++k;
        } else {
            inserted += c;
        }
    }
    for (char16_t c : inserted)
        if (c == u'{' || c == u'}' || c == u'\\' || c == kProtectedMarker || c == u' ' || c == u' ')
            return std::unexpected(MapRefusal::AssSyntax);

    std::vector<const ProjectionSpan *> selected;
    for (const auto &s : p.spans)
        if (s.displayEnd > s.displayStart && s.displayStart < end && s.displayEnd > start)
            selected.push_back(&s);
    for (const auto *s : selected) {
        if (s->isProtected())
            return std::unexpected(MapRefusal::Protected);
        if (s->displayStart < start || s->displayEnd > end)
            return std::unexpected(MapRefusal::SplitsToken);
    }

    // Insertion point: after every hidden span that touches the boundary.
    std::size_t where = 0;
    bool found = p.spans.empty() && start == 0;
    for (const auto &s : p.spans) {
        if (s.displayStart == start) {
            where = found ? std::max(where, s.rawStart) : s.rawStart;
            found = true;
        }
        if (s.displayEnd == start) {
            where = found ? std::max(where, s.rawEnd) : s.rawEnd;
            found = true;
        }
    }
    if (!found)
        return std::unexpected(MapRefusal::SplitsToken);

    std::u16string encoded;
    for (char16_t c : inserted) {
        if (c == u'\n')
            encoded += u"\\N";
        else if (c == u' ')
            encoded += u"\\h";
        else
            encoded += c;
    }

    // Remove only the selected visible source spans, then insert.
    std::u16string result(raw);
    std::size_t removedBefore = 0;
    for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
        const auto *s = *it;
        result.erase(s->rawStart, s->rawEnd - s->rawStart);
        if (s->rawEnd <= where)
            removedBefore += s->rawEnd - s->rawStart;
    }
    result.insert(where - removedBefore, encoded);

    const Projection after = project(result);
    const std::u16string desired = p.text.substr(0, start) + inserted + p.text.substr(end);
    if (after.text != desired || keptTokens(raw, p) != keptTokens(result, after))
        return std::unexpected(MapRefusal::Reinterpreted);
    return result;
}

std::size_t rawOffset(const Projection &p, std::size_t display, bool afterTags)
{
    std::optional<std::size_t> best;
    for (const auto &s : p.spans) {
        if (s.displayStart == display)
            best = best ? (afterTags ? std::max(*best, s.rawStart) : std::min(*best, s.rawStart)) : s.rawStart;
        if (s.displayEnd == display)
            best = best ? (afterTags ? std::max(*best, s.rawEnd) : std::min(*best, s.rawEnd)) : s.rawEnd;
        if (s.displayStart < display && display < s.displayEnd)
            return s.rawStart + (display - s.displayStart); // inside a text run
    }
    return best.value_or(0);
}

std::size_t displayOffset(const Projection &p, std::size_t raw)
{
    for (const auto &s : p.spans) {
        if (raw < s.rawEnd || (raw == s.rawEnd && s.rawEnd == s.rawStart)) {
            if (s.kind == SpanKind::Text && raw >= s.rawStart)
                return s.displayStart + (raw - s.rawStart);
            return s.displayStart; // inside a hidden or protected span
        }
    }
    return p.text.size();
}

std::u16string toUtf16(std::u8string_view s)
{
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        std::size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (std::size_t k = 1; k < n && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

std::u8string toUtf8(std::u16string_view s)
{
    std::u8string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char32_t cp = s[i];
        if (isHighSurrogate(s[i]) && i + 1 < s.size() && isLowSurrogate(s[i + 1])) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out += static_cast<char8_t>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char8_t>(0xC0 | (cp >> 6));
            out += static_cast<char8_t>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char8_t>(0xE0 | (cp >> 12));
            out += static_cast<char8_t>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char8_t>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char8_t>(0xF0 | (cp >> 18));
            out += static_cast<char8_t>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char8_t>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char8_t>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace hikari::core
