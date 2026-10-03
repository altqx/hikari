#include "hikari/core/ass_load.h"

#include "text_util.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

namespace hikari::core {

std::vector<const LineRecord *> Document::lines() const
{
    std::vector<const LineRecord *> out;
    for (const auto &section : m_sections)
        for (const auto &record : section.records)
            if (const auto *line = std::get_if<LineRecord>(&record))
                out.push_back(line);
    return out;
}

std::optional<std::u8string> Document::scriptInfo(std::u8string_view key) const
{
    std::optional<std::u8string> value;
    for (const auto &section : m_sections)
        if (section.kind == SectionKind::ScriptInfo)
            for (const auto &record : section.records)
                if (const auto *p = std::get_if<PropertyRecord>(&record); p && p->key == key)
                    value = p->value; // AddSInfo replaces: the last one wins
    return value;
}

bool Document::editLine(LineId id, const std::function<void(LineRecord &)> &change)
{
    for (auto &section : m_sections)
        for (auto &record : section.records)
            if (auto *line = std::get_if<LineRecord>(&record); line && line->id == id) {
                change(*line);
                line->edited = true;
                return true;
            }
    return false;
}

std::optional<LineId> Document::insertLineAfter(LineId after, LineRecord line)
{
    for (auto &section : m_sections)
        for (std::size_t i = 0; i < section.records.size(); ++i)
            if (auto *prev = std::get_if<LineRecord>(&section.records[i]); prev && prev->id == after) {
                line.id = LineId{m_nextLineId++};
                line.inserted = true;
                line.edited = true;
                line.originalSpan.reset();
                const SourceSpan &at = prev->span;
                line.span = SourceSpan{at.offset + at.length + at.terminatorLength, 0, 0};
                const LineId id = line.id;
                section.records.insert(section.records.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(line));
                return id;
            }
    return std::nullopt;
}

namespace {

void prepareInserted(LineRecord &line, LineId id, std::size_t offset)
{
    line.id = id;
    line.inserted = true;
    line.edited = true;
    line.originalSpan.reset();
    line.span = SourceSpan{offset, 0, 0};
}

} // namespace

std::optional<LineId> Document::insertLineBefore(LineId before, LineRecord line)
{
    for (auto &section : m_sections)
        for (std::size_t i = 0; i < section.records.size(); ++i)
            if (auto *next = std::get_if<LineRecord>(&section.records[i]); next && next->id == before) {
                prepareInserted(line, LineId{m_nextLineId++}, next->span.offset);
                const LineId id = line.id;
                section.records.insert(section.records.begin() + static_cast<std::ptrdiff_t>(i), std::move(line));
                return id;
            }
    return std::nullopt;
}

std::optional<LineId> Document::appendLine(LineRecord line)
{
    for (auto it = m_sections.rbegin(); it != m_sections.rend(); ++it) {
        if (it->kind != SectionKind::Events)
            continue;
        std::size_t offset = it->headerSpan ? it->headerSpan->offset + it->headerSpan->length +
                                                  it->headerSpan->terminatorLength
                                            : 0;
        for (const auto &record : it->records)
            std::visit([&](const auto &r) {
                if constexpr (requires { r.span; })
                    offset = std::max(offset, r.span.offset + r.span.length + r.span.terminatorLength);
            }, record);
        prepareInserted(line, LineId{m_nextLineId++}, offset);
        const LineId id = line.id;
        it->records.push_back(std::move(line));
        return id;
    }
    return std::nullopt;
}

bool Document::removeLine(LineId id)
{
    for (auto &section : m_sections)
        for (std::size_t i = 0; i < section.records.size(); ++i)
            if (auto *line = std::get_if<LineRecord>(&section.records[i]); line && line->id == id) {
                section.records.erase(section.records.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
    return false;
}

bool Document::setLineUnconfirmed(LineId id, bool unconfirmed)
{
    for (auto &section : m_sections)
        for (auto &record : section.records)
            if (auto *line = std::get_if<LineRecord>(&record); line && line->id == id) {
                line->unconfirmed = unconfirmed;
                line->edited = true;
                return true;
            }
    return false;
}

bool Document::setLineText(LineId id, std::u8string text)
{
    for (auto &section : m_sections)
        for (auto &record : section.records)
            if (auto *line = std::get_if<LineRecord>(&record); line && line->id == id) {
                line->text = std::move(text);
                line->edited = true;
                return true;
            }
    return false;
}

namespace {

using namespace detail;

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

// Removes every occurrence of marker; true when there was one (wxString::Replace).
bool replaceAll(std::u8string &text, u8sv marker)
{
    bool found = false;
    for (std::size_t p; (p = text.find(marker)) != std::u8string::npos; found = true)
        text.erase(p, marker.size());
    return found;
}

// Dialogue::SetRaw's Actor handling: one marker kind, legacy order, then trim.
std::u8string actorMarkers(u8sv field, LineRecord &line)
{
    std::u8string actor(field);
    if (!actor.empty() && actor.front() == u8'[') {
        if (replaceAll(actor, u8"[bookmark]")) {
            line.bookmark = true;
        } else if (replaceAll(actor, u8"[hidden]")) {
            line.visibility = LineVisibility::Hidden;
        } else if (replaceAll(actor, u8"[visible]")) {
            line.visibility = LineVisibility::VisibleBlock;
        } else if (replaceAll(actor, u8"[tree_closed]")) {
            line.group = GroupMarker::Closed;
            line.visibility = LineVisibility::Hidden;
        } else if (replaceAll(actor, u8"[tree_opened]")) {
            line.group = GroupMarker::Opened;
        } else if (replaceAll(actor, u8"[tree_description]")) {
            line.group = GroupMarker::Description;
        }
    }
    return str(trim(actor));
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
            m_spans.push_back(span);
            pos = end < b.size() ? end + 1 : end;
        }
        // A TLMode pair consumes the line after its original, so lines are
        // visited by index.
        for (m_next = 0; m_next < m_spans.size();)
            line(m_spans[m_next++]);
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
        // LoadASS checks for events before anything else, in every section
        // but embedded font/graphic data: a Dialogue in Script Info is a Line.
        if ((startsWith(view, u8"Dial") || startsWith(view, u8"Comm")) && section.kind != SectionKind::Fonts &&
            section.kind != SectionKind::Graphics) {
            event(view, span);
            return;
        }
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
        if (kind == SectionKind::Events) {
            // Evaluated at every "[Eve" header, from the Script Info read so far.
            const auto mode = m_result.document.scriptInfo(u8"TLMode");
            const auto style = m_result.document.scriptInfo(u8"TLMode Style");
            m_tlStyle.reset();
            if (mode == u8"Yes" && style && !style->empty())
                m_tlStyle = style;
        }
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
        LineRecord line = parseEvent(view, span);
        if (line.unparsed)
            addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::MalformedEvent, span.offset, view);
        if (m_tlStyle && line.style == *m_tlStyle && pair(line, view))
            return;
        line.id = DocumentBuilder::nextLineId(m_result.document);
        current().records.push_back(std::move(line));
    }

    // Dialogue::SetRaw. LoadASS takes any "Dial"/"Comm" line, but SetRaw reads
    // it as an event only with a "Dialogue"/"Comment" prefix and at least 9
    // comma tokens (wxStringTokenizer RET_EMPTY_ALL). Otherwise it falls
    // through to the plain-text form: the whole line is the text. (Such a
    // line containing " --> " would be read as an SRT cue; not reproduced.)
    LineRecord parseEvent(u8sv view, const SourceSpan &span)
    {
        LineRecord line;
        line.span = span;
        const auto parts = splitAll(view, u8',');
        if (parts.size() < 9 || !(startsWith(view, u8"Dialogue") || startsWith(view, u8"Comment"))) {
            line.unparsed = true;
            line.style = u8"Default";
            for (char8_t c : trimRight(view))
                if (c != u8'\r')
                    line.text += c;
            return line;
        }
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
        line.actor = actorMarkers(parts[4], line);
        line.marginLeft = {str(parts[5]), legacy::atoi(parts[5])};
        line.marginRight = {str(parts[6]), legacy::atoi(parts[6])};
        line.marginVertical = {str(parts[7]), legacy::atoi(parts[7])};
        line.effect = str(trim(parts[8]));
        std::size_t consumed = 0;
        for (std::size_t i = 0; i < 9; ++i)
            consumed += parts[i].size() + 1;
        line.text = consumed <= view.size() ? str(trim(view.substr(consumed))) : std::u8string{};
        return line;
    }

    // SubsLoader::LoadASS in TLMode: the next token, whatever it is, is the
    // translation line. Zero-length lines are not tokens; a CRLF blank line
    // ("\r") is. The translation line is read without left trimming.
    // Returns false when the original stays unpaired (C87-nondialogue-partner).
    bool pair(LineRecord &original, u8sv originalView)
    {
        const auto &b = DocumentBuilder::source(m_result.document).bytes;
        std::size_t j = m_next;
        while (j < m_spans.size() && m_spans[j].length == 0 && m_spans[j].terminatorLength != 2)
            ++j;
        LineRecord merged;
        if (j < m_spans.size() && validUtf8(b.data() + m_spans[j].offset, m_spans[j].length)) {
            const SourceSpan &ps = m_spans[j];
            const u8sv raw(reinterpret_cast<const char8_t *>(b.data() + ps.offset), ps.length);
            // Legacy took a ";..." or lone "{...}" line (SetRaw's NonDialogue)
            // as the translation, then saved the original twice and lost it.
            // Approved C87-nondialogue-partner: it stays a comment, and the
            // original stays an unpaired Line.
            const bool braces = startsWith(raw, u8"{") && raw.ends_with(u8'}') &&
                                std::ranges::count(raw, u8'{') == 1 && std::ranges::count(raw, u8'}') == 1;
            if (startsWith(raw, u8";") || braces) {
                addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::MalformedPair, original.span.offset,
                              original.text);
                return false;
            }
            merged = parseEvent(raw, ps);
            merged.span = SourceSpan{original.span.offset, ps.offset + ps.length - original.span.offset,
                                     ps.terminatorLength};
            m_next = j + 1;
        } else {
            // No translation line: legacy pairs with an empty plain-text line.
            // (An undecodable one is left in place here.)
            merged.unparsed = true;
            merged.style = u8"Default";
            merged.span = original.span;
        }
        if (merged.unparsed)
            addDiagnostic(Diagnostic::Severity::Warning, Diagnostic::Kind::MalformedPair, original.span.offset,
                          original.text);
        merged.id = DocumentBuilder::nextLineId(m_result.document);
        merged.originalSpan = original.span;
        merged.translation = std::move(merged.text);
        merged.text = std::move(original.text);
        // Legacy compared the trimmed Effect with form-feed+D, and the trim
        // removed the form feed, so Unconfirmed was never read back. Approved
        // C87-unconfirmed-roundtrip: the authored field itself is compared.
        const auto fields = splitAll(originalView, u8',');
        merged.unconfirmed = fields.size() > 8 && fields[8] == u8"\fD";
        current().records.push_back(std::move(merged));
        return true;
    }

    LoadResult m_result;
    std::set<SectionKind> m_seenKinds;
    std::vector<SourceSpan> m_spans;
    std::size_t m_next = 0;
    std::optional<std::u8string> m_tlStyle; // set while TLMode pairing is on
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
