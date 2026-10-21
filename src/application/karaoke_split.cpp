#include "hikari/application/karaoke_split.h"

#include "hikari/application/audio_karaoke.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

// ---- libhyphen patterns ------------------------------------------------------

std::string_view trimmed(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    return line;
}

// ---- the text's characters ---------------------------------------------------

enum class Kind { Space, Punct, Kana, Han, Hangul, Latin, Other };

struct Unit {
    std::size_t at = 0, len = 0;
    char32_t cp = 0;
    Kind kind = Kind::Other;
};

bool inRange(char32_t c, char32_t from, char32_t to)
{
    return c >= from && c <= to;
}

bool isKana(char32_t c)
{
    return inRange(c, 0x3041, 0x3096) || inRange(c, 0x309D, 0x309F) || inRange(c, 0x30A1, 0x30FA) ||
           inRange(c, 0x30FC, 0x30FF) || inRange(c, 0x31F0, 0x31FF) || inRange(c, 0xFF66, 0xFF9F);
}

bool isHan(char32_t c)
{
    return c == 0x3005 || c == 0x3007 || c == 0x303B || inRange(c, 0x3400, 0x4DBF) || inRange(c, 0x4E00, 0x9FFF) ||
           inRange(c, 0xF900, 0xFAFF) || inRange(c, 0x20000, 0x3134F);
}

bool isHangul(char32_t c)
{
    return inRange(c, 0xAC00, 0xD7A3) || inRange(c, 0x1100, 0x11FF) || inRange(c, 0x3130, 0x318F);
}

bool isLatin(char32_t c)
{
    return inRange(c, U'a', U'z') || inRange(c, U'A', U'Z') ||
           (inRange(c, 0x00C0, 0x024F) && c != 0x00D7 && c != 0x00F7) || inRange(c, 0x1E00, 0x1EFF);
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

// The visible characters of a Line, as Auto split reads them: override
// blocks (and an unclosed "{" to the end) skipped, \N \n \h one space each.
std::vector<Unit> units(u16v text, const KaraokeCharClass &classes)
{
    std::vector<Unit> out;
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
            out.push_back(unit);
            i += 2;
            continue;
        }
        unit.cp = codePointAt(text, i, unit.len);
        while (i + unit.len < text.size() && text[i + unit.len] != u'{' && karaokeContinuesGrapheme(text, i + unit.len))
            ++unit.len;
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
        else if (bmp && classes.punct && classes.punct(static_cast<char16_t>(c)))
            unit.kind = Kind::Punct;
        else if (isLatin(c))
            unit.kind = Kind::Latin;
        else
            unit.kind = Kind::Other;
        out.push_back(unit);
        i += unit.len;
    }
    // an apostrophe between two Latin letters is part of the word (don't, n'yo)
    for (std::size_t k = 1; k + 1 < out.size(); k++)
        if (out[k].kind == Kind::Punct && isApostrophe(out[k].cp) && out[k - 1].kind == Kind::Latin &&
            out[k + 1].kind == Kind::Latin && out[k - 1].at + out[k - 1].len == out[k].at &&
            out[k].at + out[k].len == out[k + 1].at)
            out[k].kind = Kind::Latin;
    return out;
}

bool isWord(Kind kind)
{
    return kind != Kind::Space && kind != Kind::Punct;
}

// Punctuation that opens what follows it (a quote or bracket after a space).
bool opens(char32_t c)
{
    static constexpr std::u32string_view set = U"([{<\"'‘“«「『【〈《（［";
    return set.find(c) != std::u32string_view::npos;
}

// ---- kana --------------------------------------------------------------------

bool smallKana(char32_t c)
{
    static constexpr std::u32string_view set =
        U"ぁぃぅぇぉゃゅょゎァィゥェォャュョ"
        U"ヮｧｨｩｪｫｬｭｮﾞﾟ";
    return set.find(c) != std::u32string_view::npos || inRange(c, 0x31F0, 0x31FF);
}

bool sokuon(char32_t c)
{
    return c == 0x3063 || c == 0x30C3 || c == 0xFF6F;
}

bool syllabicN(char32_t c)
{
    return c == 0x3093 || c == 0x30F3 || c == 0xFF9D;
}

// ---- romaji ------------------------------------------------------------------

enum class Mora { Plain, Double, N };

bool vowel(char16_t c)
{
    return c == u'a' || c == u'i' || c == u'u' || c == u'e' || c == u'o';
}

// Hepburn's syllables (with the loanword ones Japanese lyrics write: fa,
// di, she, je...) that are not a lone vowel. Not wi, we and the v ones:
// English words (love, were) would read as romaji.
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

// The morae of a lower-case word read as romaji (one character a unit,
// accents taken off), or none when it is not romaji.
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
        // syllabic n: before a consonant (not y), an apostrophe or the end; m before b, p, m
        if (c == u'n' && (next == u'\0' || next == u'\'' || (!vowel(next) && next != u'y'))) {
            out.push_back({i, Mora::N});
            i += next == u'\'' ? 2 : 1;
            continue;
        }
        if (c == u'm' && (next == u'b' || next == u'p' || next == u'm')) {
            out.push_back({i++, Mora::N});
            continue;
        }
        // a double consonant (kk, tt, tch, cch...)
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

// The letter a romaji reading takes: lower case without the marks Hepburn
// and its variants write long vowels with (ā and â); other accents (café,
// naïve) are no romaji.
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

// ---- one word ----------------------------------------------------------------

// The units (indices from `from`) of the word [from, to) that open a
// syllable, after its first.
// The run [from, to) of Latin units as romaji reads it, and how many
// letters it has.
std::pair<u16, int> latinLetters(const std::vector<Unit> &all, std::size_t from, std::size_t to,
                                 const KaraokeCharClass &classes)
{
    u16 letters;
    int count = 0;
    for (std::size_t k = from; k < to; k++) {
        letters += romajiLetter(all[k].cp, classes);
        if (!isApostrophe(all[k].cp))
            ++count;
    }
    return {letters, count};
}

// Whether the Line's Latin words are English: its words that are no romaji
// outweigh those that are, a word weighing its letters past the second (a
// tie is romaji). Short words (to, no, I) read as either, and more English
// ones than long ones (take, home) read as romaji by chance.
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
    // where the next run's syllable starts: an unclassified run (digits,
    // symbols) opens the one after it
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
            starts.push_back(opens); // a new script, a new syllable
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
            // in an English Line every word is English; in a romaji one, a
            // word that is no romaji
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

} // namespace

// ---- Hyphenation -------------------------------------------------------------

Hyphenation Hyphenation::fromPatterns(std::string_view utf8)
{
    Hyphenation out;
    bool first = true;
    while (!utf8.empty()) {
        const auto eol = utf8.find('\n');
        const auto line = trimmed(utf8.substr(0, eol));
        utf8.remove_prefix(eol == std::string_view::npos ? utf8.size() : eol + 1);
        if (std::exchange(first, false))
            continue; // the charset
        if (line.empty() || line.front() == '%')
            continue;
        if (line == "NEXTLEVEL")
            break; // compound-word levels: not read
        if (line.find(' ') != std::string_view::npos)
            continue; // LEFTHYPHENMIN and the like: print's, not a singer's
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
    // two letters at least on either side (sto|ry, not s|to|ry or dream|s)
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

// ---- Auto split --------------------------------------------------------------

bool karaokeContinuesGrapheme(std::u16string_view text, std::size_t at)
{
    if (at == 0 || at >= text.size())
        return false;
    const char16_t c = text[at], before = text[at - 1];
    if (c >= 0xDC00 && c <= 0xDFFF && before >= 0xD800 && before <= 0xDBFF)
        return true;
    if (before == 0x200D) // what a zero width joiner joins
        return true;
    if (inRange(c, 0x0300, 0x036F) || inRange(c, 0x1AB0, 0x1AFF) || inRange(c, 0x1DC0, 0x1DFF) ||
        inRange(c, 0x20D0, 0x20FF) || inRange(c, 0xFE00, 0xFE0F) || inRange(c, 0xFE20, 0xFE2F) ||
        inRange(c, 0x3099, 0x309A) || c == 0x200D)
        return true;
    if (at + 1 < text.size()) {
        std::size_t len = 0;
        const char32_t cp = codePointAt(text, at, len);
        // emoji modifiers and the supplementary variation selectors
        if (len == 2 && (inRange(cp, 0x1F3FB, 0x1F3FF) || inRange(cp, 0xE0100, 0xE01EF)))
            return true;
    }
    return false;
}

int karaokeWeight(std::u16string_view text, const KaraokeCharClass &classes)
{
    int weight = 0;
    for (const auto &unit : units(text, classes))
        if (isWord(unit.kind))
            ++weight;
    return weight;
}

std::vector<std::size_t> karaokeAutoSplit(std::u16string_view text, const KaraokeSplitRules &rules,
                                          const KaraokeCharClass &classes)
{
    const auto all = units(text, classes);
    const bool english = englishLine(all, classes);
    std::vector<std::size_t> startUnits; // the units that open a syllable, after the first
    std::size_t wordEnd = 0;             // past the last word
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
            // the separators between two words end the syllable before them,
            // but for what opens the word after the last space
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
                    at = k; // only opening punctuation: it ends the syllable before
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
        // right after the visible character before: blocks between go with the new syllable
        const std::size_t at = unit == 0 ? 0 : all[unit - 1].at + all[unit - 1].len;
        if (at > starts.back())
            starts.push_back(at);
    }
    return starts;
}

} // namespace hikari::application
