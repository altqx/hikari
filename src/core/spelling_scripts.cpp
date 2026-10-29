#include "hikari/core/spelling_scripts.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace hikari::core::spelling {

namespace {

struct Range {
    char32_t first;
    char32_t last;
    Script script;
};

// Letter blocks by start; combining diacritics (U+0300-036F), digits and
// punctuation are in none.
constexpr Range kRanges[] = {
    {0x0041, 0x005A, Script::Latin},      {0x0061, 0x007A, Script::Latin},      {0x00AA, 0x00AA, Script::Latin},
    {0x00BA, 0x00BA, Script::Latin},      {0x00C0, 0x00D6, Script::Latin},      {0x00D8, 0x00F6, Script::Latin},
    {0x00F8, 0x02AF, Script::Latin},      {0x0370, 0x03FF, Script::Greek},      {0x0400, 0x052F, Script::Cyrillic},
    {0x0531, 0x058F, Script::Armenian},   {0x0591, 0x05FF, Script::Hebrew},     {0x0600, 0x06FF, Script::Arabic},
    {0x0750, 0x077F, Script::Arabic},     {0x0870, 0x08FF, Script::Arabic},     {0x0900, 0x097F, Script::Devanagari},
    {0x0980, 0x09FF, Script::Bengali},    {0x0A00, 0x0A7F, Script::Gurmukhi},   {0x0A80, 0x0AFF, Script::Gujarati},
    {0x0B00, 0x0B7F, Script::Oriya},      {0x0B80, 0x0BFF, Script::Tamil},      {0x0C00, 0x0C7F, Script::Telugu},
    {0x0C80, 0x0CFF, Script::Kannada},    {0x0D00, 0x0D7F, Script::Malayalam},  {0x0D80, 0x0DFF, Script::Sinhala},
    {0x0E00, 0x0E7F, Script::Thai},       {0x0E80, 0x0EFF, Script::Lao},        {0x0F00, 0x0FFF, Script::Tibetan},
    {0x1000, 0x109F, Script::Myanmar},    {0x10A0, 0x10FF, Script::Georgian},   {0x1100, 0x11FF, Script::Hangul},
    {0x1200, 0x139F, Script::Ethiopic},   {0x1780, 0x17FF, Script::Khmer},
    {0x1C80, 0x1C8F, Script::Cyrillic},   {0x1C90, 0x1CBF, Script::Georgian},   {0x1D00, 0x1D7F, Script::Latin},
    {0x1E00, 0x1EFF, Script::Latin},      {0x1F00, 0x1FFF, Script::Greek},      {0x2C60, 0x2C7F, Script::Latin},
    {0x2D00, 0x2D2F, Script::Georgian},   {0x2D80, 0x2DDF, Script::Ethiopic},   {0x2DE0, 0x2DFF, Script::Cyrillic},
    {0x2E80, 0x2FDF, Script::Han},        {0x3005, 0x3005, Script::Han},        {0x3007, 0x3007, Script::Han},
    {0x3021, 0x3029, Script::Han},        {0x3041, 0x30FF, Script::Kana},       {0x3131, 0x318E, Script::Hangul},
    {0x31F0, 0x31FF, Script::Kana},       {0x3400, 0x4DBF, Script::Han},        {0x4E00, 0x9FFF, Script::Han},
    {0xA640, 0xA69F, Script::Cyrillic},   {0xA720, 0xA7FF, Script::Latin},      {0xA8E0, 0xA8FF, Script::Devanagari},
    {0xA960, 0xA97F, Script::Hangul},     {0xA9E0, 0xA9FF, Script::Myanmar},    {0xAA60, 0xAA7F, Script::Myanmar},
    {0xAB30, 0xAB6F, Script::Latin},      {0xAC00, 0xD7A3, Script::Hangul},     {0xD7B0, 0xD7FF, Script::Hangul},
    {0xF900, 0xFAFF, Script::Han},        {0xFB00, 0xFB06, Script::Latin},      {0xFB13, 0xFB17, Script::Armenian},
    {0xFB1D, 0xFB4F, Script::Hebrew},     {0xFB50, 0xFDFF, Script::Arabic},     {0xFE70, 0xFEFF, Script::Arabic},
    {0xFF21, 0xFF3A, Script::Latin},      {0xFF41, 0xFF5A, Script::Latin},      {0xFF66, 0xFF9D, Script::Kana},
    {0xFFA0, 0xFFDC, Script::Hangul},     {0x11FC0, 0x11FFF, Script::Tamil},    {0x1B000, 0x1B16F, Script::Kana},
    {0x20000, 0x3134F, Script::Han},
};

struct Numbers {
    char32_t first;
    char32_t last;
};

// Digits and number signs inside the letter blocks, by start: not letters, so
// "١wrold" (one ICU word) is Latin alone.
constexpr Numbers kNumbers[] = {
    {0x0660, 0x0669}, {0x06F0, 0x06F9}, {0x0966, 0x096F}, {0x09E6, 0x09EF}, {0x0A66, 0x0A6F},
    {0x0AE6, 0x0AEF}, {0x0B66, 0x0B6F}, {0x0BE6, 0x0BF2}, {0x0C66, 0x0C6F}, {0x0CE6, 0x0CEF},
    {0x0D66, 0x0D6F}, {0x0DE6, 0x0DEF}, {0x0E50, 0x0E59}, {0x0ED0, 0x0ED9}, {0x0F20, 0x0F33},
    {0x1040, 0x1049}, {0x1090, 0x1099}, {0x1369, 0x137C}, {0x17E0, 0x17E9}, {0x17F0, 0x17F9},
    {0xA9F0, 0xA9F9}, {0x11FC0, 0x11FD4},
};

struct Language {
    std::u16string_view tag;
    Scripts scripts;
};

// Language subtags whose dictionaries are not Latin, in tag order.
constexpr Language kLanguages[] = {
    {u"am", Script::Ethiopic},   {u"ar", Script::Arabic},     {u"as", Script::Bengali},
    {u"ba", Script::Cyrillic},   {u"be", Script::Cyrillic},   {u"bg", Script::Cyrillic},
    {u"bn", Script::Bengali},    {u"bo", Script::Tibetan},    {u"ckb", Script::Arabic},
    {u"cv", Script::Cyrillic},   {u"dz", Script::Tibetan},    {u"el", Script::Greek},
    {u"fa", Script::Arabic},     {u"grc", Script::Greek},     {u"gu", Script::Gujarati},
    {u"he", Script::Hebrew},     {u"hi", Script::Devanagari}, {u"hy", Script::Armenian},
    {u"iw", Script::Hebrew},     {u"ja", Scripts(Script::Kana) | Script::Han},
    {u"ka", Script::Georgian},   {u"kk", Script::Cyrillic},   {u"km", Script::Khmer},
    {u"kn", Script::Kannada},    {u"ko", Script::Hangul},     {u"kok", Script::Devanagari},
    {u"ky", Script::Cyrillic},   {u"lo", Script::Lao},        {u"mai", Script::Devanagari},
    {u"mk", Script::Cyrillic},   {u"ml", Script::Malayalam},  {u"mn", Script::Cyrillic},
    {u"mr", Script::Devanagari}, {u"my", Script::Myanmar},    {u"ne", Script::Devanagari},
    {u"or", Script::Oriya},      {u"pa", Script::Gurmukhi},   {u"ps", Script::Arabic},
    {u"ru", Script::Cyrillic},   {u"sa", Script::Devanagari}, {u"si", Script::Sinhala},
    {u"sr", Script::Cyrillic},   {u"ta", Script::Tamil},      {u"te", Script::Telugu},
    {u"tg", Script::Cyrillic},   {u"th", Script::Thai},       {u"ti", Script::Ethiopic},
    {u"tt", Script::Cyrillic},   {u"ug", Script::Arabic},     {u"uk", Script::Cyrillic},
    {u"ur", Script::Arabic},     {u"yi", Script::Hebrew},     {u"zh", Script::Han},
};

// ISO 15924 script subtags (sr_Latn, zh_Hant), lower-cased.
constexpr Language kScriptTags[] = {
    {u"arab", Script::Arabic},   {u"armn", Script::Armenian}, {u"beng", Script::Bengali},
    {u"cyrl", Script::Cyrillic}, {u"deva", Script::Devanagari}, {u"ethi", Script::Ethiopic},
    {u"geor", Script::Georgian}, {u"grek", Script::Greek},    {u"gujr", Script::Gujarati},
    {u"guru", Script::Gurmukhi}, {u"hang", Script::Hangul},   {u"hani", Script::Han},
    {u"hans", Script::Han},      {u"hant", Script::Han},      {u"hebr", Script::Hebrew},
    {u"hira", Script::Kana},     {u"jpan", Scripts(Script::Kana) | Script::Han},
    {u"kana", Script::Kana},     {u"khmr", Script::Khmer},    {u"knda", Script::Kannada},
    {u"kore", Script::Hangul},   {u"laoo", Script::Lao},      {u"latn", Script::Latin},
    {u"mlym", Script::Malayalam}, {u"mymr", Script::Myanmar}, {u"orya", Script::Oriya},
    {u"sinh", Script::Sinhala},  {u"taml", Script::Tamil},    {u"telu", Script::Telugu},
    {u"thai", Script::Thai},     {u"tibt", Script::Tibetan},
};

const Language *findTag(std::u16string_view tag, std::span<const Language> table)
{
    const auto it = std::ranges::lower_bound(table, tag, {}, &Language::tag);
    return it != table.end() && it->tag == tag ? &*it : nullptr;
}

template <typename F> void forEachCodePoint(std::u16string_view text, F &&f)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t c = text[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        f(c);
    }
}

// Latin-script languages with Hunspell dictionaries (LibreOffice's and
// Aegisub's), in tag order.
constexpr std::u16string_view kLatinLanguages[] = {
    u"af", u"an", u"ast", u"az", u"br", u"bs", u"ca", u"cs", u"cy", u"da", u"de", u"en", u"eo", u"es",
    u"et", u"eu", u"fi", u"fil", u"fo", u"fr", u"fur", u"fy", u"ga", u"gd", u"gl", u"gug", u"ha", u"haw",
    u"hr", u"ht", u"hu", u"ia", u"id", u"is", u"it", u"ku", u"la", u"lb", u"lt", u"lv", u"mg", u"mi",
    u"ms", u"mt", u"nb", u"nl", u"nn", u"no", u"oc", u"pl", u"pt", u"qu", u"rm", u"ro", u"rw", u"sc",
    u"sk", u"sl", u"so", u"sq", u"sv", u"sw", u"tl", u"tn", u"tr", u"uz", u"vi", u"wa", u"xh", u"yo",
    u"zu",
};

bool isLatinLanguage(std::u16string_view language)
{
    return std::ranges::binary_search(kLatinLanguages, language);
}

std::u16string lower(std::u16string_view s)
{
    std::u16string out(s);
    for (auto &c : out)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c + 32);
    return out;
}

} // namespace

bool scriptOf(char32_t c, Script &out)
{
    const auto it = std::ranges::upper_bound(kRanges, c, {}, &Range::first);
    if (it == std::ranges::begin(kRanges))
        return false;
    const Range &range = *std::prev(it);
    if (c > range.last)
        return false;
    if (const auto n = std::ranges::upper_bound(kNumbers, c, {}, &Numbers::first);
        n != std::ranges::begin(kNumbers) && c <= std::prev(n)->last)
        return false;
    out = range.script;
    return true;
}

Scripts wordScripts(std::u16string_view word)
{
    Scripts out;
    forEachCodePoint(word, [&](char32_t c) {
        Script s;
        if (scriptOf(c, s))
            out |= s;
    });
    return out;
}

bool dominantScript(std::u16string_view text, Script &out)
{
    std::array<std::size_t, 32> counts{};
    forEachCodePoint(text, [&](char32_t c) {
        Script s;
        if (scriptOf(c, s))
            ++counts[static_cast<std::size_t>(s)];
    });
    const auto best = std::ranges::max_element(counts);
    if (*best == 0)
        return false;
    out = static_cast<Script>(best - counts.begin());
    return true;
}

Scripts dictionaryScripts(std::u16string_view symbol)
{
    std::vector<std::u16string_view> parts;
    std::size_t from = 0;
    while (from <= symbol.size()) {
        std::size_t at = symbol.find_first_of(u"_-.@", from);
        if (at == std::u16string_view::npos)
            at = symbol.size();
        parts.push_back(symbol.substr(from, at - from));
        from = at + 1;
    }
    for (std::size_t i = 1; i < parts.size(); ++i)
        if (parts[i].size() == 4)
            if (const auto *tag = findTag(lower(parts[i]), kScriptTags))
                return tag->scripts;
    const std::u16string language = lower(parts.front());
    if (const auto *row = findTag(language, kLanguages))
        return row->scripts;
    if (isLatinLanguage(language))
        return Script::Latin;
    return {};
}

} // namespace hikari::core::spelling
