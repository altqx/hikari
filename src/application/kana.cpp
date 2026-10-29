#include "hikari/application/kana.h"

#include <algorithm>
#include <array>
#include <map>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

struct Entry {
    u16v kana;
    std::string_view romaji; // space-separated; the first is canonical
};

// MUST KILL: entry order decides which kana romajiToHiragana gives a reading
constexpr std::array kTable{
    Entry{u"あ", "a"},    Entry{u"い", "i"},     Entry{u"う", "u"},       Entry{u"え", "e"},    Entry{u"お", "o"},
    Entry{u"か", "ka"},   Entry{u"き", "ki"},    Entry{u"く", "ku"},      Entry{u"け", "ke"},   Entry{u"こ", "ko"},
    Entry{u"が", "ga"},   Entry{u"ぎ", "gi"},    Entry{u"ぐ", "gu"},      Entry{u"げ", "ge"},   Entry{u"ご", "go"},
    Entry{u"さ", "sa"},   Entry{u"し", "shi si"}, Entry{u"す", "su"},     Entry{u"せ", "se"},   Entry{u"そ", "so"},
    Entry{u"ざ", "za"},   Entry{u"じ", "ji zi"}, Entry{u"ず", "zu"},      Entry{u"ぜ", "ze"},   Entry{u"ぞ", "zo"},
    Entry{u"た", "ta"},   Entry{u"ち", "chi ti"}, Entry{u"つ", "tsu tu"}, Entry{u"て", "te"},   Entry{u"と", "to"},
    Entry{u"だ", "da"},   Entry{u"で", "de"},    Entry{u"ど", "do"},
    Entry{u"な", "na"},   Entry{u"に", "ni"},    Entry{u"ぬ", "nu"},      Entry{u"ね", "ne"},   Entry{u"の", "no"},
    Entry{u"は", "ha wa"}, Entry{u"ひ", "hi"},   Entry{u"ふ", "fu hu"},   Entry{u"へ", "he e"}, Entry{u"ほ", "ho"},
    Entry{u"ば", "ba"},   Entry{u"び", "bi"},    Entry{u"ぶ", "bu"},      Entry{u"べ", "be"},   Entry{u"ぼ", "bo"},
    Entry{u"ぱ", "pa"},   Entry{u"ぴ", "pi"},    Entry{u"ぷ", "pu"},      Entry{u"ぺ", "pe"},   Entry{u"ぽ", "po"},
    Entry{u"ま", "ma"},   Entry{u"み", "mi"},    Entry{u"む", "mu"},      Entry{u"め", "me"},   Entry{u"も", "mo"},
    Entry{u"や", "ya"},   Entry{u"ゆ", "yu"},    Entry{u"よ", "yo"},
    Entry{u"ら", "ra"},   Entry{u"り", "ri"},    Entry{u"る", "ru"},      Entry{u"れ", "re"},   Entry{u"ろ", "ro"},
    Entry{u"わ", "wa"},   Entry{u"を", "wo o"},  Entry{u"ん", "n n' m"},  Entry{u"ゔ", "vu"},
    Entry{u"きゃ", "kya"}, Entry{u"きゅ", "kyu"}, Entry{u"きょ", "kyo"},
    Entry{u"ぎゃ", "gya"}, Entry{u"ぎゅ", "gyu"}, Entry{u"ぎょ", "gyo"},
    Entry{u"しゃ", "sha sya"}, Entry{u"しゅ", "shu syu"}, Entry{u"しょ", "sho syo"}, Entry{u"しぇ", "she sye"},
    Entry{u"じゃ", "ja zya jya"}, Entry{u"じゅ", "ju zyu jyu"}, Entry{u"じょ", "jo zyo jyo"}, Entry{u"じぇ", "je zye"},
    Entry{u"ちゃ", "cha tya cya"}, Entry{u"ちゅ", "chu tyu cyu"}, Entry{u"ちょ", "cho tyo cyo"}, Entry{u"ちぇ", "che tye"},
    Entry{u"にゃ", "nya"}, Entry{u"にゅ", "nyu"}, Entry{u"にょ", "nyo"},
    Entry{u"ひゃ", "hya"}, Entry{u"ひゅ", "hyu"}, Entry{u"ひょ", "hyo"},
    Entry{u"びゃ", "bya"}, Entry{u"びゅ", "byu"}, Entry{u"びょ", "byo"},
    Entry{u"ぴゃ", "pya"}, Entry{u"ぴゅ", "pyu"}, Entry{u"ぴょ", "pyo"},
    Entry{u"みゃ", "mya"}, Entry{u"みゅ", "myu"}, Entry{u"みょ", "myo"},
    Entry{u"りゃ", "rya"}, Entry{u"りゅ", "ryu"}, Entry{u"りょ", "ryo"},
    Entry{u"ふぁ", "fa"},  Entry{u"ふぃ", "fi"},  Entry{u"ふぇ", "fe"},  Entry{u"ふぉ", "fo"},
    Entry{u"てぃ", "ti di"}, Entry{u"でぃ", "di"}, Entry{u"とぅ", "tu"}, Entry{u"どぅ", "du"},
    Entry{u"うぃ", "wi"},  Entry{u"うぇ", "we"},  Entry{u"うぉ", "wo"},
    Entry{u"ゔぁ", "va"},  Entry{u"ゔぃ", "vi"},  Entry{u"ゔぇ", "ve"},  Entry{u"ゔぉ", "vo"},
    Entry{u"つぁ", "tsa"},
    Entry{u"ぢ", "ji di zi"}, Entry{u"づ", "zu du"}, Entry{u"ゐ", "wi i"}, Entry{u"ゑ", "we e"},
    Entry{u"ぢゃ", "ja dya"}, Entry{u"ぢゅ", "ju dyu"}, Entry{u"ぢょ", "jo dyo"},
    Entry{u"ぁ", "a"},  Entry{u"ぃ", "i"},  Entry{u"ぅ", "u"},  Entry{u"ぇ", "e"},  Entry{u"ぉ", "o"},
    Entry{u"ゃ", "ya"}, Entry{u"ゅ", "yu"}, Entry{u"ょ", "yo"}, Entry{u"ゎ", "wa"}, Entry{u"ゕ", "ka"}, Entry{u"ゖ", "ke"},
};

constexpr char16_t kSmallTsu = u'っ';
constexpr char16_t kLongVowel = u'ー';
constexpr std::string_view kConsonants = "kstcpgzdbfhjmrwyv";

std::vector<std::string_view> readingsOf(const Entry &entry)
{
    std::vector<std::string_view> out;
    std::string_view rest = entry.romaji;
    while (!rest.empty()) {
        const auto space = rest.find(' ');
        out.push_back(rest.substr(0, space));
        rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    }
    return out;
}

u16 widen(std::string_view ascii)
{
    return {ascii.begin(), ascii.end()};
}

const std::map<std::string, u16v, std::less<>> &romajiKana()
{
    static const auto map = [] {
        std::map<std::string, u16v, std::less<>> out;
        for (bool firstOnly : {true, false})
            for (const Entry &entry : kTable) {
                const auto readings = readingsOf(entry);
                for (std::size_t i = firstOnly ? 0 : 1; i < (firstOnly ? 1 : readings.size()); ++i)
                    out.emplace(std::string(readings[i]), entry.kana);
            }
        return out;
    }();
    return map;
}

bool vowel(char16_t c)
{
    return c == u'a' || c == u'i' || c == u'u' || c == u'e' || c == u'o';
}

// 0 for a character no romaji has.
char16_t romajiLetter(char16_t c)
{
    if (c >= u'A' && c <= u'Z')
        return static_cast<char16_t>(c - u'A' + u'a');
    if ((c >= u'a' && c <= u'z') || c == u'\'' || c == u'-')
        return c;
    static constexpr std::array<std::pair<u16v, char16_t>, 5> marked{{
        {u"āâáàĀÂÁÀ", u'a'}, {u"īîíìĪÎÍÌ", u'i'}, {u"ūûúùŪÛÚÙ", u'u'}, {u"ēêéèĒÊÉÈ", u'e'}, {u"ōôóòŌÔÓÒ", u'o'}}};
    for (const auto &[set, letter] : marked)
        if (set.find(c) != u16v::npos)
            return letter;
    return 0;
}

} // namespace

bool isKana(char32_t c)
{
    return (c >= 0x3041 && c <= 0x3096) || (c >= 0x309D && c <= 0x309F) || (c >= 0x30A1 && c <= 0x30FA) ||
           (c >= 0x30FC && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF);
}

char16_t toHiragana(char16_t c)
{
    return c >= 0x30A1 && c <= 0x30F6 ? static_cast<char16_t>(c - 0x60) : c;
}

std::vector<KanaReading> kanaReadings(u16v text)
{
    std::vector<KanaReading> out;
    if (text.empty())
        return out;
    const char16_t first = toHiragana(text[0]);
    if (first == kLongVowel) {
        for (char16_t v : u16v(u"aiueo"))
            out.push_back({1, u16(1, v)});
        return out;
    }
    if (first == kSmallTsu) {
        std::u16string consonants;
        for (const auto &next : text.size() > 1 && toHiragana(text[1]) != kSmallTsu ? kanaReadings(text.substr(1))
                                                                                     : std::vector<KanaReading>{}) {
            const char16_t c = next.romaji.empty() ? u'\0' : next.romaji[0] == u'c' ? u't' : next.romaji[0];
            if (kConsonants.find(static_cast<char>(c)) != std::string_view::npos && consonants.find(c) == u16::npos)
                consonants += c;
        }
        if (consonants.empty())
            consonants = widen(kConsonants);
        for (char16_t c : consonants)
            out.push_back({1, u16(1, c)});
        return out;
    }
    for (std::size_t length : {std::size_t{2}, std::size_t{1}}) {
        if (text.size() < length)
            continue;
        u16 key(1, first);
        if (length == 2)
            key += toHiragana(text[1]);
        for (const Entry &entry : kTable)
            if (entry.kana == key)
                for (std::string_view reading : readingsOf(entry))
                    out.push_back({length, widen(reading)});
    }
    return out;
}

std::optional<u16> romajiToHiragana(u16v romaji)
{
    std::string s;
    for (char16_t c : romaji) {
        if (c == u' ' || c == u'\t' || c == u'-')
            continue;
        const char16_t letter = romajiLetter(c);
        if (!letter)
            return std::nullopt;
        s += static_cast<char>(letter);
    }
    if (s.empty())
        return std::nullopt;
    const auto &map = romajiKana();
    u16 out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        const char next = i + 1 < s.size() ? s[i + 1] : '\0';
        if (c == '\'') {
            ++i;
            continue;
        }
        if (c != 'n' && !vowel(c) && ((next == c && kConsonants.find(c) != std::string_view::npos) ||
                                      (c == 't' && next == 'c'))) {
            out += kSmallTsu;
            ++i;
            continue;
        }
        if ((c == 'n' && (next == '\0' || next == '\'' || (!vowel(next) && next != 'y'))) ||
            (c == 'm' && (next == 'b' || next == 'p' || next == 'm'))) {
            out += u'ん';
            i += next == '\'' ? 2 : 1;
            continue;
        }
        bool found = false;
        for (std::size_t length : {std::size_t{3}, std::size_t{2}, std::size_t{1}}) {
            if (i + length > s.size())
                continue;
            if (const auto it = map.find(std::string_view(s).substr(i, length)); it != map.end()) {
                out += it->second;
                i += length;
                found = true;
                break;
            }
        }
        if (!found)
            return std::nullopt;
    }
    return out;
}

} // namespace hikari::application
