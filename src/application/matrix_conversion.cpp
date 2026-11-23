#include "hikari/application/matrix_conversion.h"

#include <cstdint>
#include <string>
#include <utility>

namespace hikari::application {

namespace {

using Coefficients = std::array<double, 3>;

struct Header {
    Coefficients k;
    bool fullRange = false;
};

std::optional<Header> header(std::string_view name)
{
    static constexpr std::pair<std::string_view, Coefficients> kMatrices[] = {
        {"601", {.299, .587, .114}}, {"709", {.2126, .7152, .0722}}, {"FCC", {.3, .59, .11}}, {"240M", {.212, .701, .087}}};
    const bool tv = name.starts_with("TV."), pc = name.starts_with("PC.");
    if (!tv && !pc)
        return std::nullopt;
    for (const auto &[matrix, k] : kMatrices)
        if (name.substr(3) == matrix)
            return Header{k, pc};
    return std::nullopt;
}

std::array<double, 3> product(const std::array<double, 9> &m, const std::array<double, 3> &v)
{
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

std::int64_t channel(double value)
{
    const auto truncated = static_cast<std::int64_t>(value + .5);
    return truncated > 255 ? 255 : truncated < 0 ? 0 : truncated;
}

bool isHex(char8_t c)
{
    return (c >= u8'0' && c <= u8'9') || (c >= u8'A' && c <= u8'F') || (c >= u8'a' && c <= u8'f');
}

std::u8string bgrHex(const core::Colour &c)
{
    static constexpr char8_t kDigits[] = u8"0123456789ABCDEF";
    std::u8string out;
    for (const auto value : {c.b, c.g, c.r}) {
        out += kDigits[(value >> 4) & 0xF];
        out += kDigits[value & 0xF];
    }
    return out;
}

// libass reads a colour tag's value as it does: any run of '&' and 'H', then hex digits as 0xAABBGGRR, the
// alpha byte ignored.
bool convertTags(std::u8string &text, const MatrixConversion &conversion)
{
    bool changed = false, block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{' || text[i] == u8'}')
            block = text[i] == u8'{';
        if (!block || text[i] != u8'\\')
            continue;
        std::size_t p = i + 1;
        if (p < text.size() && text[p] >= u8'1' && text[p] <= u8'4')
            ++p;
        if (p >= text.size() || text[p] != u8'c')
            continue;
        ++p;
        while (p < text.size() && (text[p] == u8'&' || text[p] == u8'H'))
            ++p;
        const std::size_t start = p;
        while (p < text.size() && isHex(text[p]))
            ++p;
        const std::size_t digits = p - start;
        if (digits == 0 || digits > 8)
            continue;
        const auto value = std::stoul(std::string(text.begin() + start, text.begin() + p), nullptr, 16);
        const core::Colour colour{std::int64_t(value & 0xFF), std::int64_t((value >> 8) & 0xFF),
                                  std::int64_t((value >> 16) & 0xFF), 0};
        const std::size_t kept = digits > 6 ? digits - 6 : 0;
        text.replace(start + kept, digits - kept, bgrHex(conversion.convert(colour)));
        changed = true;
        i = start + kept + 5;
    }
    return changed;
}

}

bool namesConvertibleMatrix(std::string_view name)
{
    return header(name).has_value();
}

std::optional<MatrixConversion> MatrixConversion::between(std::string_view from, std::string_view to)
{
    const auto source = header(from), target = header(to);
    if (!source || !target || from == to)
        return std::nullopt;
    MatrixConversion c;
    {
        const auto [kr, kg, kb] = source->k;
        const Coefficients rows = source->fullRange ? Coefficients{1., .5, .5}
                                                    : Coefficients{219. / 255., 112. / 255., 112. / 255.};
        c.m_toYcbcr = {kr * rows[0], kg * rows[0], kb * rows[0], -kr / (1 - kb) * rows[1], -kg / (1 - kb) * rows[1],
                       rows[1],      rows[2],      -kg / (1 - kr) * rows[2], -kb / (1 - kr) * rows[2]};
        c.m_shiftTo = source->fullRange ? Coefficients{0, 128., 128.} : Coefficients{16., 128., 128.};
    }
    {
        const auto [kr, kg, kb] = target->k;
        const Coefficients columns = target->fullRange ? Coefficients{1., 2., 2.}
                                                       : Coefficients{255. / 219., 255. / 112., 255. / 112.};
        c.m_fromYcbcr = {columns[0], 0, (1 - kr) * columns[2], columns[0], -(1 - kb) * kb / kg * columns[1],
                         -(1 - kr) * kr / kg * columns[2], columns[0], (1 - kb) * columns[1], 0};
        c.m_shiftFrom = target->fullRange ? Coefficients{0, -128., -128.} : Coefficients{-16., -128., -128.};
    }
    return c;
}

core::Colour MatrixConversion::convert(core::Colour colour) const
{
    auto ycbcr = product(m_toYcbcr, {double(colour.r), double(colour.g), double(colour.b)});
    for (std::size_t i = 0; i < 3; ++i)
        ycbcr[i] += m_shiftTo[i] + m_shiftFrom[i];
    const auto rgb = product(m_fromYcbcr, ycbcr);
    return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), colour.a};
}

void convertColours(core::Document &document, const MatrixConversion &conversion)
{
    const auto styles = core::decodeStyles(document);
    for (std::size_t i = 0; i < styles.size(); ++i) {
        core::StyleValues s = styles[i];
        for (auto *colour : {&s.primary, &s.secondary, &s.outline, &s.back})
            *colour = conversion.convert(*colour);
        if (s.primary != styles[i].primary || s.secondary != styles[i].secondary || s.outline != styles[i].outline ||
            s.back != styles[i].back)
            document.editStyle(i, core::legacy::styleRawFields(s));
    }
    document.editLines([&](core::LineRecord &line) {
        if (line.unparsed ||
            (line.comment && (line.effect.starts_with(u8"template") || line.effect.starts_with(u8"code"))))
            return false;
        const bool text = convertTags(line.text, conversion);
        return convertTags(line.translation, conversion) || text;
    });
}

}
