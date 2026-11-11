#include "hikari/core/lyrics_import.h"

#include "lyrics_lines.h"
#include "text_util.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// KRC and QRC as their decoders write them: "[start,length]" opens a Line, in ms. KRC words
// follow "<offset,length,0>", their offset counted from the Line's start; QRC words come
// before "(start,length)", their start counted from the beginning of the song.

namespace hikari::core::detail {

namespace {

using Kind = LyricsProblem::Kind;

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// "n,n" or "n,n,n" of plain digits, all within kLatestLyricsTime.
std::optional<std::vector<std::int64_t>> numbers(u8sv text, std::size_t count)
{
    std::vector<std::int64_t> out;
    for (std::size_t from = 0; out.size() < count;) {
        const std::size_t comma = std::min(text.find(u8',', from), text.size());
        const u8sv digits = trim(text.substr(from, comma - from));
        if (digits.empty() || !std::ranges::all_of(digits, isDigit))
            return std::nullopt;
        out.push_back(clampedNumber(digits));
        if (out.back() > kLatestLyricsTime)
            return std::nullopt;
        if (comma == text.size())
            break;
        from = comma + 1;
    }
    if (out.size() != count)
        return std::nullopt;
    return out;
}

// The stamp at `at` that `open` starts and `close` ends: its numbers and the index after it.
std::optional<std::pair<std::vector<std::int64_t>, std::size_t>> stampAt(u8sv text, std::size_t at, char8_t open,
                                                                           char8_t close, std::size_t count)
{
    if (at >= text.size() || text[at] != open || at + 1 >= text.size() || !isDigit(text[at + 1]))
        return std::nullopt;
    const std::size_t end = text.find(close, at + 1);
    if (end == u8sv::npos)
        return std::nullopt;
    auto values = numbers(text.substr(at + 1, end - at - 1), count);
    if (!values)
        return std::nullopt;
    return std::pair{std::move(*values), end + 1};
}

std::u8string entityText(u8sv name)
{
    if (name == u8"amp")
        return u8"&";
    if (name == u8"lt")
        return u8"<";
    if (name == u8"gt")
        return u8">";
    if (name == u8"quot")
        return u8"\"";
    if (name == u8"apos")
        return u8"'";
    if (name.size() < 2 || name[0] != u8'#')
        return {};
    const bool hex = name[1] == u8'x' || name[1] == u8'X';
    std::uint32_t code = 0;
    for (char8_t c : name.substr(hex ? 2 : 1)) {
        int digit = -1;
        if (isDigit(c))
            digit = c - u8'0';
        else if (hex && c >= u8'a' && c <= u8'f')
            digit = c - u8'a' + 10;
        else if (hex && c >= u8'A' && c <= u8'F')
            digit = c - u8'A' + 10;
        if (digit < 0 || code > 0x10FFFF)
            return {};
        code = code * (hex ? 16 : 10) + static_cast<std::uint32_t>(digit);
    }
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        return {};
    std::u8string out;
    if (code < 0x80) {
        out += static_cast<char8_t>(code);
    } else if (code < 0x800) {
        out += static_cast<char8_t>(0xC0 | (code >> 6));
        out += static_cast<char8_t>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char8_t>(0xE0 | (code >> 12));
        out += static_cast<char8_t>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char8_t>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char8_t>(0xF0 | (code >> 18));
        out += static_cast<char8_t>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char8_t>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char8_t>(0x80 | (code & 0x3F));
    }
    return out;
}

struct QrcContent {
    std::u8string text;
    std::size_t lineBefore = 0; // file lines before the one the lyrics start on
};

// QRC's lyrics sit in the LyricContent attribute of its XML; a file without one is the bare lyrics.
QrcContent qrcContent(u8sv text)
{
    const u8sv key = u8"LyricContent=\"";
    const std::size_t at = text.find(key);
    if (at == u8sv::npos)
        return {std::u8string(text), 0};
    const std::size_t from = at + key.size();
    std::size_t lineBefore = 0;
    for (std::size_t i = 0; lineAt(text, i).second <= from; i = lineAt(text, i).second)
        ++lineBefore;
    const std::size_t end = std::min(text.find(u8'"', from), text.size());
    std::u8string out;
    for (std::size_t i = from; i < end; ++i) {
        const std::size_t semi = text[i] == u8'&' ? text.find(u8';', i) : u8sv::npos;
        if (semi != u8sv::npos && semi < end) {
            if (std::u8string decoded = entityText(text.substr(i + 1, semi - i - 1)); !decoded.empty()) {
                out += decoded;
                i = semi;
                continue;
            }
        }
        out += text[i];
    }
    return {std::move(out), lineBefore};
}

class TimedLines {
public:
    TimedLines(LyricsFormat format, std::size_t fileSize)
    {
        m_sheet.format = format;
        m_sheet.budget = lyricsBudget(fileSize);
    }

    std::expected<ImportedLyrics, LyricsProblem> read(u8sv text, std::size_t lineBefore = 0)
    {
        if (startsWith(text, u8"\uFEFF"))
            text.remove_prefix(3);
        std::size_t lineNumber = lineBefore;
        for (std::size_t from = 0; from <= text.size();) {
            const auto [newline, next] = lineAt(text, from);
            const u8sv raw = text.substr(from, newline - from);
            from = next;
            ++lineNumber;
            if (!validUtf8(reinterpret_cast<const std::byte *>(raw.data()), raw.size()))
                return std::unexpected(LyricsProblem{Kind::NotUtf8, lineNumber, {}});
            if (auto problem = line(trim(raw), lineNumber))
                return std::unexpected(*problem);
        }
        if (m_sheet.lines.empty())
            return std::unexpected(LyricsProblem{Kind::NoLyrics, 0, {}});
        for (LyricLine &l : m_sheet.lines) {
            l.start = shifted(l.start);
            l.end = shifted(l.end);
            for (LyricWord &word : l.words) {
                word.begin = shifted(word.begin);
                word.end = shifted(word.end);
            }
        }
        std::ranges::stable_sort(m_sheet.lines, {}, &LyricLine::start);
        m_sheet.offset = m_offset;
        return lyricsDocument(std::move(m_sheet));
    }

private:
    std::int64_t shifted(std::int64_t t) const { return std::clamp<std::int64_t>(t - m_offset, 0, kLatestLyricsTime); }

    std::optional<LyricsProblem> line(u8sv rest, std::size_t lineNumber)
    {
        if (rest.empty())
            return std::nullopt;
        const std::size_t close = rest.find(u8']');
        if (rest.size() > 1 && rest[0] == u8'[' && isDigit(rest[1])) {
            const auto stamp = stampAt(rest, 0, u8'[', u8']', 2);
            if (!stamp)
                return LyricsProblem{Kind::UnreadableTime, lineNumber,
                                     std::u8string(rest.substr(0, close == u8sv::npos ? rest.size() : close + 1))};
            return timed(stamp->first[0], stamp->first[1], rest.substr(stamp->second), lineNumber);
        }
        const std::size_t colon = rest.find(u8':');
        if (!rest.starts_with(u8'[') || close == u8sv::npos || colon == u8sv::npos || colon > close)
            return LyricsProblem{Kind::MissingTime, lineNumber, std::u8string(rest)};
        const u8sv key = trim(rest.substr(1, colon - 1));
        const u8sv value = trim(rest.substr(colon + 1, close - colon - 1));
        if (key == u8"ti" && !value.empty())
            m_sheet.title = std::u8string(value);
        else if (key == u8"ar")
            m_sheet.artist = std::u8string(value);
        else if (key == u8"offset") {
            u8sv digits = value;
            const bool negative = digits.starts_with(u8'-');
            if (negative || digits.starts_with(u8'+'))
                digits.remove_prefix(1);
            if (digits.empty() || !std::ranges::all_of(digits, isDigit) || clampedNumber(digits) > kLatestLyricsTime)
                return LyricsProblem{Kind::UnreadableTime, lineNumber, std::u8string(rest.substr(0, close + 1))};
            m_offset = negative ? -clampedNumber(digits) : clampedNumber(digits);
        }
        return std::nullopt;
    }

    std::optional<LyricsProblem> timed(std::int64_t start, std::int64_t length, u8sv words, std::size_t lineNumber)
    {
        LyricLine l;
        l.start = start;
        l.end = std::min(start + length, kLatestLyricsTime);
        std::u8string pending;
        auto addWord = [&](std::int64_t begin, std::int64_t wordLength) {
            l.words.push_back(LyricWord{std::min(begin, kLatestLyricsTime),
                                        std::min(begin + wordLength, kLatestLyricsTime), literalText(pending)});
            pending.clear();
        };
        std::size_t i = 0;
        if (m_sheet.format == LyricsFormat::Krc) {
            std::optional<std::pair<std::int64_t, std::int64_t>> open;
            while (i < words.size()) {
                if (auto stamp = stampAt(words, i, u8'<', u8'>', 3)) {
                    if (open)
                        addWord(open->first, open->second);
                    else if (!trim(pending).empty())
                        addWord(start, 0);
                    else
                        pending.clear();
                    open = std::pair{start + stamp->first[0], stamp->first[1]};
                    i = stamp->second;
                    continue;
                }
                pending += words[i++];
            }
            if (open)
                addWord(open->first, open->second);
        } else {
            while (i < words.size()) {
                if (auto stamp = stampAt(words, i, u8'(', u8')', 2)) {
                    addWord(stamp->first[0], stamp->first[1]);
                    i = stamp->second;
                    continue;
                }
                pending += words[i++];
            }
        }
        if (l.words.empty())
            l.text = literalText(trim(pending));
        else
            l.words.back().text += literalText(pending);
        std::erase_if(l.words, [](const LyricWord &w) { return w.text.empty(); });
        if (l.words.empty() && l.text.empty())
            return std::nullopt;
        std::size_t bytes = l.text.size();
        for (const LyricWord &w : l.words)
            bytes += w.text.size();
        if (!spend(m_sheet, bytes, 0, l.words.size()))
            return LyricsProblem{Kind::TooLarge, lineNumber, {}};
        m_sheet.lines.push_back(std::move(l));
        return std::nullopt;
    }

    LyricSheet m_sheet;
    std::int64_t m_offset = 0;
};

}

std::expected<ImportedLyrics, LyricsProblem> readKrc(std::span<const std::byte> bytes)
{
    if (bytes.size() > kMaxLyricsFileSize)
        return std::unexpected(LyricsProblem{Kind::TooLarge, 0, {}});
    return TimedLines(LyricsFormat::Krc, bytes.size()).read(u8sv(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()));
}

std::expected<ImportedLyrics, LyricsProblem> readQrc(std::span<const std::byte> bytes)
{
    if (bytes.size() > kMaxLyricsFileSize)
        return std::unexpected(LyricsProblem{Kind::TooLarge, 0, {}});
    const QrcContent content = qrcContent(u8sv(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()));
    return TimedLines(LyricsFormat::Qrc, bytes.size()).read(content.text, content.lineBefore);
}

}
