#include "hikari/core/karaoke_checks.h"

#include "text_util.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace hikari::core {

namespace {

using detail::u8sv;

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// kara-templater's template, code and mixin Lines, and the "fx" Lines each run regenerates.
bool isTemplateLine(const LineRecord &line)
{
    if (!line.comment && line.effect == u8"fx")
        return true;
    u8sv effect = detail::trimLeft(line.effect);
    std::u8string first;
    for (char8_t c : effect) {
        if (detail::isSpace(c))
            break;
        first += c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
    }
    return first == u8"template" || first == u8"code" || first == u8"mixin";
}

// Where the value of a \k, \K, \kf or \ko at block[i] starts; npos for another tag ("\kt", "\keep"). The
// name is followed by "(", a sign, a digit, "." or nothing.
std::size_t karaokeValue(u8sv block, std::size_t i)
{
    if (i + 1 >= block.size() || block[i] != u8'\\' || (block[i + 1] != u8'k' && block[i + 1] != u8'K'))
        return u8sv::npos;
    auto valueOrEnd = [&](std::size_t at) {
        if (at >= block.size())
            return true;
        const char8_t c = block[at];
        return c == u8'\\' || c == u8'}' || c == u8'(' || c == u8'+' || c == u8'-' || c == u8'.' || isDigit(c);
    };
    const std::size_t j = i + 2;
    if (valueOrEnd(j))
        return j;
    if (block[i + 1] == u8'k' && (block[j] == u8'f' || block[j] == u8'o') && valueOrEnd(j + 1))
        return j + 1;
    return u8sv::npos;
}

bool hasKaraokeTag(u8sv block)
{
    for (std::size_t i = 0; i < block.size(); ++i)
        if (karaokeValue(block, i) != u8sv::npos)
            return true;
    return false;
}

// 0 for none. QChar::toLower maps U+212A KELVIN SIGN to "k" (Qt 6.11), so it counts too.
std::size_t editorK(u8sv text, std::size_t i)
{
    if (i < text.size() && (text[i] == u8'k' || text[i] == u8'K'))
        return 1;
    return text.substr(std::min(i, text.size())).starts_with(u8"K") ? 3 : 0;
}

// A tag whose number follows at once, in a block that closes; none of "\k+50", "\k(50)" or "\k.5", which
// the renderers play.
bool audioEditorReads(u8sv text)
{
    auto lower = [](char8_t c) { return c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c; };
    const std::size_t lastClose = text.rfind(u8'}');
    bool inBlock = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{')
            inBlock = true;
        else if (text[i] == u8'}')
            inBlock = false;
        else if (const std::size_t k = inBlock && text[i] == u8'\\' ? editorK(text, i + 1) : 0) {
            std::size_t r = i + 1 + k;
            if (r < text.size() && lower(text[r]) == u8'f')
                ++r;
            if (r < text.size() && lower(text[r]) == u8'o')
                ++r;
            if (r < text.size() && isDigit(text[r]) && lastClose != u8sv::npos && lastClose > r)
                return true;
        }
    }
    return false;
}

// Caps a syllable so the sum cannot overflow.
constexpr std::int64_t kLongestSyllable = 10'000'000'000;

// As the audio editor splits: every \k or \K in a block, whatever follows; the number is atoi of all up to
// the next "}" or "\", so "\keep" and "\k(50)" are 0.
class EditorSyllables {
public:
    explicit EditorSyllables(u8sv text) : m_text(text) {}

    static constexpr std::int64_t kUnread = std::numeric_limits<std::int64_t>::min();

    // The next one's length in milliseconds, kUnread when the text ends
    // inside its number; nullopt past the last.
    std::optional<std::int64_t> next()
    {
        for (; m_pos < m_text.size(); ++m_pos) {
            const char8_t c = m_text[m_pos];
            if (c == u8'{')
                m_block = true;
            else if (c == u8'}')
                m_block = false;
            else if (m_block && c == u8'\\' && m_pos + 1 < m_text.size() &&
                     (m_text[m_pos + 1] == u8'k' || m_text[m_pos + 1] == u8'K'))
                return tag();
        }
        return std::nullopt;
    }

private:
    std::int64_t tag()
    {
        std::size_t from = m_pos + 2;
        if (m_text[m_pos + 1] == u8'k' && from < m_text.size() && (m_text[from] == u8'f' || m_text[from] == u8'o'))
            ++from;
        const std::size_t end = std::min(m_text.find_first_of(u8"}\\", from), m_text.size());
        m_pos = end;
        if (end == m_text.size())
            return kUnread;
        std::size_t i = from;
        while (i < end && (m_text[i] == u8' ' || (m_text[i] >= 9 && m_text[i] <= 13)))
            ++i;
        const bool negative = i < end && m_text[i] == u8'-';
        if (i < end && (m_text[i] == u8'-' || m_text[i] == u8'+'))
            ++i;
        std::int64_t ms = 0;
        for (; i < end && isDigit(m_text[i]); ++i)
            ms = std::min<std::int64_t>(ms * 10 + (m_text[i] - u8'0') * 10, kLongestSyllable);
        return negative ? -ms : ms;
    }

    u8sv m_text;
    std::size_t m_pos = 0;
    bool m_block = false;
};


// Calls `each` per syllable in order (milliseconds; nullopt for a tag with no number). True when a karaoke
// tag is unclosed or has no number.
template <typename Each>
bool syllablesOf(u8sv text, Each each)
{
    bool unclosed = false;
    for (std::size_t pos = 0; pos < text.size();) {
        if (text[pos] != u8'{') {
            ++pos;
            continue;
        }
        const std::size_t close = text.find(u8'}', pos);
        if (close == u8sv::npos) {
            unclosed = unclosed || hasKaraokeTag(text.substr(pos));
            break; // the rest is literal text
        }
        const u8sv block = text.substr(pos + 1, close - pos - 1);
        for (std::size_t i = 0; i + 1 < block.size(); ++i) {
            std::size_t j = karaokeValue(block, i);
            if (j == u8sv::npos)
                continue;
            // Inside parentheses leading whitespace is skipped ("\k( 100)" is 100).
            const bool parenthesised = j < block.size() && block[j] == u8'(';
            if (parenthesised)
                for (++j; j < block.size() && (block[j] == u8' ' || (block[j] >= 9 && block[j] <= 13)); ++j) {
                }
            const bool negative = j < block.size() && block[j] == u8'-';
            if (j < block.size() && (block[j] == u8'-' || block[j] == u8'+'))
                ++j;
            std::int64_t ms = 0;
            bool digits = false;
            for (; j < block.size() && isDigit(block[j]); ++j, digits = true)
                ms = std::min<std::int64_t>(ms * 10 + (block[j] - u8'0') * 10, kLongestSyllable);
            // The fraction is dropped: playback counts whole centiseconds.
            if (j < block.size() && block[j] == u8'.')
                for (++j; j < block.size() && isDigit(block[j]); ++j)
                    digits = true;
            if (!digits) {
                unclosed = true; // "\k" with no number
                each(std::nullopt);
                continue;
            }
            if (parenthesised && j < block.size() && block[j] == u8')')
                ++j;
            each(std::optional(negative ? -ms : ms));
            i = j - 1;
        }
        pos = close + 1;
    }
    return unclosed;
}

}

KaraokeCheck checkKaraoke(const Document &document, std::size_t limit)
{
    using Kind = KaraokeProblem::Kind;
    KaraokeCheck check;
    auto add = [&](KaraokeProblem &&p) {
        if (check.problems.size() < limit)
            check.problems.push_back(std::move(p));
        else
            ++check.unlisted;
    };
    const auto lines = document.lines();
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const LineRecord &line = *lines[row];
        if (isTemplateLine(line))
            continue;
        auto problem = [&](Kind kind) {
            KaraokeProblem p;
            p.line = line.id;
            p.row = row;
            p.kind = kind;
            return p;
        };
        const std::int64_t duration = line.end.value.microseconds() / 1000 - line.start.value.microseconds() / 1000;
        std::int64_t total = 0;
        std::size_t count = 0;
        KaraokeProblem zero = problem(Kind::ZeroLength);
        KaraokeProblem outside = problem(Kind::OutsideLine);
        auto note = [](KaraokeProblem &p, std::size_t number) {
            if (p.syllables.size() < kListedSyllables)
                p.syllables.push_back(number);
            ++p.syllableCount;
        };
        const u8sv text = line.translation.empty() ? u8sv(line.text) : u8sv(line.translation);
        const bool editorReads = audioEditorReads(text);
        const std::size_t lastClose = text.rfind(u8'}');
        const std::size_t open = text.find(u8'{', lastClose == u8sv::npos ? 0 : lastClose + 1);
        EditorSyllables editor(open != u8sv::npos && hasKaraokeTag(text.substr(open)) ? text.substr(0, open) : text);
        bool editorOnly = false;
        const bool unclosed = syllablesOf(text, [&](std::optional<std::int64_t> syllable) {
            if (editorReads && !editorOnly) {
                const auto read = editor.next();
                editorOnly = !read || (syllable && *read != *syllable);
            }
            if (!syllable)
                return;
            const std::int64_t length = *syllable;
            const std::int64_t start = total;
            total += length;
            ++count;
            if (length == 0)
                note(zero, count);
            if (length < 0 || start < 0 || total < 0 || start > duration || total > duration) // or it runs backward
                note(outside, count);
        });
        editorOnly = editorOnly || (editorReads && (count == 0 || editor.next().has_value()));
        if (count == 0 && !unclosed && !editorOnly)
            continue;
        if (unclosed)
            add(problem(Kind::Unclosed));
        if (count > 0 && !editorReads)
            add(problem(Kind::NotForAudioEditor));
        if (editorOnly)
            add(problem(Kind::EditorOnly));
        if (count > 0 && total != duration) {
            KaraokeProblem p = problem(Kind::TotalNotDuration);
            p.totalMs = total;
            p.durationMs = duration;
            add(std::move(p));
        }
        if (zero.syllableCount > 0)
            add(std::move(zero));
        if (outside.syllableCount > 0)
            add(std::move(outside));
    }
    return check;
}

}
