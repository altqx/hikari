#include "hikari/core/lyrics_import.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "lyrics_lines.h"
#include "text_util.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::core {

namespace detail {

namespace {

std::u8string number(std::int64_t n)
{
    const std::string s = std::to_string(n);
    return std::u8string(s.begin(), s.end());
}

std::int64_t centiseconds(std::int64_t ms)
{
    return (ms + 5) / 10;
}

// A Name loses its commas (field separators); one starting with "[" gets a
// word joiner first, or the loader would read a marker in it as a Line state.
std::u8string field(std::u8string_view text, bool name)
{
    std::u8string out;
    for (char8_t c : text)
        if (c != u8',' || !name)
            out += c < 0x20 ? u8' ' : c;
    out = std::u8string(trim(out));
    if (name && out.starts_with(u8'['))
        out.insert(0, u8"\u2060");
    return out;
}

std::u8string titleField(std::u8string_view text)
{
    std::u8string words;
    for (char8_t c : text)
        if (!isSpace(c) || (!words.empty() && words.back() != u8' '))
            words += isSpace(c) ? u8' ' : c;
    return field(words, false);
}

}

std::u8string literalText(std::u8string_view text)
{
    std::u8string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{')
            out += u8"\uFF5B";
        else if (text[i] == u8'}')
            out += u8"\uFF5D";
        else {
            out += text[i];
            if (text[i] == u8'\\' &&
                (i + 1 == text.size() || text[i + 1] == u8'N' || text[i + 1] == u8'n' || text[i + 1] == u8'h'))
                out += u8"\u2060";
        }
    }
    return out;
}

std::u8string karaokeText(LyricLine &line)
{
    const std::int64_t start = centiseconds(line.start);
    std::int64_t end = centiseconds(line.end);
    std::int64_t cursor = start;
    std::u8string text;
    auto syllable = [&text](std::int64_t length, std::u8string_view words) {
        text += u8"{\\k" + number(length) + u8"}";
        text += words;
    };
    for (std::size_t i = 0; i < line.words.size(); ++i) {
        const LyricWord &word = line.words[i];
        const std::int64_t begin = std::max(centiseconds(word.begin), cursor);
        std::int64_t stop = centiseconds(word.end);
        if (i + 1 < line.words.size())
            stop = std::min(stop, centiseconds(line.words[i + 1].begin));
        stop = std::max(stop, begin);
        if (begin > cursor)
            syllable(begin - cursor, {});
        syllable(stop - begin, i + 1 == line.words.size() ? trimRight(word.text) : u8sv(word.text));
        cursor = stop;
    }
    if (end > cursor)
        syllable(end - cursor, {});
    else
        end = cursor;
    line.start = start * 10;
    line.end = end * 10;
    return text;
}

LoadResult lyricsDocument(LyricSheet sheet)
{
    std::u8string title = sheet.title ? titleField(*sheet.title) : std::u8string();
    if (title.empty())
        title = u8"HikariSub Ass File";
    std::u8string script =
        u8"[Script Info]\r\n"
        u8"Title: " +
        title +
        u8"\r\n"
        u8"PlayResX: 1280\r\n"
        u8"PlayResY: 720\r\n"
        u8"ScaledBorderAndShadow: yes\r\n"
        u8"WrapStyle: 0\r\n"
        u8"ScriptType: v4.00+\r\n"
        u8"Last Style Storage: Default\r\n"
        u8"YCbCr Matrix: TV.601\r\n"
        u8"\r\n"
        u8"[V4+ Styles]\r\n"
        u8"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
        u8"Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
        u8"MarginL, MarginR, MarginV, Encoding\r\n"
        u8"Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\r\n"
        u8"\r\n"
        u8"[Events]\r\n"
        u8"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
    for (LyricLine &line : sheet.lines) {
        const std::u8string text = line.words.empty() ? field(line.text, false) : field(karaokeText(line), false);
        script += u8"Dialogue: 0," + legacy::assTimeText(line.start) + u8"," + legacy::assTimeText(line.end) +
                  u8",Default," + field(line.actor, true) + u8",0,0,0,," + text + u8"\r\n";
    }
    sheet.lines = {};
    return loadAss(std::as_bytes(std::span(script)));
}

}

namespace {

using detail::LyricLine;
using detail::LyricWord;
using detail::u8sv;

// LRC stamp: "mm:ss", "mm:ss.f" to "mm:ss.fff", "h:mm:ss.ff", or "mm:ss:ff"
// (hundredths after a colon).
std::optional<std::int64_t> lrcTime(u8sv text)
{
    text = detail::trim(text);
    std::vector<u8sv> parts;
    for (std::size_t from = 0;;) {
        const std::size_t colon = text.find(u8':', from);
        parts.push_back(text.substr(from, colon == u8sv::npos ? u8sv::npos : colon - from));
        if (colon == u8sv::npos)
            break;
        from = colon + 1;
    }
    u8sv fraction;
    if (const std::size_t dot = parts.back().find(u8'.'); dot != u8sv::npos) {
        fraction = parts.back().substr(dot + 1);
        parts.back() = parts.back().substr(0, dot);
        if (fraction.empty())
            return std::nullopt;
    } else if (parts.size() == 3) {
        fraction = parts.back();
        parts.pop_back();
    }
    if (parts.size() < 2 || parts.size() > 3)
        return std::nullopt;
    auto digits = [](u8sv s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char8_t c) { return c >= u8'0' && c <= u8'9'; });
    };
    auto value = [](u8sv s) { return detail::clampedNumber(s); };
    // The first part may run past 59 ("[75:02.00]").
    std::int64_t ms = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!digits(parts[i]) || (i > 0 && value(parts[i]) >= 60))
            return std::nullopt;
        ms = ms * 60 + value(parts[i]);
    }
    ms *= 1000;
    if (!fraction.empty()) {
        if (!digits(fraction) || fraction.size() > 3)
            return std::nullopt;
        const u8sv first = fraction.substr(0, 3);
        std::int64_t f = value(first);
        for (std::size_t i = first.size(); i < 3; ++i)
            f *= 10;
        ms += f;
    }
    if (ms > detail::kLatestLyricsTime)
        return std::nullopt;
    return ms;
}

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// "<3 you", "<12: tonight>" and "[1st verse]" are lyrics; "<00:0x.00>" is a
// broken stamp.
bool stampShaped(u8sv text)
{
    text = detail::trimLeft(text);
    if (text.empty() || !isDigit(text.front()))
        return false;
    std::size_t n = 0;
    while (n < text.size() && (isDigit(text[n]) || text[n] == u8':' || text[n] == u8'.'))
        ++n;
    for (std::size_t i = 0; i + 1 < n; ++i)
        if (text[i] == u8':' && isDigit(text[i + 1]))
            return true;
    return false;
}

struct LrcEntry {
    std::size_t lineNumber = 0;
    std::vector<std::int64_t> times;
    std::vector<LyricWord> words;    // begin set; end set only from a later stamp
    std::vector<bool> ended;         // per word: its end came from a stamp
    std::u8string text;              // lyrics without word stamps
    std::u8string actor;
    bool timedWords = false;
};

// Walaoke's "M:", "F:" and "D:", or a converter's "v1:" agents (up to 9 digits).
std::optional<std::u8string> voiceMarker(u8sv &rest)
{
    std::size_t n = 0;
    if (!rest.empty() && (rest[0] == u8'M' || rest[0] == u8'F' || rest[0] == u8'D'))
        n = 1;
    else if (rest.size() > 1 && rest[0] == u8'v' && isDigit(rest[1]))
        for (n = 1; n < rest.size() && n <= 9 && isDigit(rest[n]); ++n) {
        }
    if (n == 0 || n >= rest.size() || rest[n] != u8':')
        return std::nullopt;
    std::u8string voice(rest.substr(0, n));
    rest = detail::trimLeft(rest.substr(n + 1));
    return voice;
}

}

bool isLyricsExtension(std::u8string_view extension)
{
    return extension == u8"lrc" || extension == u8"ttml";
}

std::expected<LoadResult, LyricsProblem> importLrc(std::span<const std::byte> bytes)
{
    using Kind = LyricsProblem::Kind;
    if (bytes.size() > kMaxLyricsFileSize)
        return std::unexpected(LyricsProblem{Kind::TooLarge, 0, {}});
    u8sv all(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size());
    if (detail::startsWith(all, u8"\uFEFF"))
        all.remove_prefix(3);

    detail::LyricSheet sheet;
    sheet.budget = detail::lyricsBudget(bytes.size());
    std::int64_t offset = 0;
    std::u8string voice;
    std::vector<LrcEntry> entries;
    std::size_t stamps = 0;
    std::size_t lineNumber = 0;
    for (std::size_t from = 0; from <= all.size();) {
        const auto [newline, next] = detail::lineAt(all, from);
        const u8sv raw = all.substr(from, newline - from);
        from = next;
        ++lineNumber;
        if (!detail::validUtf8(reinterpret_cast<const std::byte *>(raw.data()), raw.size()))
            return std::unexpected(LyricsProblem{Kind::NotUtf8, lineNumber, {}});
        u8sv rest = detail::trim(raw);
        if (rest.empty())
            continue;

        LrcEntry entry;
        entry.lineNumber = lineNumber;
        while (!rest.empty() && rest.front() == u8'[') {
            const std::size_t close = rest.find(u8']');
            const u8sv tag = close == u8sv::npos ? rest.substr(1) : rest.substr(1, close - 1);
            const u8sv inner = detail::trimLeft(tag);
            if (stampShaped(inner)) {
                const auto time = close == u8sv::npos ? std::nullopt : lrcTime(tag);
                if (!time)
                    return std::unexpected(LyricsProblem{Kind::UnreadableTime, lineNumber,
                                                         std::u8string(rest.substr(0, close == u8sv::npos ? rest.size() : close + 1))});
                entry.times.push_back(*time);
            } else {
                const std::size_t colon = tag.find(u8':');
                // After a line time only a lowercase key ("[class:shhh]") makes
                // a tag; "[Verse: 1]" there is lyrics.
                auto tagKey = [](u8sv key) {
                    return !key.empty() && std::ranges::all_of(key, [](char8_t c) {
                        return (c >= u8'a' && c <= u8'z') || isDigit(c) || c == u8'_' || c == u8'-';
                    });
                };
                if (close == u8sv::npos || colon == u8sv::npos || (!entry.times.empty() && !tagKey(tag.substr(0, colon))))
                    break;
                const u8sv key = detail::trim(tag.substr(0, colon));
                const u8sv value = detail::trim(tag.substr(colon + 1));
                if (entry.times.empty() && key == u8"ti" && !detail::titleField(value).empty())
                    sheet.title = std::u8string(value);
                if (entry.times.empty() && key == u8"offset") {
                    // Signed milliseconds; positive shows the lyrics sooner.
                    u8sv digits = value;
                    const bool negative = !digits.empty() && digits.front() == u8'-';
                    if (!digits.empty() && (digits.front() == u8'-' || digits.front() == u8'+'))
                        digits.remove_prefix(1);
                    std::int64_t n = 0;
                    const bool number = digits.empty() ? digits.size() == value.size()
                                                       : std::ranges::all_of(digits, isDigit);
                    if (number)
                        n = detail::clampedNumber(digits);
                    if (!number || n > detail::kLatestLyricsTime)
                        return std::unexpected(LyricsProblem{Kind::UnreadableTime, lineNumber,
                                                             std::u8string(rest.substr(0, close + 1))});
                    offset = negative ? -n : n;
                }
            }
            rest = detail::trimLeft(rest.substr(close + 1));
        }
        stamps += entry.times.size();
        if (stamps > detail::kMaxLyricsLines)
            return std::unexpected(LyricsProblem{Kind::TooLarge, lineNumber, {}});
        if (rest.empty()) {
            if (!entry.times.empty())
                entries.push_back(std::move(entry)); // a bare time ends the line before
            continue;
        }

        if (auto marker = voiceMarker(rest))
            voice = std::move(*marker);
        entry.actor = voice;
        // "<mm:ss.xx>" starts the text after it and ends the word before.
        std::u8string pending;
        std::optional<std::int64_t> pendingBegin;
        auto closeWord = [&](std::optional<std::int64_t> end) {
            const u8sv word = detail::trimLeft(pending);
            if (!entry.words.empty() && word.size() < pending.size())
                entry.words.back().text += detail::literalText(u8sv(pending).substr(0, pending.size() - word.size()));
            if (!word.empty()) {
                entry.words.push_back(LyricWord{pendingBegin.value_or(-1), end.value_or(0), detail::literalText(word)});
                entry.ended.push_back(end.has_value());
            }
            pending.clear();
        };
        std::optional<std::int64_t> lastStamp;
        for (std::size_t i = 0; i < rest.size();) {
            if (rest[i] == u8'<' && stampShaped(rest.substr(i + 1))) {
                const std::size_t close = rest.find(u8'>', i);
                const auto time = close == u8sv::npos ? std::nullopt : lrcTime(rest.substr(i + 1, close - i - 1));
                if (!time)
                    return std::unexpected(LyricsProblem{Kind::UnreadableTime, lineNumber,
                                                         std::u8string(rest.substr(i, close == u8sv::npos ? u8sv::npos : close - i + 1))});
                const bool leadingWords = !lastStamp && !detail::trim(pending).empty() && !entry.times.empty();
                if ((lastStamp && *time < *lastStamp) || (leadingWords && *time < entry.times.front()))
                    return std::unexpected(LyricsProblem{Kind::TimeGoesBack, lineNumber, std::u8string(rest.substr(i, close - i + 1))});
                lastStamp = time;
                entry.timedWords = true;
                closeWord(time);
                pendingBegin = time;
                i = close + 1;
                continue;
            }
            pending += rest[i++];
        }
        if (entry.timedWords) {
            closeWord(std::nullopt);
            if (entry.times.size() > 1)
                return std::unexpected(LyricsProblem{Kind::RepeatedWordLine, lineNumber, std::u8string(detail::trim(raw))});
            if (!entry.words.empty() && entry.words.front().begin < 0 && entry.times.empty())
                return std::unexpected(LyricsProblem{Kind::MissingTime, lineNumber, std::u8string(detail::trim(raw))});
        } else {
            if (entry.times.empty())
                return std::unexpected(LyricsProblem{Kind::MissingTime, lineNumber, std::u8string(detail::trim(raw))});
            entry.text = detail::literalText(rest);
        }
        if (entry.times.empty() && entry.words.empty())
            continue;
        if (entry.times.empty()) {
            entry.times.push_back(entry.words.front().begin);
            if (++stamps > detail::kMaxLyricsLines)
                return std::unexpected(LyricsProblem{Kind::TooLarge, lineNumber, {}});
        }
        entries.push_back(std::move(entry));
    }

    struct Placed {
        std::int64_t start = 0;
        const LrcEntry *entry = nullptr;
    };
    auto shifted = [offset](std::int64_t t) {
        return std::clamp<std::int64_t>(t - offset, 0, detail::kLatestLyricsTime);
    };
    std::vector<Placed> placed;
    for (const LrcEntry &entry : entries)
        for (std::int64_t t : entry.times)
            placed.push_back({shifted(t), &entry});
    std::stable_sort(placed.begin(), placed.end(), [](const Placed &a, const Placed &b) { return a.start < b.start; });

    // MUST KILL: kLastLine copies a new Line's default duration
    constexpr std::int64_t kLastLine = 5000;
    std::vector<std::optional<std::int64_t>> nextTimes(placed.size());
    for (std::size_t i = placed.size(); i-- > 1;)
        nextTimes[i - 1] = placed[i].start > placed[i - 1].start ? std::optional(placed[i].start) : nextTimes[i];
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const LrcEntry &entry = *placed[i].entry;
        if (entry.words.empty() && entry.text.empty())
            continue;
        const std::optional<std::int64_t> next = nextTimes[i];
        std::size_t wordBytes = 0;
        for (const LyricWord &word : entry.words)
            wordBytes += word.text.size();
        if (!detail::spend(sheet, entry.text.size() + wordBytes, entry.actor.size(), entry.words.size()))
            return std::unexpected(LyricsProblem{Kind::TooLarge, entry.lineNumber, {}});
        LyricLine line;
        line.start = placed[i].start;
        line.actor = entry.actor;
        if (entry.words.empty()) {
            line.end = next.value_or(std::min(line.start + kLastLine, detail::kLatestLyricsTime));
            line.text = entry.text;
        } else {
            for (std::size_t k = 0; k < entry.words.size(); ++k) {
                LyricWord word = entry.words[k];
                word.begin = word.begin < 0 ? line.start : shifted(word.begin);
                word.end = entry.ended[k] ? shifted(word.end) : -1;
                line.words.push_back(std::move(word));
            }
            line.start = std::min(line.start, line.words.front().begin);
            const std::int64_t lastBegin = line.words.back().begin;
            if (line.words.back().end < 0)
                line.words.back().end =
                        std::max(lastBegin, next.value_or(std::min(lastBegin + kLastLine, detail::kLatestLyricsTime)));
            for (std::size_t k = 0; k + 1 < line.words.size(); ++k)
                if (line.words[k].end < 0)
                    line.words[k].end = line.words[k + 1].begin;
            line.end = line.words.back().end;
        }
        sheet.lines.push_back(std::move(line));
    }
    if (sheet.lines.empty())
        return std::unexpected(LyricsProblem{Kind::NoLyrics, 0, {}});
    placed = {};
    nextTimes = {};
    entries = {};
    return detail::lyricsDocument(std::move(sheet));
}

std::expected<LoadResult, LyricsProblem> importLyrics(std::span<const std::byte> bytes, std::u8string_view extension)
{
    if (extension == u8"lrc")
        return importLrc(bytes);
    if (extension == u8"ttml")
        return importTtml(bytes);
    return std::unexpected(LyricsProblem{LyricsProblem::Kind::NoLyrics, 0, {}});
}

}
