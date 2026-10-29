#include "hikari/application/karaoke_split.h"

#include "hikari/application/audio_karaoke.h"
#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

std::string_view trimmed(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    return line;
}

enum class Kind { Space, Punct, Kana, Han, Hangul, Latin, Other };

struct Unit {
    std::size_t at = 0, len = 0;
    char32_t cp = 0;   // the grapheme's first character
    char32_t mark = 0; // its one mark (NFD), 0xFFFD for more, else 0
    Kind kind = Kind::Other;
};

bool inRange(char32_t c, char32_t from, char32_t to)
{
    return c >= from && c <= to;
}

bool isKana(char32_t c)
{
    return inRange(c, 0x3041, 0x3096) || inRange(c, 0x309D, 0x309F) || inRange(c, 0x30A1, 0x30FA) ||
           inRange(c, 0x30FC, 0x30FF) || inRange(c, 0x31F0, 0x31FF) || inRange(c, 0xFF66, 0xFF9F) ||
           // Kana Extended-B, Kana Supplement, Kana Extended-A, Small Kana Extension
           inRange(c, 0x1AFF0, 0x1AFFF) || inRange(c, 0x1B000, 0x1B16F);
}

bool isHan(char32_t c)
{
    return c == 0x3005 || c == 0x3007 || c == 0x303B || inRange(c, 0x3400, 0x4DBF) || inRange(c, 0x4E00, 0x9FFF) ||
           inRange(c, 0xF900, 0xFAFF) || inRange(c, 0x20000, 0x323AF); // through Extension H
}

bool isHangul(char32_t c)
{
    return inRange(c, 0xAC00, 0xD7A3) || inRange(c, 0x1100, 0x11FF) || inRange(c, 0x3130, 0x318F) ||
           inRange(c, 0xA960, 0xA97C) || inRange(c, 0xD7B0, 0xD7FB) ||
           inRange(c, 0xFFA0, 0xFFDC); // halfwidth jamo
}

bool isLatin(char32_t c)
{
    return inRange(c, U'a', U'z') || inRange(c, U'A', U'Z') ||
           (inRange(c, 0x00C0, 0x024F) && c != 0x00D7 && c != 0x00F7) || inRange(c, 0x1E00, 0x1EFF) ||
           inRange(c, 0xFB00, 0xFB06) || // ligatures (ﬁ, ﬃ)
           // IPA and Latin Extended-C to G
           inRange(c, 0x0250, 0x02AF) || inRange(c, 0x2C60, 0x2C7F) || inRange(c, 0xA720, 0xA7FF) ||
           inRange(c, 0xAB30, 0xAB6F) || inRange(c, 0x10780, 0x107BF) || inRange(c, 0x1DF00, 0x1DFFF);
}

bool isApostrophe(char32_t c)
{
    return c == U'\'' || c == 0x2019;
}

char32_t codePointAt(u16v text, std::size_t at, std::size_t &len)
{
    const char16_t c = text[at];
    if (c >= 0xD800 && c <= 0xDBFF && at + 1 < text.size() && text[at + 1] >= 0xDC00 && text[at + 1] <= 0xDFFF) {
        len = 2;
        return 0x10000 + ((char32_t(c) - 0xD800) << 10) + (char32_t(text[at + 1]) - 0xDC00);
    }
    len = 1;
    return c;
}

// An unclosed "{" ends the text. `take` gets each unit once the next one is
// read; returning false stops.
template <class Take>
void eachUnit(u16v text, const KaraokeCharClass &classes, Take take)
{
    std::optional<Unit> held;
    std::optional<Kind> before;
    const auto hand = [&](const Unit &unit) {
        if (held && before == Kind::Latin && held->kind == Kind::Punct && isApostrophe(held->cp) &&
            unit.kind == Kind::Latin)
            held->kind = Kind::Latin;
        if (held && !take(*held))
            return false;
        before = held ? std::optional(held->kind) : std::nullopt;
        held = unit;
        return true;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == u'{') {
            const auto close = text.find(u'}', i + 1);
            if (close == u16v::npos)
                break;
            i = close + 1;
            continue;
        }
        Unit unit;
        unit.at = i;
        if (text[i] == u'\\' && i + 1 < text.size() &&
            (text[i + 1] == u'N' || text[i + 1] == u'n' || text[i + 1] == u'h')) {
            unit.len = 2;
            unit.cp = u' ';
            unit.kind = Kind::Space;
            if (!hand(unit))
                return;
            i += 2;
            continue;
        }
        unit.cp = codePointAt(text, i, unit.len);
        const std::size_t base = unit.len;
        unit.len = std::max(unit.len, karaokeLetterEnd(text, i) - i);
        for (std::size_t m = i + base; m < i + unit.len;) {
            if (text[m] == u'{') {
                m = text.find(u'}', m) + 1;
                continue;
            }
            std::size_t len = 0;
            const char32_t cp = codePointAt(text, m, len);
            unit.mark = unit.mark ? 0xFFFD : cp;
            m += len;
        }
        const char32_t c = unit.cp;
        const bool bmp = c < 0x10000;
        if (bmp && classes.space && classes.space(static_cast<char16_t>(c)))
            unit.kind = Kind::Space;
        else if (isKana(c))
            unit.kind = Kind::Kana;
        else if (isHan(c))
            unit.kind = Kind::Han;
        else if (isHangul(c))
            unit.kind = Kind::Hangul;
        else if (classes.symbol && classes.symbol(c))
            unit.kind = Kind::Other;
        else if (classes.punct && classes.punct(c))
            unit.kind = Kind::Punct;
        else if (isLatin(c))
            unit.kind = Kind::Latin;
        else
            unit.kind = Kind::Other;
        if (!hand(unit))
            return;
        i += unit.len;
    }
    if (held)
        take(*held);
}

// At most `most` + 1 units.
std::vector<Unit> units(u16v text, const KaraokeCharClass &classes, std::size_t most)
{
    std::vector<Unit> out;
    eachUnit(text, classes, [&](const Unit &unit) {
        out.push_back(unit);
        return out.size() <= most;
    });
    return out;
}

bool isWord(Kind kind)
{
    return kind != Kind::Space && kind != Kind::Punct;
}

// Opening quotes and brackets.
bool opens(char32_t c)
{
    static constexpr std::u32string_view set = U"([{<\"'‘“«「『【〈《（［";
    return set.find(c) != std::u32string_view::npos;
}

bool smallKana(char32_t c)
{
    static constexpr std::u32string_view set =
        U"ぁぃぅぇぉゃゅょゎァィゥェォャュョ"
        U"ヮｧｨｩｪｫｬｭｮﾞﾟ"
        U"ゕゖヵヶ";
    return set.find(c) != std::u32string_view::npos || inRange(c, 0x31F0, 0x31FF) ||
           // Small Kana Extension
           c == 0x1B132 || inRange(c, 0x1B150, 0x1B152) || c == 0x1B155 || inRange(c, 0x1B164, 0x1B167);
}

bool sokuon(char32_t c)
{
    return c == 0x3063 || c == 0x30C3 || c == 0xFF6F;
}

bool syllabicN(char32_t c)
{
    return c == 0x3093 || c == 0x30F3 || c == 0xFF9D;
}

enum class Mora { Plain, Double, N };

bool vowel(char16_t c)
{
    return c == u'a' || c == u'i' || c == u'u' || c == u'e' || c == u'o';
}

bool romajiSyllable(u16v s)
{
    static constexpr std::array<std::u16string_view, 104> table = {
        u"ka",  u"ki",  u"ku",  u"ke",  u"ko",  u"kya", u"kyu", u"kyo", u"ga",  u"gi",  u"gu",  u"ge",  u"go",
        u"gya", u"gyu", u"gyo", u"sa",  u"su",  u"se",  u"so",  u"shi", u"sha", u"shu", u"sho", u"she", u"za",
        u"zu",  u"ze",  u"zo",  u"ji",  u"ja",  u"ju",  u"jo",  u"je",  u"ta",  u"te",  u"to",  u"tsu", u"chi",
        u"cha", u"chu", u"cho", u"che", u"da",  u"de",  u"do",  u"di",  u"na",  u"ni",  u"nu",  u"ne",  u"no",
        u"nya", u"nyu", u"nyo", u"ha",  u"hi",  u"he",  u"ho",  u"hya", u"hyu", u"hyo", u"fu",  u"fa",  u"fi",
        u"fe",  u"fo",  u"ba",  u"bi",  u"bu",  u"be",  u"bo",  u"bya", u"byu", u"byo", u"pa",  u"pi",  u"pu",
        u"pe",  u"po",  u"pya", u"pyu", u"pyo", u"ma",  u"mi",  u"mu",  u"me",  u"mo",  u"mya", u"myu", u"myo",
        u"ya",  u"yu",  u"yo",  u"ra",  u"ri",  u"ru",  u"re",  u"ro",  u"rya", u"ryu", u"ryo", u"wa",  u"wo",
    };
    return std::find(table.begin(), table.end(), s) != table.end();
}

struct MoraAt {
    std::size_t at;
    Mora kind;
};

// nullopt when the lower-case word is not romaji.
std::optional<std::vector<MoraAt>> romajiMorae(u16v s)
{
    std::vector<MoraAt> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char16_t c = s[i];
        const char16_t next = i + 1 < s.size() ? s[i + 1] : u'\0';
        if (vowel(c)) {
            out.push_back({i++, Mora::Plain});
            continue;
        }
        if (c == u'n' && (next == u'\0' || next == u'\'' || (!vowel(next) && next != u'y'))) {
            out.push_back({i, Mora::N});
            i += next == u'\'' ? 2 : 1;
            continue;
        }
        if (c == u'm' && (next == u'b' || next == u'p' || next == u'm')) {
            out.push_back({i++, Mora::N});
            continue;
        }
        if ((next == c && std::u16string_view(u"kgsztdbpfjc").find(c) != std::u16string_view::npos) ||
            (c == u't' && next == u'c')) {
            out.push_back({i++, Mora::Double});
            continue;
        }
        if (i + 3 <= s.size() && romajiSyllable(s.substr(i, 3))) {
            out.push_back({i, Mora::Plain});
            i += 3;
        } else if (i + 2 <= s.size() && romajiSyllable(s.substr(i, 2))) {
            out.push_back({i, Mora::Plain});
            i += 2;
        } else {
            return std::nullopt;
        }
    }
    return out;
}

char16_t romajiLetter(char32_t c, const KaraokeCharClass &classes)
{
    if (c >= 0x10000)
        return u'?';
    char16_t low = classes.lower ? classes.lower(static_cast<char16_t>(c)) : static_cast<char16_t>(c);
    if (low >= u'A' && low <= u'Z')
        low = static_cast<char16_t>(low - u'A' + u'a');
    struct Fold {
        char16_t letter;
        std::u16string_view from;
    };
    static constexpr Fold folds[] = {
        {u'a', u"âāÂĀ"}, {u'i', u"îīÎĪ"}, {u'u', u"ûūÛŪ"}, {u'e', u"êēÊĒ"}, {u'o', u"ôōÔŌ"},
    };
    for (const auto &fold : folds)
        if (fold.from.find(low) != std::u16string_view::npos)
            return fold.letter;
    return isApostrophe(c) ? u'\'' : low;
}

std::pair<u16, int> latinLetters(const std::vector<Unit> &all, std::size_t from, std::size_t to,
                                 const KaraokeCharClass &classes)
{
    u16 letters;
    int count = 0;
    for (std::size_t k = from; k < to; k++) {
        const char16_t letter = romajiLetter(all[k].cp, classes);
        const bool longVowel = (all[k].mark == 0x0302 || all[k].mark == 0x0304) &&
                               std::u16string_view(u"aiueo").find(letter) != std::u16string_view::npos;
        letters += all[k].mark && !longVowel ? u'\uFFFD' : letter;
        if (!isApostrophe(all[k].cp))
            ++count;
    }
    return {letters, count};
}

bool englishLine(const std::vector<Unit> &all, const KaraokeCharClass &classes)
{
    int romaji = 0, english = 0;
    for (std::size_t k = 0; k < all.size();) {
        if (all[k].kind != Kind::Latin) {
            ++k;
            continue;
        }
        std::size_t end = k + 1;
        while (end < all.size() && all[end].kind == Kind::Latin)
            ++end;
        const auto [letters, count] = latinLetters(all, k, end, classes);
        if (count > 2)
            (romajiMorae(letters) ? romaji : english) += count - 2;
        k = end;
    }
    return english > romaji;
}

void splitWord(const std::vector<Unit> &all, std::size_t from, std::size_t to, const KaraokeSplitRules &rules,
               const KaraokeCharClass &classes, bool english, std::vector<std::size_t> &starts)
{
    std::size_t runStart = from;
    std::size_t opens = from;
    while (runStart < to) {
        std::size_t runEnd = runStart + 1;
        while (runEnd < to && all[runEnd].kind == all[runStart].kind)
            ++runEnd;
        if (all[runStart].kind == Kind::Other) {
            runStart = runEnd;
            continue;
        }
        if (opens != from)
            starts.push_back(opens);
        opens = runEnd;
        const bool lastRun = runEnd == to;
        switch (all[runStart].kind) {
        case Kind::Han:
        case Kind::Hangul:
            for (std::size_t k = runStart + 1; k < runEnd; k++)
                starts.push_back(k);
            break;
        case Kind::Kana:
            for (std::size_t k = runStart + 1; k < runEnd; k++) {
                const char32_t c = all[k].cp;
                if (smallKana(c) || (sokuon(c) && !rules.doubleConsonantApart))
                    continue;
                if (syllabicN(c) && (rules.everyN || (lastRun && k + 1 == runEnd)))
                    continue;
                starts.push_back(k);
            }
            break;
        case Kind::Latin: {
            const u16 letters = latinLetters(all, runStart, runEnd, classes).first;
            const auto morae = english ? std::nullopt : romajiMorae(letters);
            if (morae) {
                for (std::size_t m = 1; m < morae->size(); m++) {
                    const auto &mora = (*morae)[m];
                    if (mora.kind == Mora::Double && !rules.doubleConsonantApart)
                        continue;
                    if (mora.kind == Mora::N && (rules.everyN || (lastRun && m + 1 == morae->size())))
                        continue;
                    starts.push_back(runStart + mora.at);
                }
            } else if (rules.hyphenation) {
                for (const std::size_t at : rules.hyphenation->breaks(letters))
                    starts.push_back(runStart + at);
            }
            break;
        }
        default:
            break;
        }
        runStart = runEnd;
    }
}

}

Hyphenation Hyphenation::fromPatterns(std::string_view utf8)
{
    Hyphenation out;
    bool first = true;
    while (!utf8.empty()) {
        const auto eol = utf8.find('\n');
        const auto line = trimmed(utf8.substr(0, eol));
        utf8.remove_prefix(eol == std::string_view::npos ? utf8.size() : eol + 1);
        if (std::exchange(first, false))
            continue; // the charset line
        if (line.empty() || line.front() == '%')
            continue;
        if (line == "NEXTLEVEL")
            break; // compound-word levels
        if (line.find(' ') != std::string_view::npos)
            continue; // LEFTHYPHENMIN and the like
        if (line.find('/') != std::string_view::npos)
            continue; // libhyphen's non-standard patterns
        const u16 pattern = core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(line.data()), line.size()));
        u16 letters;
        std::vector<std::uint8_t> values(1, 0);
        for (const char16_t c : pattern) {
            if (c >= u'0' && c <= u'9') {
                values.back() = static_cast<std::uint8_t>(c - u'0');
            } else {
                letters += c;
                values.push_back(0);
            }
        }
        if (letters.empty())
            continue;
        out.m_longest = std::max(out.m_longest, letters.size());
        out.m_patterns[letters] = std::move(values);
    }
    return out;
}

std::vector<std::size_t> Hyphenation::breaks(std::u16string_view word) const
{
    constexpr std::size_t kLeft = 2, kRight = 2;
    std::vector<std::size_t> out;
    if (m_patterns.empty() || word.size() < kLeft + kRight)
        return out;
    const u16 dotted = u"." + u16(word) + u".";
    std::vector<std::uint8_t> points(dotted.size() + 1, 0);
    for (std::size_t i = 0; i < dotted.size(); i++)
        for (std::size_t len = 1; len <= m_longest && i + len <= dotted.size(); len++) {
            const auto found = m_patterns.find(dotted.substr(i, len));
            if (found == m_patterns.end())
                continue;
            for (std::size_t k = 0; k < found->second.size(); k++)
                points[i + k] = std::max(points[i + k], found->second[k]);
        }
    // points[p + 1] lies between word[p - 1] and word[p]
    for (std::size_t p = kLeft; p + kRight <= word.size(); p++)
        if (points[p + 1] % 2)
            out.push_back(p);
    return out;
}

bool karaokeContinuesGrapheme(std::u16string_view text, std::size_t at)
{
    if (at == 0 || at >= text.size())
        return false;
    std::size_t start = 0;
    while (start < at)
        start = core::graphemeEnd(text, start);
    return start != at;
}

int karaokeWeight(std::u16string_view text, const KaraokeCharClass &classes)
{
    int weight = 0;
    eachUnit(text, classes, [&](const Unit &unit) {
        weight += isWord(unit.kind) ? 1 : 0;
        return true;
    });
    return weight;
}

std::vector<std::size_t> karaokeAutoSplit(std::u16string_view text, const KaraokeSplitRules &rules,
                                          const KaraokeCharClass &classes)
{
    const auto all = units(text, classes, kKaraokeAutoSplitMost);
    if (all.size() > kKaraokeAutoSplitMost)
        return {};
    const bool english = englishLine(all, classes);
    std::vector<std::size_t> startUnits;
    std::size_t wordEnd = 0;
    bool firstWord = true;
    for (std::size_t k = 0; k < all.size();) {
        if (!isWord(all[k].kind)) {
            ++k;
            continue;
        }
        std::size_t end = k + 1;
        while (end < all.size() && isWord(all[end].kind))
            ++end;
        if (!firstWord) {
            std::size_t at = k;
            std::size_t lastSpace = all.size();
            for (std::size_t s = wordEnd; s < k; s++)
                if (all[s].kind == Kind::Space)
                    lastSpace = s;
            if (lastSpace != all.size()) {
                at = lastSpace + 1;
            } else {
                while (at > wordEnd && opens(all[at - 1].cp))
                    --at;
                if (at == wordEnd)
                    at = k;
            }
            startUnits.push_back(at);
        }
        firstWord = false;
        splitWord(all, k, end, rules, classes, english, startUnits);
        wordEnd = end;
        k = end;
    }
    std::vector<std::size_t> starts{0};
    for (const std::size_t unit : startUnits) {
        const std::size_t at = unit == 0 ? 0 : all[unit - 1].at + all[unit - 1].len;
        if (at > starts.back())
            starts.push_back(at);
    }
    return starts;
}

}
