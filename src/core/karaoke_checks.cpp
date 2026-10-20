#include "hikari/core/karaoke_checks.h"

#include "text_util.h"

#include <cstdlib>
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

// Whether a block holds a karaoke tag: "\k" or "\K" not followed by "t"
// (\kt sets an absolute time; it is not a syllable).
bool hasKaraokeTag(u8sv block)
{
    for (std::size_t i = 0; i + 1 < block.size(); ++i)
        if (block[i] == u8'\\' && (block[i + 1] == u8'k' || block[i + 1] == u8'K') &&
            !(block[i + 1] == u8'k' && i + 2 < block.size() && block[i + 2] == u8't'))
            return true;
    return false;
}

struct Syllables {
    std::vector<std::int64_t> values; // centiseconds
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
            if (block[i] != u8'\\' || (block[i + 1] != u8'k' && block[i + 1] != u8'K'))
                continue;
            std::size_t j = i + 2;
            if (block[i + 1] == u8'k' && j < block.size() && block[j] == u8't')
                continue;
            if (block[i + 1] == u8'k' && j < block.size() && (block[j] == u8'f' || block[j] == u8'o'))
                ++j;
            const std::size_t number = j;
            if (j < block.size() && block[j] == u8'-')
                ++j;
            const std::size_t digits = j;
            while (j < block.size() && isDigit(block[j]))
                ++j;
            if (j == digits) {
                out.unclosed = true; // "\k" with no number
                continue;
            }
            out.values.push_back(std::strtoll(std::string(block.begin() + number, block.begin() + j).c_str(), nullptr, 10));
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
        const Syllables syllables = syllablesOf(line.text);
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
        const std::int64_t durationMs = (line.end.value.microseconds() - line.start.value.microseconds()) / 1000;
        const std::int64_t duration = (durationMs + (durationMs < 0 ? -5 : 5)) / 10;
        std::int64_t total = 0;
        KaraokeProblem zero = problem(Kind::ZeroLength);
        KaraokeProblem outside = problem(Kind::OutsideLine);
        for (std::size_t i = 0; i < syllables.values.size(); ++i) {
            const std::int64_t start = total, length = syllables.values[i];
            total += length;
            if (length == 0)
                zero.syllables.push_back(i + 1);
            if (start < 0 || total < 0 || start > duration || (start == duration && length > 0))
                outside.syllables.push_back(i + 1);
        }
        if (!syllables.values.empty() && total != duration) {
            KaraokeProblem p = problem(Kind::TotalNotDuration);
            p.totalCs = total;
            p.durationCs = duration;
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
