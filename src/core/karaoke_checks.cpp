#include "hikari/core/karaoke_checks.h"

#include "text_util.h"

#include <algorithm>
#include <optional>
#include <string>

namespace hikari::core {

namespace {

using detail::u8sv;

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// The kara-templater roles that hold templates rather than lyrics.
bool isTemplateLine(const LineRecord &line)
{
    u8sv effect = detail::trimLeft(line.effect);
    std::u8string first;
    for (char8_t c : effect) {
        if (detail::isSpace(c))
            break;
        first += c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
    }
    return first == u8"template" || first == u8"code" || first == u8"mixin";
}

// The karaoke tag whose backslash is at block[i]: where its value starts,
// after "k", "K", "kf" or "ko"; npos for another tag ("\kt", which sets an
// absolute time, or an unknown "\keep"). As the tag parser reads tags
// (lua_helper's karaoke tags), the name is followed by its value ("(", a
// sign, a digit or "."), or by nothing at all, a tag with no number.
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

// Whether a block holds a karaoke tag.
bool hasKaraokeTag(u8sv block)
{
    for (std::size_t i = 0; i < block.size(); ++i)
        if (karaokeValue(block, i) != u8sv::npos)
            return true;
    return false;
}

// Longer syllables count as this long (over 115 days, in milliseconds), so
// adding them up cannot overflow.
constexpr std::int64_t kLongestSyllable = 10'000'000'000;

struct Syllables {
    std::vector<std::int64_t> values; // milliseconds (whole centiseconds)
    bool unclosed = false;
};

Syllables syllablesOf(u8sv text)
{
    Syllables out;
    for (std::size_t pos = 0; pos < text.size();) {
        if (text[pos] != u8'{') {
            ++pos;
            continue;
        }
        const std::size_t close = text.find(u8'}', pos);
        if (close == u8sv::npos) {
            out.unclosed = out.unclosed || hasKaraokeTag(text.substr(pos));
            break; // the rest is literal text
        }
        const u8sv block = text.substr(pos + 1, close - pos - 1);
        for (std::size_t i = 0; i + 1 < block.size(); ++i) {
            std::size_t j = karaokeValue(block, i);
            if (j == u8sv::npos)
                continue;
            // The value: "(" optional, a sign, digits and a fraction.
            const bool parenthesised = j < block.size() && block[j] == u8'(';
            if (parenthesised)
                ++j;
            const bool negative = j < block.size() && block[j] == u8'-';
            if (j < block.size() && (block[j] == u8'-' || block[j] == u8'+'))
                ++j;
            std::int64_t ms = 0;
            bool digits = false;
            for (; j < block.size() && isDigit(block[j]); ++j, digits = true)
                ms = std::min<std::int64_t>(ms * 10 + (block[j] - u8'0') * 10, kLongestSyllable);
            // A fraction is read and dropped: the karaoke the application
            // plays (AudioKaraoke's legacyAtoi, lua_helper's atoi) counts
            // whole centiseconds.
            if (j < block.size() && block[j] == u8'.')
                for (++j; j < block.size() && isDigit(block[j]); ++j)
                    digits = true;
            if (!digits) {
                out.unclosed = true; // "\k" with no number
                continue;
            }
            if (parenthesised && j < block.size() && block[j] == u8')')
                ++j;
            out.values.push_back(negative ? -ms : ms);
            i = j - 1;
        }
        pos = close + 1;
    }
    return out;
}

} // namespace

std::vector<KaraokeProblem> checkKaraoke(const Document &document)
{
    using Kind = KaraokeProblem::Kind;
    std::vector<KaraokeProblem> problems;
    const auto lines = document.lines();
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const LineRecord &line = *lines[row];
        if (isTemplateLine(line))
            continue;
        // The text karaoke plays: the translation when there is one.
        const Syllables syllables = syllablesOf(line.translation.empty() ? line.text : line.translation);
        if (syllables.values.empty() && !syllables.unclosed)
            continue;
        auto problem = [&](Kind kind) {
            KaraokeProblem p;
            p.line = line.id;
            p.row = row;
            p.kind = kind;
            return p;
        };
        if (syllables.unclosed)
            problems.push_back(problem(Kind::Unclosed));
        // Each end in whole milliseconds, as the audio display's karaoke
        // reads the Line (Application's audio Lines), then their distance.
        const std::int64_t duration = line.end.value.microseconds() / 1000 - line.start.value.microseconds() / 1000;
        std::int64_t total = 0;
        KaraokeProblem zero = problem(Kind::ZeroLength);
        KaraokeProblem outside = problem(Kind::OutsideLine);
        for (std::size_t i = 0; i < syllables.values.size(); ++i) {
            const std::int64_t start = total, length = syllables.values[i];
            total += length;
            if (length == 0)
                zero.syllables.push_back(i + 1);
            if (length < 0 || start < 0 || total < 0 || start > duration || total > duration) // or it runs backward
                outside.syllables.push_back(i + 1);
        }
        if (!syllables.values.empty() && total != duration) {
            KaraokeProblem p = problem(Kind::TotalNotDuration);
            p.totalMs = total;
            p.durationMs = duration;
            problems.push_back(std::move(p));
        }
        if (!zero.syllables.empty())
            problems.push_back(std::move(zero));
        if (!outside.syllables.empty())
            problems.push_back(std::move(outside));
    }
    return problems;
}

} // namespace hikari::core
