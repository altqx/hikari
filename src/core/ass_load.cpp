#include "hikari/core/ass_load.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

namespace hikari::core {

// Builds a Document's private state for the loader.
struct DocumentBuilder {
    static SourceText &source(Document &d) { return d.m_source; }
    static std::vector<Section> &sections(Document &d) { return d.m_sections; }
    static LineId nextLineId(Document &d) { return LineId{d.m_nextLineId++}; }
};

std::vector<const LineRecord *> Document::lines() const
{
    std::vector<const LineRecord *> out;
    for (const auto &section : m_sections)
        for (const auto &record : section.records)
            if (const auto *line = std::get_if<LineRecord>(&record))
                out.push_back(line);
    return out;
}

namespace {

using u8sv = std::u8string_view;

bool isSpace(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\n' || c == u8'\f' || c == u8'\v';
}

u8sv trimLeft(u8sv s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    return s;
}

u8sv trimRight(u8sv s)
{
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

u8sv trim(u8sv s)
{
    return trimRight(trimLeft(s));
}

bool startsWith(u8sv s, u8sv prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

// wxString::SubString(from, to) == Mid(from, to - from + 1), with the legacy
// code's size_t arithmetic (positions derived from npos wrap around).
u8sv wxSubString(u8sv s, std::size_t from, std::size_t to)
{
    if (from > s.size())
        return {};
    const std::size_t count = to - from + 1;
    return s.substr(from, count);
}

bool validUtf8(const std::byte *p, std::size_t n)
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

SectionKind headerKind(u8sv header)
{
    // Prefix rules follow the legacy loader where it has them.
    if (startsWith(header, u8"[Script Info]"))
        return SectionKind::ScriptInfo;
    if (startsWith(header, u8"[V4+"))
        return SectionKind::Styles;
    if (startsWith(header, u8"[V4"))
        return SectionKind::SsaStyles;
    if (startsWith(header, u8"[Eve"))
        return SectionKind::Events;
    if (startsWith(header, u8"[Fonts]"))
        return SectionKind::Fonts;
    if (startsWith(header, u8"[Graphics]"))
        return SectionKind::Graphics;
    return SectionKind::Unknown;
}

std::vector<u8sv> splitAll(u8sv s, char8_t sep)
{
    std::vector<u8sv> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::u8string str(u8sv s)
{
    return std::u8string(s);
}

class Loader {
public:
    explicit Loader(std::span<const std::byte> bytes)
    {
        DocumentBuilder::source(m_result.document).bytes.assign(bytes.begin(), bytes.end());
    }

    LoadResult run()
    {
        auto &src = DocumentBuilder::source(m_result.document);
        const auto &b = src.bytes;
        std::size_t pos = 0;
        if (b.size() >= 3 && b[0] == std::byte{0xEF} && b[1] == std::byte{0xBB} && b[2] == std::byte{0xBF}) {
            src.encoding = TextEncoding::Utf8WithBom;
            pos = 3;
        } else if (b.size() >= 2 && ((b[0] == std::byte{0xFF} && b[1] == std::byte{0xFE}) ||
                                     (b[0] == std::byte{0xFE} && b[1] == std::byte{0xFF}))) {
            // UTF-16 belongs to the encoding work; keep everything as bytes.
            addDiagnostic(Diagnostic::Severity::Error, Diagnostic::Kind::UnsupportedEncoding, 0, {});
            current().records.push_back(OpaqueRecord{SourceSpan{0, b.size(), 0}});
            return std::move(m_result);
        }
        while (pos < b.size()) {
            std::size_t end = pos;
            while (end < b.size() && b[end] != std::byte{'\n'})
                ++end;
            SourceSpan span{pos, end - pos, end < b.size() ? 1u : 0u};
            if (span.terminatorLength == 1 && span.length > 0 && b[end - 1] == std::byte{'\r'}) {
                span.length -= 1;
                span.terminatorLength = 2;
            }
            line(span);
            pos = end < b.size() ? end + 1 : end;
        }
        return std::move(m_result);
    }

private:
    Section &current()
    {
        auto &sections = DocumentBuilder::sections(m_result.document);
        if (sections.empty())
            sections.push_back(Section{});
        return sections.back();
    }

    void addDiagnostic(Diagnostic::Severity severity, Diagnostic::Kind kind, std::size_t offset, u8sv excerpt)
    {
        m_result.diagnostics.push_back({severity, kind, offset, str(excerpt.substr(0, 80))});
    }

    void line(const SourceSpan &span)
    {
        const auto &b = DocumentBuilder::source(m_result.document).bytes;
        const std::byte *p = b.data() + span.offset;
        if (!validUtf8(p, span.length)) {
            addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::InvalidUtf8, span.offset, {});
            current().records.push_back(OpaqueRecord{span});
            return;
        }
        const u8sv raw(reinterpret_cast<const char8_t *>(p), span.length);
        const u8sv view = trimLeft(raw);
        if (!view.empty() && view.front() == u8'[') {
            header(trimRight(view), span);
            return;
        }
        Section &section = current();
        switch (section.kind) {
        case SectionKind::ScriptInfo:
            if (!view.empty() && view.front() != u8';' && view.find(u8':') != u8sv::npos &&
                !startsWith(view, u8"Format")) {
                const auto colon = view.find(u8':');
                section.records.push_back(PropertyRecord{str(trim(view.substr(0, colon))),
                                                         str(trim(view.substr(colon + 1))), span});
                return;
            }
            break;
        case SectionKind::Styles:
        case SectionKind::SsaStyles:
            if (startsWith(view, u8"Format:")) {
                section.records.push_back(FormatRecord{fields(view.substr(7)), span});
                return;
            }
            if (startsWith(view, u8"Style:")) {
                auto parts = fields(view.substr(6));
                StyleRecord style{parts.empty() ? std::u8string{} : str(trim(parts.front())), std::move(parts), span};
                section.records.push_back(std::move(style));
                return;
            }
            break;
        case SectionKind::Events:
            if (startsWith(view, u8"Format:")) {
                section.records.push_back(FormatRecord{fields(view.substr(7)), span});
                return;
            }
            if (startsWith(view, u8"Dial") || startsWith(view, u8"Comm")) {
                event(view, span);
                return;
            }
            break;
        default:
            break;
        }
        section.records.push_back(OpaqueRecord{span});
    }

    static std::vector<std::u8string> fields(u8sv rest)
    {
        std::vector<std::u8string> out;
        for (auto part : splitAll(rest, u8','))
            out.push_back(str(part));
        return out;
    }

    void header(u8sv text, const SourceSpan &span)
    {
        const SectionKind kind = headerKind(text);
        if (kind == SectionKind::Unknown) {
            addDiagnostic(Diagnostic::Severity::Info, Diagnostic::Kind::UnknownSection, span.offset, text);
        } else if (!m_seenKinds.insert(kind).second) {
            addDiagnostic(Diagnostic::Severity::Info, Diagnostic::Kind::RepeatedSection, span.offset, text);
        }
        if (kind == SectionKind::SsaStyles)
            addDiagnostic(Diagnostic::Severity::Info, Diagnostic::Kind::SsaStyles, span.offset, text);
        DocumentBuilder::sections(m_result.document).push_back(Section{kind, str(text), span, {}});
    }

    TimeField time(u8sv lexeme, std::size_t offset)
    {
        if (!legacy::isCanonicalAssTime(lexeme))
            addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::MalformedTime, offset, lexeme);
        const std::int64_t ms = legacy::assTimeMilliseconds(lexeme);
        return TimeField{str(lexeme), DocumentTime(ms * 1000)};
    }

    void event(u8sv view, const SourceSpan &span)
    {
        // Legacy: wxStringTokenizer(",", RET_EMPTY_ALL) needs at least 9 tokens;
        // the text is everything after the 9th token's comma.
        const auto parts = splitAll(view, u8',');
        if (parts.size() < 9) {
            addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::MalformedEvent, span.offset, view);
            current().records.push_back(OpaqueRecord{span});
            return;
        }
        LineRecord line;
        line.id = DocumentBuilder::nextLineId(m_result.document);
        line.span = span;
        const u8sv first = parts[0];
        line.comment = !startsWith(first, u8"Dialogue");
        u8sv layerLexeme;
        if (first.find(u8"arked=") == u8sv::npos) {
            const auto space = first.find(u8' ');
            layerLexeme = space == u8sv::npos ? u8sv{} : first.substr(space + 1);
        } else {
            layerLexeme = first.substr(first.rfind(u8'=') + 1);
        }
        line.layer = {str(layerLexeme), legacy::atoi(layerLexeme)};
        line.start = time(parts[1], span.offset);
        line.end = time(parts[2], span.offset);
        line.style = str(parts[3]);
        line.actor = str(trim(parts[4]));
        line.marginLeft = {str(parts[5]), legacy::atoi(parts[5])};
        line.marginRight = {str(parts[6]), legacy::atoi(parts[6])};
        line.marginVertical = {str(parts[7]), legacy::atoi(parts[7])};
        line.effect = str(trim(parts[8]));
        std::size_t consumed = 0;
        for (std::size_t i = 0; i < 9; ++i)
            consumed += parts[i].size() + 1;
        line.text = consumed <= view.size() ? str(trim(view.substr(consumed))) : std::u8string{};
        current().records.push_back(std::move(line));
    }

    LoadResult m_result;
    std::set<SectionKind> m_seenKinds;
};

} // namespace

namespace legacy {

std::int64_t atoi(std::u8string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && isSpace(text[i]))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == u8'+' || text[i] == u8'-'))
        negative = text[i++] == u8'-';
    // strtol saturates at the long range; wxAtoi then truncates to int.
    constexpr std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::uint64_t magnitude = 0;
    bool saturated = false;
    for (; i < text.size() && text[i] >= u8'0' && text[i] <= u8'9'; ++i) {
        const std::uint64_t digit = static_cast<std::uint64_t>(text[i] - u8'0');
        if (magnitude > (limit + (negative ? 1 : 0) - digit) / 10)
            saturated = true;
        else
            magnitude = magnitude * 10 + digit;
    }
    std::int64_t asLong;
    if (saturated)
        asLong = negative ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
    else if (negative)
        asLong = magnitude == limit + 1 ? std::numeric_limits<std::int64_t>::min()
                                        : -static_cast<std::int64_t>(magnitude);
    else
        asLong = static_cast<std::int64_t>(magnitude);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(asLong)));
}

std::int64_t assTimeMilliseconds(std::u8string_view lexeme)
{
    // SubsTime::ParseMS for ASS: raw.Trim() (right) then fixed offsets from the
    // first ':'; each field through wxAtoi; centiseconds * 10.
    const u8sv raw = trimRight(lexeme);
    if (raw.empty())
        return 0;
    const std::size_t colon = raw.find(u8':'); // npos wraps exactly as in the legacy code
    const std::int64_t hours = atoi(wxSubString(raw, 0, colon - 1));
    const std::int64_t minutes = atoi(wxSubString(raw, colon + 1, colon + 2));
    const std::int64_t seconds = atoi(wxSubString(raw, colon + 4, colon + 5));
    const std::int64_t centis = atoi(wxSubString(raw, colon + 7, colon + 8));
    return hours * 3'600'000 + minutes * 60'000 + seconds * 1'000 + centis * 10;
}

bool isCanonicalAssTime(std::u8string_view lexeme)
{
    const u8sv t = trim(lexeme);
    const auto colon = t.find(u8':');
    if (colon == u8sv::npos || colon == 0 || t.size() != colon + 9)
        return false;
    auto digits = [&](std::size_t from, std::size_t count) {
        for (std::size_t k = from; k < from + count; ++k)
            if (t[k] < u8'0' || t[k] > u8'9')
                return false;
        return true;
    };
    return digits(0, colon) && digits(colon + 1, 2) && t[colon + 3] == u8':' && digits(colon + 4, 2) &&
           t[colon + 6] == u8'.' && digits(colon + 7, 2);
}

} // namespace legacy

LoadResult loadAss(std::span<const std::byte> bytes)
{
    return Loader(bytes).run();
}

} // namespace hikari::core
