#include "hikari/core/line_formats.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/srt.h"

#include "text_util.h"

#include <cstdio>
#include <optional>

namespace hikari::core {

using namespace detail;

namespace {

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// [0-9-]+ (min 1) or [0-9-]* (min 0) starting at pos; returns end position.
std::optional<std::size_t> digitsOrDash(u8sv s, std::size_t pos, std::size_t min)
{
    std::size_t e = pos;
    while (e < s.size() && (isDigit(s[e]) || s[e] == u8'-'))
        ++e;
    if (e - pos < min)
        return std::nullopt;
    return e;
}

struct Timed {
    u8sv start, end, text;
};

// ^\{([0-9-]+)\}{([0-9-]*)\}([^\r\n]*) and its [..][..] MPL2 twin.
std::optional<Timed> matchBracketed(u8sv line, char8_t open, char8_t close)
{
    if (line.empty() || line[0] != open)
        return std::nullopt;
    const auto e1 = digitsOrDash(line, 1, 1);
    if (!e1 || *e1 >= line.size() || line[*e1] != close || *e1 + 1 >= line.size() || line[*e1 + 1] != open)
        return std::nullopt;
    const auto e2 = digitsOrDash(line, *e1 + 2, 0);
    if (!e2 || *e2 >= line.size() || line[*e2] != close)
        return std::nullopt;
    return Timed{line.substr(1, *e1 - 1), line.substr(*e1 + 2, *e2 - *e1 - 2), line.substr(*e2 + 1)};
}

// ^([0-9]+)[:;]([0-9]+)[:;]([0-9]+)[:;, ]([^\r\n]*)
struct TmpMatch {
    std::u8string startLexeme; // groups joined with ':' as the legacy code does
    u8sv text;
};
std::optional<TmpMatch> matchTmp(u8sv line)
{
    std::size_t p = 0;
    u8sv groups[3];
    for (int g = 0; g < 3; ++g) {
        const std::size_t from = p;
        while (p < line.size() && isDigit(line[p]))
            ++p;
        if (p == from || p >= line.size())
            return std::nullopt;
        groups[g] = line.substr(from, p - from);
        const char8_t sep = line[p];
        const bool ok = g < 2 ? (sep == u8':' || sep == u8';') : (sep == u8':' || sep == u8';' || sep == u8',' || sep == u8' ');
        if (!ok)
            return std::nullopt;
        ++p;
    }
    TmpMatch m;
    m.startLexeme = std::u8string(groups[0]) + u8':' + std::u8string(groups[1]) + u8':' + std::u8string(groups[2]);
    m.text = line.substr(p);
    return m;
}

// SubsTime::ParseMS for TMPlayer: positional h:mm:ss, no fraction.
std::int64_t tmpMilliseconds(u8sv raw)
{
    raw = trimRight(raw);
    if (raw.empty())
        return 0;
    const std::size_t colon = raw.find(u8':');
    return legacy::atoi(wxSubString(raw, 0, colon - 1)) * 3'600'000 +
           legacy::atoi(wxSubString(raw, colon + 1, colon + 2)) * 60'000 +
           legacy::atoi(wxSubString(raw, colon + 4, colon + 5)) * 1'000;
}

bool isAssEvent(u8sv line)
{
    if (!startsWith(line, u8"Dialogue") && !startsWith(line, u8"Comment"))
        return false;
    std::size_t commas = 0;
    for (char8_t c : line)
        commas += c == u8',';
    return commas >= 8;
}

bool isNonDialogue(u8sv line)
{
    if (startsWith(line, u8";"))
        return true;
    if (line.empty() || line.front() != u8'{' || line.back() != u8'}')
        return false;
    std::size_t open = 0, close = 0;
    for (char8_t c : line) {
        open += c == u8'{';
        close += c == u8'}';
    }
    return open == 1 && close == 1;
}

std::u8string number(std::int64_t v)
{
    const std::string s = std::to_string(v);
    return std::u8string(s.begin(), s.end());
}

} // namespace

namespace legacy {

std::u8string tmpTimeText(std::int64_t ms)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", static_cast<long long>(ms / 3600000),
                  static_cast<long long>(ms / 60000 % 60), static_cast<long long>(ms / 1000 % 60));
    return std::u8string(reinterpret_cast<const char8_t *>(buf));
}

} // namespace legacy

void Document::setFrameRate(FrameRate rate)
{
    m_frameRate = rate;
    for (auto &section : m_sections)
        for (auto &record : section.records)
            if (auto *line = std::get_if<LineRecord>(&record)) {
                auto resolve = [&](std::optional<std::int64_t> frame, TimeField &field) {
                    if (!frame)
                        return;
                    const auto seconds = rate.secondsAt(VideoFrameIndex(*frame));
                    if (!seconds)
                        return;
                    const auto us = mulDiv(seconds->numerator(), 1'000'000, seconds->denominator(), 1,
                                           Rounding::NearestTiesAway);
                    if (us)
                        field.value = DocumentTime(*us);
                };
                resolve(line->startFrame, line->start);
                resolve(line->endFrame, line->end);
            }
}

LoadResult loadLineFormats(std::span<const std::byte> bytes)
{
    LoadResult result;
    Document &doc = result.document;
    auto &src = DocumentBuilder::source(doc);
    src.bytes.assign(bytes.begin(), bytes.end());
    const auto &b = src.bytes;
    std::size_t pos = 0;
    if (b.size() >= 3 && b[0] == std::byte{0xEF} && b[1] == std::byte{0xBB} && b[2] == std::byte{0xBF}) {
        src.encoding = TextEncoding::Utf8WithBom;
        pos = 3;
    }
    auto &sections = DocumentBuilder::sections(doc);
    sections.push_back(Section{SectionKind::Cues, {}, std::nullopt, {}});
    auto &records = sections.back().records;
    std::optional<SubtitleFormat> detected;

    while (pos < b.size()) {
        std::size_t end = pos;
        while (end < b.size() && b[end] != std::byte{'\n'})
            ++end;
        SourceSpan span{pos, end - pos, end < b.size() ? 1u : 0u};
        if (span.terminatorLength == 1 && span.length > 0 && b[end - 1] == std::byte{'\r'}) {
            span.length -= 1;
            span.terminatorLength = 2;
        }
        pos = end < b.size() ? end + 1 : end;
        if (!validUtf8(b.data() + span.offset, span.length)) {
            result.diagnostics.push_back({Diagnostic::Severity::Warning, Diagnostic::Kind::InvalidUtf8, span.offset, {}});
            records.push_back(OpaqueRecord{span});
            continue;
        }
        const u8sv raw(reinterpret_cast<const char8_t *>(b.data() + span.offset), span.length);
        // Tokens are right-trimmed; zero-length lines are not tokens at all.
        const u8sv token = trimRight(raw);
        if (span.length == 0 && span.terminatorLength != 2) {
            records.push_back(OpaqueRecord{span});
            continue;
        }
        if (isAssEvent(token) || token.find(u8" --> ") != u8sv::npos) {
            if (!detected)
                detected = isAssEvent(token) ? SubtitleFormat::Ass : SubtitleFormat::Srt;
            records.push_back(OpaqueRecord{span});
            continue;
        }
        LineRecord line;
        line.style = u8"Default";
        line.span = span;
        SubtitleFormat format = SubtitleFormat::PlainText;
        if (auto m = matchBracketed(token, u8'{', u8'}')) {
            format = SubtitleFormat::MicroDvd;
            const std::int64_t s = legacy::atoi(m->start), e = legacy::atoi(m->end);
            line.start.lexeme = std::u8string(m->start);
            line.end.lexeme = std::u8string(m->end);
            line.startFrame = s < 0 ? 0 : s; // legacy clamps a negative frame to 0
            line.endFrame = e < 0 ? 0 : e;
            line.text = std::u8string(trimLeft(m->text));
        } else if (auto m2 = matchBracketed(token, u8'[', u8']')) {
            format = SubtitleFormat::Mpl2;
            line.start = TimeField{std::u8string(m2->start), DocumentTime(legacy::atoi(m2->start) * 100'000)};
            line.end = TimeField{std::u8string(m2->end), DocumentTime(legacy::atoi(m2->end) * 100'000)};
            line.text = std::u8string(trimLeft(m2->text));
        } else if (auto t = matchTmp(token)) {
            format = SubtitleFormat::TMPlayer;
            line.start = TimeField{t->startLexeme, DocumentTime(tmpMilliseconds(t->startLexeme) * 1000)};
            line.text = std::u8string(trimLeft(t->text));
        } else if (isNonDialogue(token)) {
            records.push_back(OpaqueRecord{span}); // legacy hidden comment line
            continue;
        } else {
            line.text = std::u8string(token); // plain text: untimed
        }
        if (!detected && format != SubtitleFormat::PlainText)
            detected = format;
        line.id = DocumentBuilder::nextLineId(doc);
        records.push_back(std::move(line));
    }

    // SetSubsFormat: the first timed Line decides; ASS and SRT reload.
    if (detected == SubtitleFormat::Ass)
        return loadAss(bytes);
    if (detected == SubtitleFormat::Srt)
        return loadSrt(bytes);
    DocumentBuilder::setFormat(doc, detected.value_or(SubtitleFormat::PlainText));
    return result;
}

std::vector<std::byte> encodeLineFormat(const Document &document)
{
    const auto &src = document.source().bytes;
    std::vector<std::byte> out;
    auto copy = [&](std::size_t from, std::size_t count) {
        out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(from),
                   src.begin() + static_cast<std::ptrdiff_t>(from + count));
    };
    auto append = [&](std::u8string_view text) {
        for (char8_t c : text)
            out.push_back(static_cast<std::byte>(c));
    };
    if (document.source().encoding == TextEncoding::Utf8WithBom)
        copy(0, 3);
    for (const auto &section : document.sections())
        for (const auto &record : section.records) {
            const SourceSpan &span = std::visit([](const auto &r) -> const SourceSpan & { return r.span; }, record);
            const auto *line = std::get_if<LineRecord>(&record);
            if (!line || !line->edited) {
                copy(span.offset, span.length + span.terminatorLength);
                continue;
            }
            const std::int64_t startMs = line->start.value.microseconds() / 1000;
            const std::int64_t endMs = line->end.value.microseconds() / 1000;
            switch (document.format()) {
            case SubtitleFormat::MicroDvd:
                append(u8"{" + number(line->startFrame.value_or(0)) + u8"}{" + number(line->endFrame.value_or(0)) +
                       u8"}" + line->text);
                break;
            case SubtitleFormat::Mpl2:
                append(u8"[" + number(startMs / 100) + u8"][" + number(endMs / 100) + u8"]" + line->text);
                break;
            case SubtitleFormat::TMPlayer:
                append(legacy::tmpTimeText(startMs) + u8":" + line->text);
                break;
            default:
                append(line->text);
                break;
            }
            copy(span.offset + span.length, span.terminatorLength);
        }
    return out;
}

} // namespace hikari::core
