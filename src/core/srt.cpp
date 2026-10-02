#include "hikari/core/srt.h"
#include "hikari/core/ass_load.h"

#include "text_util.h"

#include <cstdio>
#include <optional>

namespace hikari::core {

using namespace detail;

namespace {

struct PhysicalLine {
    SourceSpan span;
    bool valid = true; // UTF-8
    u8sv text;         // content without terminator; empty when invalid
};

// wxStringTokenizer(text, "\n", wxTOKEN_STRTOK) never yields an empty token,
// so only zero-length lines vanish. A CRLF blank line is the token "\r" and
// survives; Trim() then empties it.
bool isToken(const PhysicalLine &line)
{
    return line.span.length + (line.span.terminatorLength == 2 ? 1 : 0) > 0;
}

// SubsLoader::TrimLastNumber on the accumulated cue text: drops the trailing
// "\r\n<digits>\r\n" (the next cue's number) or, failing that, the final CRLF.
void trimLastNumber(std::u8string &text)
{
    if (text.size() < 3) {
        text.clear();
        return;
    }
    const std::u8string_view digits = u8"0123456789\r\n";
    for (std::size_t i = text.size() - 2; i > 0; --i) {
        const char8_t ch = text[i - 1];
        const auto pos = digits.find(ch);
        if (pos == std::u8string_view::npos) {
            text.erase(i);
            break;
        }
        if (pos == 10) { // '\r'
            text.erase(i - 1);
            break;
        }
        if (i == 1)
            text.clear();
    }
}

struct LegacyCue {
    std::u8string startLexeme, endLexeme, text;
};

// Dialogue::SetRaw for an SRT cue built from "timing\r\ntext...".
LegacyCue parseCue(const std::u8string &raw)
{
    LegacyCue cue;
    const auto firstSpace = raw.find(u8' ');
    cue.startLexeme = raw.substr(0, firstSpace);
    std::u8string rest = firstSpace == std::u8string::npos ? std::u8string{} : raw.substr(firstSpace + 1);
    const auto secondSpace = rest.find(u8' ');
    rest = secondSpace == std::u8string::npos ? std::u8string{} : rest.substr(secondSpace + 1);
    const auto newline = rest.find(u8'\n');
    cue.endLexeme = std::u8string(trimRight(u8sv(rest).substr(0, newline)));
    std::u8string text = newline == std::u8string::npos ? std::u8string{} : rest.substr(newline + 1);
    std::u8string out;
    for (char8_t c : text) {
        if (c == u8'\r')
            continue;
        if (c == u8'\n')
            out += u8"\\N";
        else
            out += c;
    }
    cue.text = std::move(out);
    return cue;
}

} // namespace

namespace legacy {

std::int64_t srtTimeMilliseconds(std::u8string_view lexeme)
{
    const u8sv raw = trimRight(lexeme);
    if (raw.empty())
        return 0;
    const std::size_t colon = raw.find(u8':');
    const std::int64_t hours = atoi(wxSubString(raw, 0, colon - 1));
    const std::int64_t minutes = atoi(wxSubString(raw, colon + 1, colon + 2));
    const std::int64_t seconds = atoi(wxSubString(raw, colon + 4, colon + 5));
    const std::int64_t millis = atoi(wxSubString(raw, colon + 7, colon + 9));
    return hours * 3'600'000 + minutes * 60'000 + seconds * 1'000 + millis;
}

std::u8string srtTimeText(std::int64_t ms)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld,%03lld", static_cast<long long>(ms / 3600000),
                  static_cast<long long>(ms / 60000 % 60), static_cast<long long>(ms / 1000 % 60),
                  static_cast<long long>(ms % 1000));
    return std::u8string(reinterpret_cast<const char8_t *>(buf));
}

} // namespace legacy

LoadResult loadSrt(std::span<const std::byte> bytes)
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

    std::vector<PhysicalLine> lines;
    while (pos < b.size()) {
        std::size_t end = pos;
        while (end < b.size() && b[end] != std::byte{'\n'})
            ++end;
        PhysicalLine line{SourceSpan{pos, end - pos, end < b.size() ? 1u : 0u}};
        if (line.span.terminatorLength == 1 && line.span.length > 0 && b[end - 1] == std::byte{'\r'}) {
            line.span.length -= 1;
            line.span.terminatorLength = 2;
        }
        line.valid = validUtf8(b.data() + line.span.offset, line.span.length);
        if (line.valid)
            line.text = u8sv(reinterpret_cast<const char8_t *>(b.data() + line.span.offset), line.span.length);
        else
            result.diagnostics.push_back({Diagnostic::Severity::Warning, Diagnostic::Kind::InvalidUtf8,
                                          line.span.offset, {}});
        lines.push_back(line);
        pos = end < b.size() ? end + 1 : end;
    }

    // Pass 1: the legacy algorithm, recording where each cue's timing line is
    // and whether TrimLastNumber took a cue-number line from the previous text.
    struct Cue {
        std::size_t timingLine;
        std::optional<std::size_t> numberLine;
        LegacyCue legacy;
    };
    std::vector<Cue> cues;
    std::u8string text1;
    std::optional<std::size_t> openTiming;
    std::optional<std::size_t> lastTokenLine;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!isToken(lines[i]))
            continue;
        const u8sv token = trimRight(lines[i].text);
        if (token.find(u8" --> ") != u8sv::npos) {
            const std::size_t before = text1.size();
            trimLastNumber(text1);
            // A digits-only previous token that trimLastNumber removed is this cue's number.
            std::optional<std::size_t> number;
            if (lastTokenLine && text1.size() < before) {
                const u8sv prev = trimRight(lines[*lastTokenLine].text);
                if (!prev.empty() && prev.find_first_not_of(u8"0123456789") == u8sv::npos)
                    number = *lastTokenLine;
            }
            if (openTiming && !text1.empty())
                cues.back().legacy = parseCue(text1);
            else if (openTiming)
                cues.pop_back(); // legacy adds nothing when the accumulated cue trimmed to empty
            cues.push_back(Cue{i, number, {}});
            openTiming = i;
            text1.clear();
        }
        text1 += token;
        text1 += u8"\r\n";
        lastTokenLine = i;
    }
    if (openTiming) {
        const std::u8string finalText(trimRight(text1)); // wxString::Trim() trims the right side
        if (!finalText.empty())
            cues.back().legacy = parseCue(finalText);
        else
            cues.pop_back();
    }

    // Pass 2: records. A cue spans its number line (if any) through the line
    // before the next cue's first line; text before the first cue is opaque.
    auto &sections = DocumentBuilder::sections(doc);
    sections.push_back(Section{SectionKind::Cues, {}, std::nullopt, {}});
    auto &records = sections.back().records;
    std::size_t next = 0;
    for (std::size_t c = 0; c < cues.size(); ++c) {
        const std::size_t first = cues[c].numberLine.value_or(cues[c].timingLine);
        for (; next < first; ++next)
            records.push_back(OpaqueRecord{lines[next].span});
        std::size_t last = c + 1 < cues.size() ? cues[c + 1].numberLine.value_or(cues[c + 1].timingLine) : lines.size();
        // Trailing blank separator lines stay separate opaque records, so an
        // edited cue regenerates without swallowing the gap to the next cue.
        while (last > first + 1 && lines[last - 1].valid && trim(lines[last - 1].text).empty())
            --last;
        const SourceSpan &a = lines[first].span, &z = lines[last - 1].span;
        LineRecord line;
        line.id = DocumentBuilder::nextLineId(doc);
        line.span = SourceSpan{a.offset, z.offset + z.length - a.offset, z.terminatorLength};
        line.style = u8"Default";
        const LegacyCue &lc = cues[c].legacy;
        line.start = TimeField{lc.startLexeme, DocumentTime(legacy::srtTimeMilliseconds(lc.startLexeme) * 1000)};
        line.end = TimeField{lc.endLexeme, DocumentTime(legacy::srtTimeMilliseconds(lc.endLexeme) * 1000)};
        line.text = lc.text;
        if (cues[c].numberLine)
            line.cueNumber = std::u8string(trimRight(lines[*cues[c].numberLine].text));
        records.push_back(std::move(line));
        next = last;
    }
    for (; next < lines.size(); ++next)
        records.push_back(OpaqueRecord{lines[next].span});
    return result;
}

std::vector<std::byte> encodeSrt(const Document &document)
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
            // The cue's own newline style; the legacy writer's form otherwise.
            const std::u8string_view nl = span.terminatorLength == 1 ? u8"\n" : u8"\r\n";
            if (line->cueNumber) {
                append(*line->cueNumber);
                append(nl);
            }
            append(legacy::srtTimeText(line->start.value.microseconds() / 1000));
            append(u8" --> ");
            append(legacy::srtTimeText(line->end.value.microseconds() / 1000));
            append(nl);
            std::u8string text = line->text;
            for (std::size_t p; (p = text.find(u8"\\N")) != std::u8string::npos;)
                text.replace(p, 2, nl);
            append(text);
            copy(span.offset + span.length, span.terminatorLength);
        }
    return out;
}

} // namespace hikari::core
