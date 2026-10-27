#include "hikari/application/kanji_timer.h"

#include "hikari/application/kana.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

constexpr std::size_t kHistory = 500;
// and their size at most (the oldest go first; the last always stays)
constexpr std::size_t kHistoryBytes = std::size_t{64} << 20;

// Aegisub's: a kanji is read as five kana at most (uketamawa.ru), and a kana
// is looked for at most three characters ahead.
constexpr std::size_t kLongestReading = 5;
constexpr std::size_t kLookahead = 3;

// Where the karaoke tag at `at` (the backslash) ends: \k, \kf, \ko or \K and
// its time (a sign, digits, a fraction); `at` when it is none (\kt, \keep).
std::size_t karaokeTagEnd(u16v text, std::size_t at)
{
    std::size_t i = at + 1;
    if (i >= text.size() || (text[i] != u'k' && text[i] != u'K'))
        return at;
    const bool upper = text[i] == u'K';
    ++i;
    if (!upper && i < text.size() && (text[i] == u'f' || text[i] == u'o'))
        ++i;
    std::size_t number = i;
    if (number < text.size() && (text[number] == u'-' || text[number] == u'+'))
        ++number;
    const std::size_t digits = number;
    while (number < text.size() && ((text[number] >= u'0' && text[number] <= u'9') || text[number] == u'.'))
        ++number;
    if (number == digits) // no time: only a bare tag at the block's end or before the next one
        return i < text.size() && (text[i] == u'\\' || text[i] == u'}') ? i : at;
    return number;
}

// The character as matching compares it: lower case, a Latin vowel's mark
// taken off, katakana as hiragana (Aegisub compares by ICU's primary
// strength).
char16_t fold(char16_t c, const KaraokeCharClass &classes)
{
    c = classes.lower ? classes.lower(c) : c;
    static constexpr std::array<std::pair<u16v, char16_t>, 5> marked{
        {{u"āâáàä", u'a'}, {u"īîíìï", u'i'}, {u"ūûúùü", u'u'}, {u"ēêéèë", u'e'}, {u"ōôóòö", u'o'}}};
    for (const auto &[set, letter] : marked)
        if (set.find(c) != u16v::npos)
            return letter;
    return toHiragana(c);
}

u16 folded(u16v text, const KaraokeCharClass &classes)
{
    u16 out;
    out.reserve(text.size());
    for (char16_t c : text)
        out += fold(c, classes);
    return out;
}

bool spaceUnit(u16v unit, const KaraokeCharClass &classes)
{
    if (unit == u"\\N" || unit == u"\\n" || unit == u"\\h")
        return true;
    return !unit.empty() && std::ranges::all_of(unit, [&](char16_t c) { return classes.space(c); });
}

// The length of the space at `at`: a space character, or \N, \n or \h
// (the line breaks and hard space a syllable keeps); 0 for none.
std::size_t spaceAt(u16v text, std::size_t at, const KaraokeCharClass &classes)
{
    if (at < text.size() && classes.space(text[at]))
        return 1;
    if (at + 1 < text.size() && text[at] == u'\\' && (text[at + 1] == u'N' || text[at + 1] == u'n' || text[at + 1] == u'h'))
        return 2;
    return 0;
}

bool allSpace(u16v text, const KaraokeCharClass &classes)
{
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t n = spaceAt(text, i, classes);
        if (!n)
            return false;
        i += n;
    }
    return true;
}

// The characters' text from `at` on, for the kana readings.
// The characters from `at` that a kana reading can take (kanaReadings reads
// a small っ, the kana after it and a small ゃ at most), not the rest of the
// Line: a long Line would make each syllable copy all of it.
constexpr std::size_t kReadingWindow = 3;
u16 charactersFrom(const KanjiText &kanji, std::size_t at)
{
    u16 out;
    for (std::size_t i = at; i < kanji.units.size() && i < at + kReadingWindow; ++i)
        out += kanji.units[i].text;
    return out;
}

std::size_t syllablesIn(const std::vector<KanjiGroup> &groups)
{
    std::size_t n = 0;
    for (const auto &g : groups)
        n += g.syllables;
    return n;
}

std::size_t charactersIn(const std::vector<KanjiGroup> &groups)
{
    std::size_t n = 0;
    for (const auto &g : groups)
        n += g.characters;
    return n;
}

} // namespace

bool hasKaraokeTags(u16v text)
{
    bool inBlock = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'{')
            inBlock = true;
        else if (text[i] == u'}')
            inBlock = false;
        else if (inBlock && text[i] == u'\\') {
            const std::size_t end = karaokeTagEnd(text, i);
            if (end != i && end > 0 && text[end - 1] >= u'0' && text[end - 1] <= u'9')
                return true;
        }
    }
    return false;
}

std::vector<KanjiSyllable> kanjiSyllables(u16v text, int startMs, int endMs)
{
    AudioKaraoke karaoke;
    karaoke.splitTags({u16(text), startMs, endMs});
    std::vector<KanjiSyllable> out;
    int previous = startMs;
    for (int i = 0; i < karaoke.count(); ++i) {
        const int end = karaoke.times()[static_cast<std::size_t>(i)];
        const double cs = (static_cast<double>(end) - previous) / 10.0;
        out.push_back({karaoke.stripped(i), static_cast<int>(std::lround(cs)), karaoke.tags()[static_cast<std::size_t>(i)]});
        previous = end;
    }
    return out;
}

namespace {

// The characters of a text with no \k tags (KanjiText's units).
std::vector<KanjiText::Unit> unitsOf(u16v t)
{
    std::vector<KanjiText::Unit> units;
    std::size_t i = 0;
    while (i < t.size()) {
        if (t[i] == u'{') {
            const std::size_t close = t.find(u'}', i);
            if (close == u16v::npos)
                break; // KA1: an unclosed block to the end is no text
            i = close + 1;
            continue;
        }
        const std::size_t end = std::max(karaokeLetterEnd(t, i), i + 1);
        KanjiText::Unit unit{i, end, {}};
        for (std::size_t j = i; j < end;) {
            if (t[j] == u'{') {
                const std::size_t close = t.find(u'}', j);
                j = close == u16v::npos ? end : close + 1;
                continue;
            }
            unit.text += t[j++];
        }
        units.push_back(std::move(unit));
        i = end;
    }
    return units;
}

// KA4-furigana: a unit that is a kanji (its first code point), or kana.
bool kanjiUnit(const KanjiText::Unit &unit)
{
    const u16 &t = unit.text;
    if (t.empty())
        return false;
    if (t.size() > 1 && t[0] >= 0xD800 && t[0] < 0xDC00 && t[1] >= 0xDC00 && t[1] < 0xE000)
        return isKanji(0x10000 + ((char32_t(t[0]) - 0xD800) << 10) + (char32_t(t[1]) - 0xDC00));
    return isKanji(t[0]);
}

bool kanaUnit(const KanjiText::Unit &unit)
{
    return !unit.text.empty() && isKana(unit.text[0]);
}

// The runs of kanji units in [from, to).
std::vector<std::pair<std::size_t, std::size_t>> kanjiRunsIn(const std::vector<KanjiText::Unit> &units,
                                                             std::size_t from, std::size_t to)
{
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    for (std::size_t u = from; u < to; ++u) {
        if (!kanjiUnit(units[u]))
            continue;
        if (!runs.empty() && runs.back().second == u)
            ++runs.back().second;
        else
            runs.emplace_back(u, u + 1);
    }
    return runs;
}

// A reading split over the kanji runs of [from, to), its kana spelt
// between them (the units that are neither left out).
std::optional<std::vector<u16>> splitOver(const std::vector<KanjiText::Unit> &units, std::size_t from,
                                          std::size_t to, u16v reading)
{
    u16 surface;
    for (std::size_t u = from; u < to; ++u)
        if (kanjiUnit(units[u]) || kanaUnit(units[u]))
            surface += units[u].text;
    auto parts = splitReading(surface, reading);
    if (!parts || parts->size() != kanjiRunsIn(units, from, to).size())
        return std::nullopt;
    return parts;
}

} // namespace

KanjiText kanjiText(u16v text)
{
    KanjiText out;
    // the \k tags out of every block, and a block they leave empty; where
    // each was, in the text left
    std::vector<std::size_t> tags;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != u'{') {
            out.text += text[i++];
            continue;
        }
        const std::size_t close = text.find(u'}', i);
        const std::size_t end = close == u16v::npos ? text.size() : close + 1;
        u16 block;
        std::size_t removed = 0;
        for (std::size_t j = i; j < end;) {
            if (text[j] == u'\\') {
                if (const std::size_t tagEnd = karaokeTagEnd(text.substr(0, end), j); tagEnd != j) {
                    j = tagEnd;
                    ++removed;
                    continue;
                }
            }
            block += text[j++];
        }
        if (!(removed && (block == u"{}" || block == u"{")))
            out.text += block;
        tags.insert(tags.end(), removed, out.text.size());
        i = end;
    }
    // KA4-furigana: each syllable's reading out of the text (base|reading,
    // the full-width forms, a ! or < after the | dropped), in one pass; a
    // syllable with none is kept too (none), as a # highlight after it reads on
    struct Read {
        std::size_t syllable, begin, end;
        u16 text;
        bool hash, none = false;
    };
    std::vector<Read> read;
    bool any = false; // a syllable has a reading
    {
        u16 kept;
        std::vector<std::size_t> moved(tags.size());
        std::size_t copied = 0, gone = 0;
        for (std::size_t t = 0; t < tags.size(); ++t) {
            const std::size_t from = tags[t], to = t + 1 < tags.size() ? tags[t + 1] : out.text.size();
            moved[t] = from - gone;
            auto skipBlock = [&](std::size_t j) {
                const std::size_t close = out.text.find(u'}', j);
                return close == u16::npos || close >= to ? to : close + 1;
            };
            std::size_t bar = u16::npos, characters = 0;
            char16_t sole = 0;
            for (std::size_t j = from; j < to && bar == u16::npos;) {
                if (out.text[j] == u'{') {
                    j = skipBlock(j);
                } else if (out.text[j] == u'|' || out.text[j] == u'｜') {
                    bar = j;
                } else {
                    ++characters;
                    sole = out.text[j++];
                }
            }
            // (a # highlight: its one character outside blocks a #)
            const bool hash = t > 0 && characters == 1 && (sole == u'#' || sole == u'＃');
            if (bar == u16::npos) {
                read.push_back({t, from - gone, to - gone, u16(), hash, true});
                continue;
            }
            any = true;
            u16 reading, blocks;
            for (std::size_t j = bar + 1; j < to;) {
                if (out.text[j] == u'{') {
                    const std::size_t close = skipBlock(j);
                    blocks.append(out.text, j, close - j);
                    j = close;
                } else {
                    reading += out.text[j++];
                }
            }
            if (!reading.empty() && u16v(u"!<！＜").find(reading.front()) != u16v::npos)
                reading.erase(0, 1);
            if (kept.empty())
                kept.reserve(out.text.size());
            kept.append(out.text, copied, bar - copied);
            kept += blocks;
            copied = to;
            read.push_back({t, from - gone, bar - gone, std::move(reading), hash});
            gone += to - bar - blocks.size();
        }
        if (any) {
            kept.append(out.text, copied);
            out.text = std::move(kept);
            tags = std::move(moved);
        }
    }
    // a # syllable's reading goes on the base before it, through the #
    // highlights with none (karaskel keeps the base over every one); a base
    // that reads nothing is no reading
    {
        std::vector<Read> merged;
        for (Read &r : read) {
            if (!r.hash) {
                if (!merged.empty() && merged.back().none)
                    merged.pop_back();
                merged.push_back(std::move(r));
            } else if (!merged.empty() && merged.back().syllable + 1 == r.syllable) {
                merged.back().text += r.text;
                merged.back().none = merged.back().none && r.none;
                merged.back().syllable = r.syllable;
            }
        }
        if (!merged.empty() && merged.back().none)
            merged.pop_back();
        read = any ? std::move(merged) : std::vector<Read>();
    }
    // the readings by the kanji runs of their bases' units
    // (in order: one pass over the units)
    auto finish = [&] {
        std::size_t from = 0;
        for (const Read &r : read) {
            while (from < out.units.size() && out.units[from].begin < r.begin)
                ++from;
            std::size_t to = from;
            while (to < out.units.size() && out.units[to].end <= r.end)
                ++to;
            const auto runs = kanjiRunsIn(out.units, from, to);
            if (runs.empty())
                continue;
            // (split in hiragana; each part as the Line writes it)
            const auto parts = splitOver(out.units, from, to, r.text);
            std::size_t at = 0, k = 0;
            for (std::size_t u = from; u < to && parts && k < runs.size(); ++u) {
                if (u == runs[k].first) {
                    out.readings.push_back({runs[k].first, runs[k].second, r.text.substr(at, (*parts)[k].size())});
                    at += (*parts)[k++].size();
                    u = runs[k - 1].second - 1;
                } else if (kanaUnit(out.units[u])) {
                    at += out.units[u].text.size();
                }
            }
            if (!parts)
                out.readings.push_back({runs[0].first, runs[0].second, r.text});
        }
    };
    out.units = unitsOf(out.text);
    // KA8-multi-highlight: the # syllables, one character each up to the next tag
    std::vector<bool> hash(tags.size());
    std::vector<std::size_t> erased; // their units, in order
    for (std::size_t t = 1, u = 0; t < tags.size(); ++t) {
        while (u < out.units.size() && out.units[u].begin < tags[t])
            ++u;
        const std::size_t next = t + 1 < tags.size() ? tags[t + 1] : u16::npos;
        if (u < out.units.size() && (out.units[u].text == u"#" || out.units[u].text == u"＃") &&
            (u + 1 == out.units.size() || out.units[u + 1].begin >= next) && out.units[u].begin < next) {
            hash[t] = true;
            erased.push_back(u);
        }
    }
    if (erased.empty()) {
        finish();
        return out;
    }
    // the text without them, and each tag moved back by what went before it,
    // in one pass each (a Line of many # syllables must not take n² here)
    u16 kept;
    kept.reserve(out.text.size());
    std::size_t copied = 0;
    for (const std::size_t u : erased) {
        kept.append(out.text, copied, out.units[u].begin - copied);
        copied = out.units[u].end;
    }
    kept.append(out.text, copied);
    std::size_t gone = 0;
    for (std::size_t t = 0, e = 0; t < tags.size(); ++t) {
        for (; e < erased.size() && out.units[erased[e]].end <= tags[t]; ++e)
            gone += out.units[erased[e]].end - out.units[erased[e]].begin;
        tags[t] -= gone;
    }
    gone = 0;
    for (std::size_t r = 0, e = 0; r < read.size(); ++r) {
        for (; e < erased.size() && out.units[erased[e]].end <= read[r].begin; ++e)
            gone += out.units[erased[e]].end - out.units[erased[e]].begin;
        read[r].begin -= gone;
        read[r].end -= gone;
    }
    out.text = std::move(kept);
    out.units = unitsOf(out.text);
    // the groups the tags make, when no character comes before the first
    if (!out.units.empty() && out.units.front().begin < tags.front()) {
        finish();
        return out;
    }
    std::size_t u = 0;
    for (std::size_t t = 0; t < tags.size(); ++t) {
        if (hash[t]) {
            ++out.tagged.back().syllables;
            continue;
        }
        std::size_t next = t + 1;
        while (next < tags.size() && hash[next])
            ++next;
        KanjiGroup g{1, 0};
        for (; u < out.units.size() && (next == tags.size() || out.units[u].begin < tags[next]); ++u)
            ++g.characters;
        out.tagged.push_back(g);
    }
    finish();
    return out;
}

KanjiGroup suggestGroup(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                        std::size_t at, const KaraokeCharClass &classes, bool *ranOut)
{
    if (ranOut)
        *ranOut = false;
    if (from >= syllables.size())
        return {0, 0};
    KanjiGroup g{1, 0};
    const std::size_t n = kanji.units.size();
    const u16 src = folded(syllables[from].text, classes);
    std::size_t si = 0, di = at;
    // past the spaces of both; true when this group is over
    auto eatSpace = [&] {
        while (const std::size_t space = spaceAt(src, si, classes))
            si += space;
        while (di < n && spaceUnit(kanji.units[di].text, classes)) {
            ++di;
            ++g.characters;
        }
        if (di >= n) { // no characters left: every syllable left is this group's
            g.syllables = syllables.size() - from;
            if (ranOut && g.syllables > 1)
                *ranOut = true;
            return true;
        }
        return si >= src.size();
    };
    if (eatSpace())
        return g;
    while (si < src.size()) {
        // the same letters
        const u16 unit = folded(kanji.units[di].text, classes);
        if (!unit.empty() && u16v(src).substr(si).starts_with(unit)) {
            si += unit.size();
            ++di;
            ++g.characters;
            if (eatSpace())
                return g;
            continue;
        }
        // a kana by its reading
        // (the most kana, and of the same kana the longest romaji: n' before n)
        const KanaReading *best = nullptr;
        const u16 rest = charactersFrom(kanji, di);
        const auto readings = kanaReadings(rest);
        for (const KanaReading &reading : readings)
            if (u16v(src).substr(si).starts_with(reading.romaji) &&
                (!best || std::pair(reading.length, reading.romaji.size()) > std::pair(best->length, best->romaji.size())))
                best = &reading;
        if (!best)
            break;
        si += best->romaji.size();
        for (std::size_t taken = 0; taken < best->length && di < n; ++di, ++g.characters)
            taken += kanji.units[di].text.size();
        if (eatSpace())
            return g;
    }
    // stuck at a character: the last one takes every syllable left
    if (di + 1 == n) {
        g.syllables = syllables.size() - from;
        ++g.characters;
        return g;
    }
    // the next kana within kLookahead characters, at the start of a later syllable
    for (std::size_t ahead = 0; ahead < kLookahead; ++ahead) {
        const std::size_t j = di + ahead + 1;
        if (j >= n)
            break;
        const u16v unit = kanji.units[j].text;
        if (unit.size() != 1 || !isKana(unit[0]))
            continue;
        std::vector<u16> readings{folded(unit, classes)};
        for (const KanaReading &reading : kanaReadings(charactersFrom(kanji, j)))
            readings.push_back(reading.romaji);
        const std::size_t most = (ahead + 1) * kLongestReading;
        std::size_t position = 0;
        for (std::size_t k = from; k < syllables.size(); ++k) {
            if (allSpace(syllables[k].text, classes))
                continue; // a blank syllable counts for nothing
            if (++position == 1)
                continue;
            if (position > most)
                break;
            const u16 syllable = folded(syllables[k].text, classes);
            if (std::ranges::none_of(readings, [&](const u16 &r) { return !r.empty() && u16v(syllable).starts_with(r); }))
                continue;
            if (position == 2) { // the next syllable: everything before it is this one's
                g.characters += ahead + 1;
                g.syllables = k - from;
                return g;
            }
            // further on: the characters shared evenly by the syllables (as
            // many with text, with the blank ones among them)
            g.characters += 1;
            const auto shared = static_cast<std::size_t>((static_cast<double>(position) - 1.0) / (static_cast<double>(ahead) + 1.0) + 0.5);
            g.syllables = 0;
            for (std::size_t counted = 0; counted < shared; ++g.syllables)
                if (!allSpace(syllables[from + g.syllables].text, classes))
                    ++counted;
            return g;
        }
    }
    g.characters = std::max<std::size_t>(g.characters, 1);
    return g;
}

KanjiSuggestion suggestGroups(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                              std::size_t at, const KaraokeCharClass &classes)
{
    KanjiSuggestion out;
    while (from < syllables.size()) {
        bool ranOut = false;
        KanjiGroup g = suggestGroup(syllables, from, kanji, at, classes, &ranOut);
        if (ranOut)
            out.clean = false;
        g.syllables = std::clamp<std::size_t>(g.syllables, 1, syllables.size() - from);
        g.characters = std::min(g.characters, kanji.units.size() - std::min(at, kanji.units.size()));
        if (g.characters == 0)
            out.clean = false;
        out.groups.push_back(g);
        from += g.syllables;
        at += g.characters;
    }
    for (; at < kanji.units.size(); ++at)
        if (!spaceUnit(kanji.units[at].text, classes))
            out.clean = false;
    return out;
}

namespace {

// The kana `count` syllables from `from` sing: their romaji (ASCII marks
// left out) read as kana; none when part of it is no romaji.
std::optional<u16> sungKana(const std::vector<KanjiSyllable> &syllables, std::size_t from, std::size_t count)
{
    u16 romaji;
    for (std::size_t k = from; k < from + count && k < syllables.size(); ++k)
        for (const char16_t c : syllables[k].text)
            if (c >= 128 || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'\'' || c == u' ')
                romaji += c;
    if (romaji.empty())
        return std::nullopt;
    return romajiToHiragana(romaji);
}

// The dictionary's reading of a kanji run: its words' runs within it when
// they take every kanji of it. `word`: the first word not before it (the
// runs are asked in order).
u16 dictionaryReading(const KanjiText &kanji, const std::vector<FuriganaRun> &dictionary, std::size_t &word,
                      std::size_t from, std::size_t to)
{
    const std::size_t begin = kanji.units[from].begin, end = kanji.units[to - 1].end;
    while (word < dictionary.size() && dictionary[word].end <= begin)
        ++word;
    u16 out;
    std::size_t covered = begin;
    for (std::size_t w = word; w < dictionary.size() && dictionary[w].begin < end; ++w) {
        const FuriganaRun &run = dictionary[w];
        if (run.begin < begin || run.end > end)
            return {};
        if (run.suggested.empty())
            return {};
        // (kana between two runs is no kanji of this one)
        if (run.begin != covered)
            return {};
        out += run.suggested;
        covered = run.end;
    }
    return covered == end ? out : u16();
}

// The kanji runs of each group, in order.
std::vector<std::pair<std::size_t, std::size_t>> runsOver(const KanjiText &kanji, const std::vector<KanjiGroup> &groups)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t character = 0;
    for (const KanjiGroup &g : groups) {
        const auto runs = kanjiRunsIn(kanji.units, std::min(character, kanji.units.size()),
                                      std::min(character + g.characters, kanji.units.size()));
        out.insert(out.end(), runs.begin(), runs.end());
        character += g.characters;
    }
    return out;
}

// The typed readings on these runs, so regrouping loses none: the ones in
// a run one after another, one over more runs all on the first (the others
// none), one over no run as it was. (Both in order: one pass.)
KanjiTyped fitTyped(const std::vector<std::pair<std::size_t, std::size_t>> &runs, const KanjiTyped &typed)
{
    KanjiTyped out;
    std::size_t r = 0;
    for (const auto &[range, text] : typed) {
        while (r < runs.size() && runs[r].second <= range.first)
            ++r;
        if (r == runs.size() || runs[r].first >= range.second) {
            out.emplace(range, text);
            continue;
        }
        out[runs[r]] += text;
        while (r + 1 < runs.size() && runs[r + 1].first < range.second)
            out.try_emplace(runs[++r]);
    }
    return out;
}

} // namespace

std::vector<KanjiReading> kanjiReadings(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                                        const std::vector<KanjiGroup> &groups,
                                        const std::vector<FuriganaRun> &dictionary, const KanjiTyped &typed,
                                        bool suggest)
{
    std::vector<KanjiReading> out;
    const KanjiTyped fitted = fitTyped(runsOver(kanji, groups), typed);
    std::size_t syllable = 0, character = 0, word = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const std::size_t from = std::min(character, kanji.units.size());
        const std::size_t to = std::min(character + groups[g].characters, kanji.units.size());
        const auto runs = kanjiRunsIn(kanji.units, from, to);
        std::optional<std::vector<u16>> parts;
        if (suggest && !runs.empty())
            if (const auto sung = sungKana(syllables, syllable, groups[g].syllables))
                parts = splitOver(kanji.units, from, to, *sung);
        for (std::size_t r = 0; r < runs.size(); ++r) {
            KanjiReading k{g, runs[r].first, runs[r].second};
            if (suggest) {
                k.dictionary = dictionaryReading(kanji, dictionary, word, runs[r].first, runs[r].second);
                k.sung = parts.has_value();
                k.suggested = parts ? (*parts)[r] : k.dictionary;
            }
            if (const auto found = fitted.find(runs[r]); found != fitted.end())
                k.typed = found->second;
            if (suggest || k.typed)
                out.push_back(std::move(k));
        }
        syllable += groups[g].syllables;
        character += groups[g].characters;
    }
    for (KanjiReading &k : out)
        k.differs = k.sung && !k.dictionary.empty() && k.dictionary != k.suggested;
    // a word over more runs, one after another (both in order)
    std::size_t first = 0;
    for (const FuriganaRun &word : dictionary) {
        if (!suggest || word.suggested.empty())
            continue;
        while (first < out.size() && kanji.units[out[first].begin].begin < word.begin)
            ++first;
        std::size_t last = first;
        u16 sung;
        bool allSung = true;
        while (last < out.size() && kanji.units[out[last].end - 1].end <= word.end &&
               kanji.units[out[last].begin].begin == (last == first ? word.begin : kanji.units[out[last - 1].end - 1].end)) {
            sung += out[last].suggested;
            allSung = allSung && out[last].sung;
            ++last;
        }
        if (last - first < 2 || kanji.units[out[last - 1].end - 1].end != word.end)
            continue;
        for (std::size_t r = first; r < last; ++r) {
            out[r].dictionary = word.suggested;
            out[r].differs = allSung && sung != word.suggested;
        }
    }
    return out;
}

namespace {

// KA4-furigana: the groups as kanjiOutput writes them and each one's reading
// pieces (none: no furigana; one per syllable: the first after the base,
// the others after its # syllables).
struct Furigana {
    std::vector<KanjiGroup> groups;
    std::vector<std::vector<u16>> pieces;
};

Furigana furiganaOf(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                    const std::vector<KanjiGroup> &groups, bool multi, const std::vector<KanjiReading> &readings)
{
    Furigana out;
    std::size_t syllable = 0, character = 0, next = 0;
    // a group's pieces: its reading (its kana spelt between the kanji runs'
    // readings), per syllable when the reading is the sung one
    auto piecesOf = [&](std::size_t from, std::size_t count, std::size_t at, std::size_t to,
                        const std::vector<const KanjiReading *> &runs) -> std::vector<u16> {
        u16 whole;
        bool any = false;
        std::size_t r = 0;
        for (std::size_t u = at; u < to; ++u) {
            if (r < runs.size() && u == runs[r]->begin) {
                whole += runs[r]->reading();
                any = any || !runs[r]->reading().empty();
                u = runs[r++]->end - 1;
            } else if (kanaUnit(kanji.units[u])) {
                whole += kanji.units[u].text;
            }
        }
        if (!any)
            return {};
        if (multi && count > 1 && to > at) {
            std::vector<u16> each;
            u16 sung;
            for (std::size_t k = from; k < from + count; ++k) {
                const auto kana = sungKana(syllables, k, 1);
                if (!kana || kana->empty())
                    return {whole};
                sung += *kana;
                each.push_back(*kana);
            }
            if (hiragana(whole) == sung)
                return each;
        }
        return {whole};
    };
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const KanjiGroup &g = groups[i];
        const std::size_t at = std::min(character, kanji.units.size());
        // the last group's characters are the rest of the Line (KA3-leftover-kanji)
        const std::size_t to = i + 1 == groups.size() ? kanji.units.size() : std::min(character + g.characters, kanji.units.size());
        std::vector<const KanjiReading *> runs;
        for (; next < readings.size() && readings[next].group <= i; ++next)
            if (readings[next].group == i)
                runs.push_back(&readings[next]);
        // a group of kanji and kana, sung as its runs' suggestions, whose
        // syllables line up with them: one group per stretch of kanji or not
        bool split = false;
        if (!runs.empty() && g.syllables > 1 && std::ranges::all_of(runs, [](const auto *r) { return r->sung; })) {
            // each stretch's units and kana
            std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> stretches; // from, to, kana
            const std::size_t own = std::min(at + g.characters, kanji.units.size());
            std::size_t r = 0;
            for (std::size_t u = at; u < own;) {
                std::size_t end = u, kana = 0;
                if (r < runs.size() && u == runs[r]->begin) {
                    end = runs[r]->end;
                    kana = runs[r++]->suggested.size();
                } else {
                    while (end < own && !(r < runs.size() && end == runs[r]->begin)) {
                        if (kanaUnit(kanji.units[end]))
                            kana += hiragana(kanji.units[end].text).size();
                        ++end;
                    }
                }
                // (a stretch with no kana goes with the one before)
                if (kana == 0 && !stretches.empty())
                    std::get<1>(stretches.back()) = end;
                else if (!stretches.empty() && std::get<2>(stretches.back()) == 0)
                    stretches.back() = {std::get<0>(stretches.back()), end, kana};
                else
                    stretches.emplace_back(u, end, kana);
                u = end;
            }
            std::vector<std::size_t> counts; // the syllables of each stretch
            std::size_t k = syllable, sungSoFar = 0, stretchEnd = 0;
            bool lined = stretches.size() > 1;
            for (const auto &[from, end, kana] : stretches) {
                stretchEnd += kana;
                std::size_t n = 0;
                while (lined && sungSoFar < stretchEnd && k < syllable + g.syllables) {
                    const auto sung = sungKana(syllables, k++, 1);
                    if (!sung || sung->empty())
                        lined = false;
                    else
                        sungSoFar += sung->size();
                    ++n;
                }
                lined = lined && sungSoFar == stretchEnd && n > 0;
                counts.push_back(n);
            }
            if (lined && k == syllable + g.syllables) {
                split = true;
                std::size_t s = syllable, rr = 0;
                for (std::size_t t = 0; t < stretches.size(); ++t) {
                    const auto &[from, end, kana] = stretches[t];
                    std::vector<const KanjiReading *> its;
                    for (; rr < runs.size() && runs[rr]->begin < end; ++rr)
                        its.push_back(runs[rr]);
                    out.groups.push_back({counts[t], end - from});
                    out.pieces.push_back(its.empty() ? std::vector<u16>{} : piecesOf(s, counts[t], from, end, its));
                    s += counts[t];
                }
            }
        }
        if (!split) {
            out.groups.push_back(g);
            out.pieces.push_back(runs.empty() ? std::vector<u16>{} : piecesOf(syllable, g.syllables, at, to, runs));
        }
        syllable += g.syllables;
        character += g.characters;
    }
    return out;
}

} // namespace

std::u16string kanjiOutput(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                           const std::vector<KanjiGroup> &written, bool multi, const std::vector<KanjiReading> *readings)
{
    Furigana furigana;
    if (readings && !readings->empty())
        furigana = furiganaOf(syllables, kanji, written, multi, *readings);
    const std::vector<KanjiGroup> &groups = furigana.groups.empty() ? written : furigana.groups;
    // each group's tag and where it goes; a # syllable's (KA8-multi-highlight)
    // with its "#" after the group's characters
    // and KA4-furigana's readings, each after its base
    struct Tag {
        std::size_t at;
        u16 tag;
        bool hash;
        bool reading = false;
    };
    std::vector<Tag> tags;
    auto tagOf = [&](std::size_t syllable, int cs) {
        const u16 letters = syllable < syllables.size() && !syllables[syllable].tag.empty() ? syllables[syllable].tag : u16(u"k");
        const auto number = std::to_string(cs);
        return u"\\" + letters + u16(number.begin(), number.end());
    };
    std::size_t syllable = 0, character = 0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const KanjiGroup &g = groups[i];
        const std::size_t end = std::min(syllable + g.syllables, syllables.size());
        const bool hashes = multi && g.characters > 0 && end > syllable + 1;
        int cs = 0;
        for (std::size_t k = syllable; k < (hashes ? syllable + 1 : end); ++k)
            cs += syllables[k].cs;
        const std::size_t at = character < kanji.units.size() ? kanji.units[character].begin : kanji.text.size();
        tags.push_back({at, tagOf(syllable, cs), false});
        character += g.characters;
        // the last group's characters are the rest of the Line (KA3-leftover-kanji)
        const std::size_t last = i + 1 == groups.size() ? kanji.units.size() : std::min(character, kanji.units.size());
        const std::size_t after = last ? kanji.units[last - 1].end : 0;
        const std::vector<u16> none;
        const auto &pieces = i < furigana.pieces.size() ? furigana.pieces[i] : none;
        const bool each = hashes && pieces.size() == end - syllable;
        if (!pieces.empty() && g.characters > 0)
            tags.push_back({after, pieces.front(), false, true});
        if (hashes) {
            for (std::size_t k = syllable + 1; k < end; ++k) {
                tags.push_back({after, tagOf(k, syllables[k].cs), true});
                if (each)
                    tags.push_back({after, pieces[k - syllable], false, true});
            }
        }
        syllable += g.syllables;
    }
    u16 out;
    std::size_t copied = 0;
    std::size_t lastAt = u16::npos;
    for (const auto &[at, tag, hash, reading] : tags) {
        out.append(kanji.text, copied, at - copied);
        copied = at;
        if (reading) {
            out += u'|';
            out += tag;
        } else if (hash) {
            out += u'{';
            out += tag;
            out += u"}#";
        } else if (at != lastAt && at > 0 && kanji.text[at - 1] == u'}' && !out.empty() && out.back() == u'}') {
            // into the Line's own block just before it (not one made here)
            out.insert(out.size() - 1, tag);
        } else {
            out += u'{';
            out += tag;
            out += u'}';
        }
        lastAt = at;
    }
    out.append(kanji.text, copied);
    return out;
}

// ---- the window's state -------------------------------------------------------

KanjiTimer::KanjiTimer(KaraokeCharClass classes) : m_classes(std::move(classes)) {}

KanjiTimer::Key KanjiTimer::keyOf(const Pair &pair)
{
    return {pair.romaji ? pair.romaji->id.value : 0, pair.kanji ? pair.kanji->id.value : 0};
}

void KanjiTimer::setLines(std::vector<Line> romaji, std::vector<Line> kanji)
{
    m_romaji = std::move(romaji);
    m_kanji = std::move(kanji);
    derive();
}

void KanjiTimer::setSuggest(bool on)
{
    m_suggest = on;
    derive();
}

void KanjiTimer::setDictionary(ReadingAnalyzer analyze)
{
    m_analyze = std::move(analyze);
    m_dictionary.clear();
}

const std::vector<FuriganaRun> &KanjiTimer::dictionaryOf(const KanjiText &kanji) const
{
    static const std::vector<FuriganaRun> none;
    if (!m_analyze)
        return none;
    auto found = m_dictionary.find(kanji.text);
    if (found == m_dictionary.end())
        found = m_dictionary.emplace(kanji.text, suggestReadings(kanji.text, m_analyze)).first;
    return found->second;
}

std::vector<KanjiReading> KanjiTimer::readingsOf(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                                 const KanjiText &kanji, const std::vector<KanjiGroup> &groups) const
{
    static const std::vector<FuriganaRun> none;
    return kanjiReadings(syllables, kanji, groups, m_furigana ? dictionaryOf(kanji) : none, work.typed, m_furigana);
}

std::u16string KanjiTimer::outputOf(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                    const KanjiText &kanji, const std::vector<KanjiGroup> &groups) const
{
    // (the characters after the last group are its: KA3-leftover-kanji)
    std::vector<KanjiGroup> all = groups;
    if (const std::size_t used = charactersIn(all); !all.empty() && used < kanji.units.size())
        all.back().characters += kanji.units.size() - used;
    const auto readings = readingsOf(work, syllables, kanji, all);
    return kanjiOutput(syllables, kanji, all, m_multi, &readings);
}

namespace {

int overlap(const KanjiTimer::Line &a, const KanjiTimer::Line &b)
{
    return std::min(a.endMs, b.endMs) - std::max(a.startMs, b.startMs);
}

} // namespace

// KA3-pair-by-time: a romaji Line pairs with the kanji Line it overlaps most,
// in time order; without any overlap (an untimed kanji style) by Document
// order, as Aegisub pairs. A skipped Line pairs with none.
void KanjiTimer::derive()
{
    struct Item {
        std::optional<Line> romaji, kanji;
        int startMs = 0;
        std::size_t order = 0;
    };
    std::vector<Item> items;
    std::vector<std::pair<Line, std::size_t>> romaji, kanji;
    for (std::size_t i = 0; i < m_romaji.size(); ++i) {
        if (m_state.skippedRomaji.contains(m_romaji[i].id))
            items.push_back({m_romaji[i], std::nullopt, m_romaji[i].startMs, i * 2});
        else
            romaji.emplace_back(m_romaji[i], i * 2);
    }
    for (std::size_t i = 0; i < m_kanji.size(); ++i) {
        if (m_state.skippedKanji.contains(m_kanji[i].id))
            items.push_back({std::nullopt, m_kanji[i], m_kanji[i].startMs, i * 2 + 1});
        else
            kanji.emplace_back(m_kanji[i], i * 2 + 1);
    }
    // Whether any romaji Line overlaps any kanji Line: the romaji Lines by
    // start with the latest end so far, each kanji Line looked up in them
    // (n log n; a file of many Lines must not take n² here).
    bool byTime = false;
    {
        std::vector<std::pair<int, int>> spans; // (start, end) of the romaji Lines
        for (const auto &r : romaji)
            if (r.first.endMs > r.first.startMs) // (a Line of no length overlaps nothing)
                spans.emplace_back(r.first.startMs, r.first.endMs);
        std::ranges::sort(spans);
        std::vector<int> latestEnd(spans.size());
        for (std::size_t s = 0; s < spans.size(); ++s)
            latestEnd[s] = s ? std::max(latestEnd[s - 1], spans[s].second) : spans[s].second;
        for (const auto &k : kanji) {
            if (k.first.endMs <= k.first.startMs)
                continue;
            // the romaji Lines starting before this one ends
            const auto before = std::ranges::lower_bound(spans, std::pair(k.first.endMs, std::numeric_limits<int>::min()));
            const auto count = static_cast<std::size_t>(before - spans.begin());
            if (count > 0 && latestEnd[count - 1] > k.first.startMs) {
                byTime = true;
                break;
            }
        }
    }
    if (byTime) {
        auto earlier = [](const auto &a, const auto &b) {
            return std::pair(a.first.startMs, a.second) < std::pair(b.first.startMs, b.second);
        };
        std::ranges::stable_sort(romaji, earlier);
        std::ranges::stable_sort(kanji, earlier);
    }
    std::size_t i = 0, j = 0;
    auto alone = [&](bool isRomaji) {
        const auto &[line, order] = isRomaji ? romaji[i++] : kanji[j++];
        items.push_back(isRomaji ? Item{line, std::nullopt, line.startMs, order} : Item{std::nullopt, line, line.startMs, order});
    };
    while (i < romaji.size() && j < kanji.size()) {
        const Line &r = romaji[i].first, &k = kanji[j].first;
        if (byTime) {
            const int o = overlap(r, k);
            if (o <= 0) {
                alone(std::pair(r.startMs, r.endMs) <= std::pair(k.startMs, k.endMs));
                continue;
            }
            // A Line goes alone for the next one only when most of it lies
            // outside its partner (a stray Line just touching it), never
            // when it lies within it.
            const auto mostlyOutside = [o](const Line &line) { return 2 * o < line.endMs - line.startMs; };
            if (i + 1 < romaji.size() && overlap(romaji[i + 1].first, k) > o && mostlyOutside(r)) {
                alone(true);
                continue;
            }
            if (j + 1 < kanji.size() && overlap(r, kanji[j + 1].first) > o && mostlyOutside(k)) {
                alone(false);
                continue;
            }
        }
        items.push_back({r, k, r.startMs, std::min(romaji[i].second, kanji[j].second)});
        ++i;
        ++j;
    }
    while (i < romaji.size())
        alone(true);
    while (j < kanji.size())
        alone(false);
    std::ranges::stable_sort(items, [byTime](const Item &a, const Item &b) {
        return byTime ? std::pair(a.startMs, a.order) < std::pair(b.startMs, b.order) : a.order < b.order;
    });

    m_pairs.clear();
    for (auto &item : items)
        m_pairs.push_back({std::move(item.romaji), std::move(item.kanji), Status::ToDo});
    for (auto &pair : m_pairs)
        pair.status = statusOf(pair);
    // the current pair: the same one, else the first with both Lines
    m_current = -1;
    for (std::size_t p = 0; p < m_pairs.size(); ++p)
        if (m_state.current && keyOf(m_pairs[p]) == *m_state.current)
            m_current = static_cast<int>(p);
    if (m_current < 0 || !m_pairs[static_cast<std::size_t>(m_current)].romaji ||
        !m_pairs[static_cast<std::size_t>(m_current)].kanji) {
        m_current = -1;
        for (std::size_t p = 0; p < m_pairs.size() && m_current < 0; ++p)
            if (m_pairs[p].romaji && m_pairs[p].kanji)
                m_current = static_cast<int>(p);
        m_state.current = m_current < 0 ? std::nullopt : std::optional(keyOf(m_pairs[static_cast<std::size_t>(m_current)]));
    }
}

KanjiTimer::Work &KanjiTimer::workFor(const Pair &pair)
{
    Work &work = m_state.works[keyOf(pair)];
    // made on other texts: a done pair shows Changed, the rest start over
    if (work.romajiText != pair.romaji->text || work.kanjiText != pair.kanji->text) {
        // (the groups read from the kanji Line were nobody's work)
        const bool had = work.done || work.applied ||
                         (!work.linked.empty() && work.linked != kanjiText(work.kanjiText).tagged);
        work = Work{};
        work.changed = had;
        work.romajiText = pair.romaji->text;
        work.kanjiText = pair.kanji->text;
        auto kanji = kanjiText(pair.kanji->text);
        // KA4-furigana: the Line's own readings as typed ones
        for (const auto &reading : kanji.readings)
            work.typed[{reading.begin, reading.end}] = reading.text;
        // KA8-multi-highlight: a kanji Line with # syllables starts linked
        // as its tags group it, when that takes every syllable
        auto tagged = std::move(kanji.tagged);
        if (!tagged.empty() &&
            syllablesIn(tagged) == kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs).size())
            work.linked = std::move(tagged);
    }
    return work;
}

KanjiTimer::Status KanjiTimer::statusOf(const Pair &pair)
{
    if (!pair.kanji)
        return Status::NoKanji;
    if (!pair.romaji)
        return Status::NoRomaji;
    const Work &work = workFor(pair);
    if (work.applied)
        return Status::Applied;
    if (work.done)
        return Status::Done;
    if (work.changed)
        return Status::Changed;
    if (!m_suggest)
        return Status::ToDo;
    bool clean = true;
    const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
    (void)proposal(work, syllables, kanjiText(pair.kanji->text), &clean);
    return clean ? Status::Suggested : Status::Check;
}

KanjiGroup KanjiTimer::selectionOf(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                   const KanjiText &kanji, bool *ranOut) const
{
    const std::size_t from = syllablesIn(work.linked), at = charactersIn(work.linked);
    const std::size_t syllablesLeft = syllables.size() - std::min(from, syllables.size());
    const std::size_t charactersLeft = kanji.units.size() - std::min(at, kanji.units.size());
    KanjiGroup g;
    if (work.selection)
        g = *work.selection;
    else if (m_suggest)
        g = suggestGroup(syllables, from, kanji, at, m_classes, ranOut);
    else
        g = {1, 1};
    return {std::min(g.syllables, syllablesLeft), std::min(g.characters, charactersLeft)};
}

// The linked groups, the selection and the suggestion after it; `clean`
// whether the selection and the suggestion have no blank group and leave
// no character.
std::vector<KanjiGroup> KanjiTimer::proposal(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                             const KanjiText &kanji, bool *clean) const
{
    std::vector<KanjiGroup> out = work.linked;
    bool ranOut = false; // the suggested selection took the syllables the kanji ran out for
    const KanjiGroup selection = selectionOf(work, syllables, kanji, &ranOut);
    bool ok = !ranOut;
    if (selection.syllables || selection.characters) {
        out.push_back(selection);
        ok = ok && selection.characters > 0 && selection.syllables > 0;
    }
    if (m_suggest) {
        const auto rest = suggestGroups(syllables, syllablesIn(out), kanji, charactersIn(out), m_classes);
        out.insert(out.end(), rest.groups.begin(), rest.groups.end());
        ok = ok && rest.clean;
    } else if (syllablesIn(out) < syllables.size()) {
        ok = false;
    }
    if (clean)
        *clean = ok;
    return out;
}

std::optional<KanjiTimer::View> KanjiTimer::view() const
{
    if (m_current < 0)
        return std::nullopt;
    const Pair &pair = m_pairs[static_cast<std::size_t>(m_current)];
    const auto found = m_state.works.find(keyOf(pair));
    const Work work = found != m_state.works.end() ? found->second : Work{};
    View v;
    v.syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
    v.kanji = kanjiText(pair.kanji->text);
    v.linked = work.linked;
    v.selection = selectionOf(work, v.syllables, v.kanji);
    if (m_suggest) {
        std::vector<KanjiGroup> upTo = v.linked;
        upTo.push_back(v.selection);
        v.rest = suggestGroups(v.syllables, syllablesIn(upTo), v.kanji, charactersIn(upTo), m_classes).groups;
    }
    // Accept line: every syllable in a group, none of the ones not linked blank
    std::vector<KanjiGroup> all = v.linked;
    bool blank = false;
    if (v.selection.syllables || v.selection.characters) {
        all.push_back(v.selection);
        blank = v.selection.characters == 0;
    }
    for (const KanjiGroup &g : v.rest) {
        all.push_back(g);
        blank = blank || g.characters == 0;
    }
    const bool covers = syllablesIn(all) >= v.syllables.size() && !v.syllables.empty();
    v.canAccept = covers && !blank;
    if (m_furigana)
        v.readings = readingsOf(work, v.syllables, v.kanji, all);
    if (covers)
        v.output = outputOf(work, v.syllables, v.kanji, all);
    return v;
}

void KanjiTimer::begin()
{
    m_open = Step{};
    m_open->currentBefore = m_state.current;
}

KanjiTimer::Work &KanjiTimer::touch(const Pair &pair)
{
    const Key key = keyOf(pair);
    if (m_open && !m_open->works.contains(key)) {
        const auto found = m_state.works.find(key);
        m_open->works[key].first = found != m_state.works.end() ? std::optional(found->second) : std::nullopt;
    }
    return workFor(pair);
}

void KanjiTimer::commit()
{
    Step step = std::move(*m_open);
    m_open.reset();
    step.currentAfter = m_state.current;
    step.bytes = sizeof(Step);
    for (auto it = step.works.begin(); it != step.works.end();) {
        auto &[before, after] = it->second;
        const auto found = m_state.works.find(it->first);
        after = found != m_state.works.end() ? std::optional(found->second) : std::nullopt;
        if (before == after) {
            it = step.works.erase(it);
            continue;
        }
        for (const auto *w : {&before, &after})
            if (*w)
                step.bytes += sizeof(Work) + ((*w)->romajiText.size() + (*w)->kanjiText.size()) * sizeof(char16_t) +
                              (*w)->linked.size() * sizeof(KanjiGroup);
        ++it;
    }
    // the steps undone are gone; the oldest go past kHistory or kHistoryBytes
    for (; m_steps.size() > m_undo; m_steps.pop_back())
        m_stepBytes -= m_steps.back().bytes;
    m_stepBytes += step.bytes;
    m_steps.push_back(std::move(step));
    std::size_t oldest = 0;
    while (m_steps.size() - oldest > 1 && (m_steps.size() - oldest > kHistory || m_stepBytes > kHistoryBytes))
        m_stepBytes -= m_steps[oldest++].bytes;
    m_steps.erase(m_steps.begin(), m_steps.begin() + static_cast<std::ptrdiff_t>(oldest));
    m_undo = m_steps.size();
}

void KanjiTimer::rollback()
{
    const Step step = std::move(*m_open);
    m_open.reset();
    replay(step, false);
}

void KanjiTimer::replay(const Step &step, bool forward)
{
    for (const auto &[key, change] : step.works) {
        const auto &work = forward ? change.second : change.first;
        if (work)
            m_state.works[key] = *work;
        else
            m_state.works.erase(key);
    }
    if (step.skippedRomaji)
        forward ? (void)m_state.skippedRomaji.insert(*step.skippedRomaji) : (void)m_state.skippedRomaji.erase(*step.skippedRomaji);
    if (step.skippedKanji)
        forward ? (void)m_state.skippedKanji.insert(*step.skippedKanji) : (void)m_state.skippedKanji.erase(*step.skippedKanji);
    m_state.current = forward ? step.currentAfter : step.currentBefore;
    derive();
}

bool KanjiTimer::editCurrent(
    const std::function<bool(Work &, const std::vector<KanjiSyllable> &, const KanjiText &)> &edit)
{
    if (m_current < 0)
        return false;
    const Pair &pair = m_pairs[static_cast<std::size_t>(m_current)];
    begin();
    Work &work = touch(pair);
    const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
    const auto kanji = kanjiText(pair.kanji->text);
    if (!edit(work, syllables, kanji)) {
        rollback();
        return false;
    }
    commit();
    derive();
    return true;
}

namespace {

// The selection's syllables or characters changed by `by`, within what is left.
bool resize(std::size_t &value, int by, std::size_t left)
{
    if ((by < 0 && value == 0) || (by > 0 && value >= left))
        return false;
    value = static_cast<std::size_t>(static_cast<long long>(value) + by);
    return true;
}

} // namespace

bool KanjiTimer::growSyllables()
{
    return editCurrent([this](Work &w, const auto &syllables, const auto &kanji) {
        KanjiGroup s = selectionOf(w, syllables, kanji);
        if (!resize(s.syllables, 1, syllables.size() - syllablesIn(w.linked)))
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::shrinkSyllables()
{
    return editCurrent([this](Work &w, const auto &syllables, const auto &kanji) {
        KanjiGroup s = selectionOf(w, syllables, kanji);
        if (!resize(s.syllables, -1, 0))
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::growCharacters()
{
    return editCurrent([this](Work &w, const auto &syllables, const auto &kanji) {
        KanjiGroup s = selectionOf(w, syllables, kanji);
        if (!resize(s.characters, 1, kanji.units.size() - charactersIn(w.linked)))
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::shrinkCharacters()
{
    return editCurrent([this](Work &w, const auto &syllables, const auto &kanji) {
        KanjiGroup s = selectionOf(w, syllables, kanji);
        if (!resize(s.characters, -1, 0))
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::link()
{
    return editCurrent([this](Work &w, const auto &syllables, const auto &kanji) {
        const KanjiGroup s = selectionOf(w, syllables, kanji);
        if (!s.syllables && !s.characters)
            return false;
        w.linked.push_back(s);
        w.selection.reset();
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::unlink()
{
    return editCurrent([](Work &w, const auto &, const auto &) {
        if (w.linked.empty())
            return false;
        w.selection = w.linked.back();
        w.linked.pop_back();
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::acceptWork(const Pair &pair, Work &work, bool all)
{
    const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
    const auto kanji = kanjiText(pair.kanji->text);
    if (syllables.empty())
        return false;
    std::vector<KanjiGroup> groups = all ? proposal(work, syllables, kanji) : work.linked;
    if (syllablesIn(groups) < syllables.size())
        return false;
    // a blank group is linked by hand only (Accept line is off over one)
    for (std::size_t g = work.linked.size(); g < groups.size(); ++g)
        if (groups[g].characters == 0)
            return false;
    // KA3-leftover-kanji: the characters after the last group are its
    if (const std::size_t used = charactersIn(groups); used < kanji.units.size() && !groups.empty())
        groups.back().characters += kanji.units.size() - used;
    work.linked = std::move(groups);
    work.selection = KanjiGroup{0, 0};
    work.done = true;
    work.applied = work.changed = false;
    return true;
}

void KanjiTimer::moveOnFrom(int from)
{
    // the next pair with both Lines that is not done, else stay
    for (std::size_t p = static_cast<std::size_t>(from) + 1; p < m_pairs.size(); ++p) {
        const Pair &pair = m_pairs[p];
        if (pair.romaji && pair.kanji && pair.status != Status::Done && pair.status != Status::Applied) {
            m_state.current = keyOf(pair);
            return;
        }
    }
}

bool KanjiTimer::acceptLine()
{
    if (m_current < 0)
        return false;
    begin();
    const int at = m_current;
    const Pair pair = m_pairs[static_cast<std::size_t>(at)];
    if (!acceptWork(pair, touch(pair), true)) {
        rollback();
        return false;
    }
    derive();
    moveOnFrom(at);
    commit();
    derive();
    return true;
}

bool KanjiTimer::enter()
{
    if (m_current < 0)
        return false;
    const Pair &pair = m_pairs[static_cast<std::size_t>(m_current)];
    const Work &work = workFor(pair);
    const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
    if (!syllables.empty() && syllablesIn(work.linked) >= syllables.size()) {
        begin();
        const int at = m_current;
        if (!acceptWork(pair, touch(pair), false)) {
            rollback();
            return false;
        }
        derive();
        moveOnFrom(at);
        commit();
        derive();
        return true;
    }
    return link();
}

bool KanjiTimer::setReading(std::u16string_view reading)
{
    return editCurrent([&](Work &work, const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji) {
        const std::size_t from = charactersIn(work.linked);
        const auto runs = kanjiRunsIn(kanji.units, std::min(from, kanji.units.size()),
                                      std::min(from + selectionOf(work, syllables, kanji).characters, kanji.units.size()));
        if (runs.empty())
            return false;
        // a reading for each run when it has as many, split at spaces;
        // else all of it for the first
        std::vector<u16> pieces;
        for (std::size_t i = 0; i <= reading.size();) {
            std::size_t end = reading.find_first_of(u" \u3000", i);
            if (end == u16v::npos)
                end = reading.size();
            if (end > i)
                pieces.emplace_back(reading.substr(i, end - i));
            i = end + 1;
        }
        if (pieces.size() != runs.size()) {
            u16 whole;
            for (const auto &piece : pieces)
                whole += piece;
            pieces.assign(runs.size(), u16());
            pieces.front() = std::move(whole);
        }
        const KanjiTyped before = work.typed;
        work.typed = fitTyped(runsOver(kanji, proposal(work, syllables, kanji)), work.typed);
        for (std::size_t r = 0; r < runs.size(); ++r)
            work.typed[runs[r]] = pieces[r];
        return work.typed != before;
    });
}

bool KanjiTimer::useSuggestion()
{
    return editCurrent([&](Work &work, const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji) {
        const std::size_t from = charactersIn(work.linked);
        const KanjiTyped before = work.typed;
        work.typed = fitTyped(runsOver(kanji, proposal(work, syllables, kanji)), work.typed);
        for (const auto &run : kanjiRunsIn(kanji.units, std::min(from, kanji.units.size()),
                                           std::min(from + selectionOf(work, syllables, kanji).characters,
                                                    kanji.units.size())))
            work.typed.erase(run);
        return work.typed != before;
    });
}

bool KanjiTimer::clickSyllable(std::size_t syllable)
{
    return editCurrent([&](Work &w, const auto &syllables, const auto &kanji) {
        if (syllable >= syllables.size())
            return false;
        std::size_t start = 0;
        for (std::size_t g = 0; g < w.linked.size(); ++g) {
            if (syllable < start + w.linked[g].syllables) { // a linked group: it and the ones after it go
                w.selection = w.linked[g];
                w.linked.resize(g);
                w.done = w.applied = w.changed = false;
                return true;
            }
            start += w.linked[g].syllables;
        }
        KanjiGroup s = selectionOf(w, syllables, kanji);
        s.syllables = syllable + 1 - start;
        if (w.selection && *w.selection == s)
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::clickCharacter(std::size_t character)
{
    return editCurrent([&](Work &w, const auto &syllables, const auto &kanji) {
        if (character >= kanji.units.size())
            return false;
        std::size_t start = 0;
        for (std::size_t g = 0; g < w.linked.size(); ++g) {
            if (character < start + w.linked[g].characters) {
                w.selection = w.linked[g];
                w.linked.resize(g);
                w.done = w.applied = w.changed = false;
                return true;
            }
            start += w.linked[g].characters;
        }
        KanjiGroup s = selectionOf(w, syllables, kanji);
        s.characters = character + 1 - start;
        if (w.selection && *w.selection == s)
            return false;
        w.selection = s;
        w.done = w.applied = w.changed = false;
        return true;
    });
}

bool KanjiTimer::choose(int pair)
{
    if (pair < 0 || static_cast<std::size_t>(pair) >= m_pairs.size() || pair == m_current)
        return false;
    const Pair &p = m_pairs[static_cast<std::size_t>(pair)];
    if (!p.romaji || !p.kanji)
        return false;
    m_state.current = keyOf(p); // not an undo step: undo goes back to the pair it changes
    derive();
    return true;
}

bool KanjiTimer::previous()
{
    for (int p = m_current - 1; p >= 0; --p)
        if (m_pairs[static_cast<std::size_t>(p)].romaji && m_pairs[static_cast<std::size_t>(p)].kanji)
            return choose(p);
    return false;
}

bool KanjiTimer::skipRomaji()
{
    if (m_current < 0)
        return false;
    const Pair pair = m_pairs[static_cast<std::size_t>(m_current)];
    begin();
    m_state.skippedRomaji.insert(pair.romaji->id);
    m_open->skippedRomaji = pair.romaji->id;
    derive();
    // the kanji Line's new pair, if it has one
    for (const Pair &p : m_pairs)
        if (p.kanji && p.kanji->id == pair.kanji->id && p.romaji)
            m_state.current = keyOf(p);
    commit();
    derive();
    return true;
}

bool KanjiTimer::skipKanji()
{
    if (m_current < 0)
        return false;
    const Pair pair = m_pairs[static_cast<std::size_t>(m_current)];
    begin();
    m_state.skippedKanji.insert(pair.kanji->id);
    m_open->skippedKanji = pair.kanji->id;
    derive();
    for (const Pair &p : m_pairs)
        if (p.romaji && p.romaji->id == pair.romaji->id && p.kanji)
            m_state.current = keyOf(p);
    commit();
    derive();
    return true;
}

int KanjiTimer::acceptAllSuggestions()
{
    if (!m_suggest)
        return 0;
    int accepted = 0;
    begin();
    for (const Pair &pair : std::vector<Pair>(m_pairs)) {
        if (pair.status != Status::Suggested)
            continue;
        Work &work = touch(pair);
        const Work before = work;
        if (acceptWork(pair, work, true))
            ++accepted;
        else
            work = before;
    }
    if (!accepted) {
        rollback();
        return 0;
    }
    const int at = m_current;
    derive();
    if (at >= 0 && m_pairs[static_cast<std::size_t>(at)].status == Status::Done)
        moveOnFrom(at);
    commit();
    derive();
    return accepted;
}

bool KanjiTimer::undo()
{
    if (!canUndo())
        return false;
    replay(m_steps[--m_undo], false);
    return true;
}

bool KanjiTimer::redo()
{
    if (!canRedo())
        return false;
    replay(m_steps[m_undo++], true);
    return true;
}

std::vector<std::pair<core::LineId, std::u16string>> KanjiTimer::pending() const
{
    std::vector<std::pair<core::LineId, std::u16string>> out;
    for (const Pair &pair : m_pairs) {
        if (pair.status != Status::Done)
            continue;
        const auto found = m_state.works.find(keyOf(pair));
        if (found == m_state.works.end())
            continue;
        const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
        out.emplace_back(pair.kanji->id, outputOf(found->second, syllables, kanjiText(pair.kanji->text), found->second.linked));
    }
    return out;
}

void KanjiTimer::applied()
{
    for (const Pair &pair : m_pairs) {
        if (pair.status != Status::Done)
            continue;
        Work &work = m_state.works[keyOf(pair)];
        const auto syllables = kanjiSyllables(pair.romaji->text, pair.romaji->startMs, pair.romaji->endMs);
        work.kanjiText = outputOf(work, syllables, kanjiText(pair.kanji->text), work.linked);
        work.done = false;
        work.applied = true;
    }
    // what came before can no longer be undone: the Document has it
    m_steps.clear();
    m_stepBytes = 0;
    m_undo = 0;
}

} // namespace hikari::application
