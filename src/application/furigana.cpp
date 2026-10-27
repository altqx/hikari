#include "hikari/application/furigana.h"

#include <cstdint>
#include <unordered_set>
#include <utility>

namespace hikari::application {
namespace {

// The code point at `i` and how many UTF-16 units it takes.
std::pair<char32_t, std::size_t> codePointAt(std::u16string_view s, std::size_t i)
{
    const char16_t c = s[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
        return {0x10000 + ((char32_t(c) - 0xD800) << 10) + (char32_t(s[i + 1]) - 0xDC00), 2};
    return {c, 1};
}

bool hasKanji(std::u16string_view s)
{
    for (std::size_t i = 0; i < s.size();) {
        const auto [c, n] = codePointAt(s, i);
        if (isKanji(c))
            return true;
        i += n;
    }
    return false;
}

bool isSmallKana(char16_t c)
{
    switch (c) {
    case u'ぁ': case u'ぃ': case u'ぅ': case u'ぇ': case u'ぉ': case u'ゃ': case u'ゅ': case u'ょ': case u'ゎ':
    case u'ァ': case u'ィ': case u'ゥ': case u'ェ': case u'ォ': case u'ャ': case u'ュ': case u'ョ': case u'ヮ':
        return true;
    default:
        return false;
    }
}

// A word's characters as runs of kanji and the text between them.
struct Segment {
    std::size_t begin = 0, end = 0;
    bool kanji = false;
};

std::vector<Segment> segments(std::u16string_view s)
{
    std::vector<Segment> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto [c, n] = codePointAt(s, i);
        const bool kanji = isKanji(c);
        if (out.empty() || out.back().kanji != kanji)
            out.push_back({i, i, kanji});
        i += n;
        out.back().end = i;
    }
    return out;
}

// The earliest split of `reading` (hiragana) over the segments: each kanji
// run one kana or more, the text between spelt as it is. A place (segment,
// kana) that found none is not tried again, and a split that takes more than
// kSplitSteps steps is none, so no text makes this slow; no recursion, so no
// text runs the stack out either.
constexpr std::size_t kSplitSteps = 1u << 22;

bool split(std::u16string_view surface, const std::vector<Segment> &segs, std::u16string_view reading,
           std::vector<std::u16string> &out)
{
    std::vector<std::u16string> kana(segs.size());
    for (std::size_t k = 0; k < segs.size(); ++k)
        if (!segs[k].kanji)
            kana[k] = hiragana(surface.substr(segs[k].begin, segs[k].end - segs[k].begin));
    // at[k]: where segment k starts in the reading; taken[k]: what a kanji one takes
    std::vector<std::size_t> at(segs.size() + 1, 0), taken(segs.size(), 0);
    std::unordered_set<std::uint64_t> failed;
    const auto place = [&](std::size_t k) { return std::uint64_t(k) * (reading.size() + 1) + at[k]; };
    std::size_t k = 0;
    bool again = false; // segment k is tried again, a kanji run taking one kana more
    for (std::size_t steps = 0;; ++steps) {
        if (steps == kSplitSteps)
            return false;
        bool on = false;
        if (k == segs.size()) {
            if (at[k] == reading.size())
                break;
        } else if (again) {
            if (segs[k].kanji && at[k] + taken[k] < reading.size()) {
                at[k + 1] = at[k] + ++taken[k];
                on = true;
            }
        } else if (!failed.contains(place(k))) {
            if (!segs[k].kanji && reading.substr(at[k]).starts_with(kana[k])) {
                at[k + 1] = at[k] + kana[k].size();
                on = true;
            } else if (segs[k].kanji && at[k] < reading.size()) {
                taken[k] = 1;
                at[k + 1] = at[k] + 1;
                on = true;
            }
        }
        if (on) {
            ++k;
            again = false;
            continue;
        }
        failed.insert(place(k));
        if (k == 0)
            return false;
        --k;
        again = true;
    }
    for (std::size_t s = 0; s < segs.size(); ++s)
        if (segs[s].kanji)
            out.emplace_back(reading.substr(at[s], taken[s]));
    return true;
}

// The pieces of a Line's text the analyser reads: outside override blocks,
// between \N \n \h.
std::vector<std::pair<std::size_t, std::size_t>> plainPieces(std::u16string_view text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t start = 0;
    const auto close = [&](std::size_t end) {
        if (end > start)
            out.emplace_back(start, end);
    };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == u'{') {
            const auto shut = text.find(u'}', i);
            if (shut != std::u16string_view::npos) {
                close(i);
                start = i = shut + 1;
                continue;
            }
        } else if (text[i] == u'\\' && i + 1 < text.size() &&
                   (text[i + 1] == u'N' || text[i + 1] == u'n' || text[i + 1] == u'h')) {
            close(i);
            start = i = i + 2;
            continue;
        }
        ++i;
    }
    close(text.size());
    return out;
}

// One word's runs, at `at` in the text.
void wordRuns(std::u16string_view surface, std::u16string_view reading, std::size_t at, std::vector<FuriganaRun> &out)
{
    const auto segs = segments(surface);
    const std::u16string kana = hiragana(reading);
    const auto run = [&](std::size_t begin, std::size_t end, std::u16string suggested) {
        out.push_back({at + begin, at + end, std::u16string(surface.substr(begin, end - begin)), std::move(suggested),
                       std::nullopt});
    };
    std::vector<std::u16string> parts;
    if (kana.empty() || split(surface, segs, kana, parts)) {
        std::size_t part = 0;
        for (const Segment &seg : segs)
            if (seg.kanji)
                run(seg.begin, seg.end, kana.empty() ? std::u16string() : std::move(parts[part++]));
        return;
    }
    // No split fits: one run from the first kanji to the last, less the kana
    // before and after it where the reading spells them.
    const std::size_t first = segs.front().kanji ? 0 : 1, last = segs.back().kanji ? segs.size() - 1 : segs.size() - 2;
    std::u16string_view rest = kana;
    if (first == 1)
        if (const auto lead = hiragana(surface.substr(0, segs[0].end)); rest.starts_with(lead) && rest.size() > lead.size())
            rest.remove_prefix(lead.size());
    if (last + 1 < segs.size())
        if (const auto tail = hiragana(surface.substr(segs.back().begin)); rest.ends_with(tail) && rest.size() > tail.size())
            rest.remove_suffix(tail.size());
    run(segs[first].begin, segs[last].end, std::u16string(rest));
}

} // namespace

bool isKanji(char32_t c)
{
    return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0x20000 && c <= 0x2FA1F) || (c >= 0x30000 && c <= 0x323AF) || c == U'々' || c == U'〆' ||
           c == U'〇' || c == U'ヵ' || c == U'ヶ';
}

std::u16string hiragana(std::u16string_view text)
{
    std::u16string out(text);
    for (char16_t &c : out)
        if ((c >= u'ァ' && c <= u'ヶ') || c == u'ヽ' || c == u'ヾ')
            c = char16_t(c - 0x60);
    return out;
}

std::optional<std::vector<std::u16string>> splitReading(std::u16string_view surface, std::u16string_view reading)
{
    std::vector<std::u16string> out;
    if (!split(surface, segments(surface), hiragana(reading), out))
        return std::nullopt;
    return out;
}

std::vector<FuriganaRun> suggestReadings(std::u16string_view text, const ReadingAnalyzer &analyze)
{
    std::vector<FuriganaRun> out;
    if (!analyze)
        return out;
    for (const auto &[begin, end] : plainPieces(text)) {
        const std::u16string_view piece = text.substr(begin, end - begin);
        if (!hasKanji(piece))
            continue;
        std::size_t cursor = 0;
        for (const ReadingWord &word : analyze(piece)) {
            const auto at = word.surface.empty() ? std::u16string_view::npos : piece.find(word.surface, cursor);
            if (at == std::u16string_view::npos)
                continue;
            cursor = at + word.surface.size();
            if (hasKanji(word.surface))
                wordRuns(word.surface, word.reading, begin + at, out);
        }
    }
    return out;
}

std::vector<FuriganaRun> keepTypedReadings(std::vector<FuriganaRun> fresh, const std::vector<FuriganaRun> &previous)
{
    std::size_t next = 0;
    for (FuriganaRun &run : fresh)
        for (std::size_t j = next; j < previous.size(); ++j)
            if (previous[j].base == run.base) {
                run.typed = previous[j].typed;
                next = j + 1;
                break;
            }
    return fresh;
}

std::vector<std::u16string> morae(std::u16string_view kana)
{
    std::vector<std::u16string> out;
    for (const char16_t c : kana)
        if (isSmallKana(c) && !out.empty())
            out.back() += c;
        else
            out.emplace_back(1, c);
    return out;
}

std::vector<FuriganaSyllable> runSyllables(std::u16string_view base, const std::vector<std::u16string> &pieces,
                                           char16_t sequenceBreak)
{
    if (pieces.empty())
        return {{std::u16string(base), {}, 0}};
    std::vector<FuriganaSyllable> out;
    out.push_back({std::u16string(base), pieces.front(), sequenceBreak});
    for (std::size_t i = 1; i < pieces.size(); ++i)
        out.push_back({u"#", pieces[i], 0});
    return out;
}

std::u16string syllableText(const FuriganaSyllable &syllable)
{
    if (syllable.reading.empty())
        return syllable.base;
    std::u16string out = syllable.base + u'|';
    if (syllable.sequenceBreak)
        out += syllable.sequenceBreak;
    return out + syllable.reading;
}

FuriganaSyllable parseSyllable(std::u16string_view text)
{
    FuriganaSyllable out;
    const auto bar = text.find_first_of(u"|｜");
    out.base = text.substr(0, bar);
    if (out.base == u"＃")
        out.base = u"#";
    if (bar == std::u16string_view::npos)
        return out;
    std::u16string_view reading = text.substr(bar + 1);
    if (!reading.empty()) {
        const char16_t c = reading.front();
        if (c == u'!' || c == u'！' || c == u'<' || c == u'＜') {
            out.sequenceBreak = c == u'!' || c == u'！' ? u'!' : u'<';
            reading.remove_prefix(1);
        }
    }
    out.reading = reading;
    return out;
}

} // namespace hikari::application
