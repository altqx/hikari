#pragma once

// Internal helpers shared by the core format loaders: legacy-compatible text
// trimming and splitting, wxString::SubString arithmetic, UTF-8 validation,
// and builder access to a Document's private state.

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

// Builds a Document's private state for the loader.
struct DocumentBuilder {
    static SourceText &source(Document &d) { return d.m_source; }
    static std::vector<Section> &sections(Document &d) { return d.m_sections; }
    static LineId nextLineId(Document &d) { return LineId{d.m_nextLineId++}; }
    static void setFormat(Document &d, SubtitleFormat f) { d.m_format = f; }
    static std::uint64_t peekNextLineId(const Document &d) { return d.m_nextLineId; }
    static void setNextLineId(Document &d, std::uint64_t next) { d.m_nextLineId = next; }
};


namespace detail {

// The newline style around an encoder's position, for Lines inserted in the
// editor (they have no source bytes of their own).
struct NewlineTracker {
    std::u8string newline = u8"\n";
    bool openEnd = false; // the last record ended the file without a newline

    void see(const SourceSpan &span)
    {
        if (span.terminatorLength == 2)
            newline = u8"\r\n";
        else if (span.terminatorLength == 1)
            newline = u8"\n";
        if (span.length + span.terminatorLength > 0)
            openEnd = span.terminatorLength == 0;
    }
    // An inserted record: newline-terminated, or newline-led at an open end.
    std::u8string wrap(std::u8string_view body) const
    {
        return openEnd ? newline + std::u8string(body) : std::u8string(body) + newline;
    }
};

using u8sv = std::u8string_view;

inline bool isSpace(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\n' || c == u8'\f' || c == u8'\v';
}

inline u8sv trimLeft(u8sv s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    return s;
}

inline u8sv trimRight(u8sv s)
{
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

inline u8sv trim(u8sv s)
{
    return trimRight(trimLeft(s));
}

inline bool startsWith(u8sv s, u8sv prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

// wxString::SubString(from, to) == Mid(from, to - from + 1), with the legacy
// code's size_t arithmetic (positions derived from npos wrap around).
inline u8sv wxSubString(u8sv s, std::size_t from, std::size_t to)
{
    if (from > s.size())
        return {};
    const std::size_t count = to - from + 1;
    return s.substr(from, count);
}

inline bool validUtf8(const std::byte *p, std::size_t n)
{
    std::size_t i = 0;
    while (i < n) {
        const auto c = static_cast<unsigned char>(p[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + len > n)
            return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(p[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}


} // namespace detail
} // namespace hikari::core
