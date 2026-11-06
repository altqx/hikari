#include "hikari/core/srv3.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace hikari::core {

namespace {

using u8sv = std::u8string_view;

constexpr u8sv kKeyPrefix = u8"Hikari srv3 ";

constexpr std::pair<u8sv, std::uint8_t> kShadowWords[] = {
    {u8"glow", Srv3Glow},
    {u8"soft", Srv3SoftShadow},
    {u8"hard", Srv3HardShadow},
    {u8"bevel", Srv3Bevel},
};

constexpr std::pair<std::u16string_view, std::uint8_t> kBuiltIns[] = {
    {u"YTPlain", 0},
    {u"YTPlainBox", 0},
    {u"YTGlow", Srv3Glow},
    {u"YTGlowBox", Srv3Glow},
    {u"YTSoftShadow", Srv3SoftShadow},
    {u"YTSoftShadowBox", Srv3SoftShadow},
    {u"YTHardShadow", Srv3HardShadow},
    {u"YTHardShadowBox", Srv3HardShadow},
    {u"YTBevel", Srv3Bevel},
    {u"YTBevelBox", Srv3Bevel},
    {u"__ytplain", 0},
    {u"__ytglow", Srv3Glow},
    {u"__ytsoftshadow", Srv3SoftShadow},
    {u"__ythardshadow", Srv3HardShadow},
    {u"__ytbevel", Srv3Bevel},
};

bool isOptionsKey(u8sv key)
{
    if (!key.starts_with(kKeyPrefix) || key.size() == kKeyPrefix.size())
        return false;
    for (const char8_t c : key.substr(kKeyPrefix.size()))
        if (c < u8'0' || c > u8'9')
            return false;
    return true;
}

std::optional<std::optional<std::uint32_t>> colourField(u8sv field)
{
    if (field.empty())
        return std::optional<std::uint32_t>();
    if (field.size() != 7 || field[0] != u8'#')
        return std::nullopt;
    std::uint32_t rgb = 0;
    const char *first = reinterpret_cast<const char *>(field.data()) + 1;
    const auto [end, error] = std::from_chars(first, first + 6, rgb, 16);
    if (error != std::errc() || end != first + 6)
        return std::nullopt;
    return std::optional<std::uint32_t>(rgb);
}

std::optional<std::uint8_t> shadowField(u8sv field)
{
    if (field == u8"none")
        return std::uint8_t(0);
    std::uint8_t shadows = 0;
    while (!field.empty()) {
        const std::size_t plus = field.find(u8'+');
        const u8sv word = field.substr(0, plus);
        bool known = false;
        for (const auto &[name, bit] : kShadowWords)
            if (word == name) {
                shadows |= bit;
                known = true;
            }
        if (!known)
            return std::nullopt;
        field = plus == u8sv::npos ? u8sv() : field.substr(plus + 1);
    }
    return shadows;
}

std::optional<Srv3StyleOptions> parsed(u8sv value)
{
    std::array<u8sv, 6> fields;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::size_t comma = value.find(u8',');
        if ((comma == u8sv::npos) != (i + 1 == fields.size()))
            return std::nullopt;
        fields[i] = value.substr(0, comma);
        value = comma == u8sv::npos ? u8sv() : value.substr(comma + 1);
    }
    const auto shadows = shadowField(fields[1]);
    const auto text = colourField(fields[3]), outline = colourField(fields[4]), shadow = colourField(fields[5]);
    if (fields[0].empty() || !shadows || (fields[2] != u8"karaoke" && fields[2] != u8"-") || !text || !outline || !shadow)
        return std::nullopt;
    return Srv3StyleOptions{.style = toUtf16(fields[0]),
                            .shadows = *shadows,
                            .karaoke = fields[2] == u8"karaoke",
                            .currentWordText = *text,
                            .currentWordOutline = *outline,
                            .currentWordShadow = *shadow};
}

std::u8string keyFor(std::size_t number)
{
    const std::string digits = std::to_string(number);
    return std::u8string(kKeyPrefix) + std::u8string(digits.begin(), digits.end());
}

std::u8string colourText(const std::optional<std::uint32_t> &rgb)
{
    if (!rgb)
        return {};
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::u8string out = u8"#";
    for (int shift = 20; shift >= 0; shift -= 4)
        out += static_cast<char8_t>(kHex[(*rgb >> shift) & 0xF]);
    return out;
}

std::u8string valueOf(const Srv3StyleOptions &o)
{
    std::u8string shadows;
    for (const auto &[name, bit] : kShadowWords)
        if (o.shadows & bit)
            shadows += (shadows.empty() ? u8"" : u8"+") + std::u8string(name);
    return toUtf8(o.style) + u8',' + (shadows.empty() ? u8"none" : shadows) + u8',' + (o.karaoke ? u8"karaoke" : u8"-") +
           u8',' + colourText(o.currentWordText) + u8',' + colourText(o.currentWordOutline) + u8',' +
           colourText(o.currentWordShadow);
}

template <typename Visit>
void eachProperty(const Document &document, Visit visit)
{
    for (const auto &section : document.sections()) {
        if (section.kind != SectionKind::ScriptInfo)
            continue;
        for (const auto &record : section.records)
            if (const auto *property = std::get_if<PropertyRecord>(&record))
                visit(*property);
    }
}

}

bool isSrv3BuiltInStyle(std::u16string_view style)
{
    return std::ranges::any_of(kBuiltIns, [&](const auto &builtIn) { return builtIn.first == style; });
}

Srv3StyleOptions srv3DefaultOptions(std::u16string_view style)
{
    for (const auto &[name, shadows] : kBuiltIns)
        if (name == style)
            return {.style = std::u16string(style), .shadows = shadows};
    return {.style = std::u16string(style), .shadows = Srv3SoftShadow};
}

std::vector<Srv3StyleOptions> srv3StyleOptions(const Document &document)
{
    std::vector<Srv3StyleOptions> out;
    eachProperty(document, [&](const PropertyRecord &property) {
        if (isOptionsKey(property.key))
            if (auto o = parsed(property.value))
                out.push_back(std::move(*o));
    });
    return out;
}

bool setSrv3StyleOptions(Document &document, const std::vector<Srv3StyleOptions> &options)
{
    std::vector<Document::PropertySlot> block;
    for (std::size_t i = 0; i < options.size(); ++i)
        block.push_back({.property = std::pair{keyFor(i + 1), valueOf(options[i])}});
    std::vector<Document::PropertySlot> slots;
    bool placed = false, any = false;
    std::size_t index = 0;
    eachProperty(document, [&](const PropertyRecord &property) {
        any = true;
        if (!isOptionsKey(property.key)) {
            slots.push_back({.from = index++});
            return;
        }
        ++index;
        if (!placed)
            slots.insert(slots.end(), block.begin(), block.end());
        placed = true;
    });
    if (!any) {
        for (const auto &slot : block)
            if (!document.setScriptInfo(slot.property->first, slot.property->second))
                return false;
        return true;
    }
    if (!placed)
        slots.insert(slots.end(), block.begin(), block.end());
    return document.rearrangeScriptInfo(slots);
}

}
