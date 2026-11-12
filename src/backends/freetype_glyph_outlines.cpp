#include "hikari/backends/freetype_glyph_outlines.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

#include <absl/container/flat_hash_map.h>

#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace hikari::backends {

namespace {

namespace shape = application::shape;

constexpr double kUpscale = 64; // outlines are read at 64 times the size, as Yutils' FONT_PRECISION
constexpr int kDecimals = 3;
constexpr FT_Fixed kItalicShear = 0x05700; // libass's ass_glyph_italicize

std::u32string decodeUtf8(std::string_view s)
{
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i]);
        const int width = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
        if (width == 0 || i + static_cast<std::size_t>(width) > s.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t c = width == 1 ? lead : lead & (0x7F >> width);
        bool valid = true;
        for (int k = 1; k < width; ++k) {
            const auto b = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            valid = valid && (b & 0xC0) == 0x80;
            c = c << 6 | (b & 0x3F);
        }
        out.push_back(valid ? c : 0xFFFD);
        i += valid ? static_cast<std::size_t>(width) : 1;
    }
    return out;
}

void appendPoint(std::string &out, double x, double y)
{
    out += ' ';
    shape::appendNumber(out, x, kDecimals);
    out += ' ';
    shape::appendNumber(out, y, kDecimals);
}

void rectangle(std::string &out, double x0, double y0, double x1, double y1)
{
    out += out.empty() ? "m" : " m";
    appendPoint(out, x0, y0);
    out += " l";
    appendPoint(out, x1, y0);
    appendPoint(out, x1, y1);
    appendPoint(out, x0, y1);
}

void appendUtf8(std::string &out, char32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

struct Metrics {
    double ascent = 0, descent = 0; // in output units, before \fscy
    double unitsToOut = 0;          // font units to output units
};

struct Pen {
    std::string *out;
    double originX = 0, baseline = 0, scale = 0, scaleX = 1, scaleY = 1;
    FT_Vector last{};
    void point(const FT_Vector *v) { appendPoint(*out, (originX + v->x * scale) * scaleX, (baseline - v->y * scale) * scaleY); }
};

int moveTo(const FT_Vector *to, void *user)
{
    auto &pen = *static_cast<Pen *>(user);
    *pen.out += pen.out->empty() ? "m" : " m";
    pen.point(to);
    pen.last = *to;
    return 0;
}

int lineTo(const FT_Vector *to, void *user)
{
    auto &pen = *static_cast<Pen *>(user);
    *pen.out += " l";
    pen.point(to);
    pen.last = *to;
    return 0;
}

int cubicTo(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
    auto &pen = *static_cast<Pen *>(user);
    *pen.out += " b";
    pen.point(c1);
    pen.point(c2);
    pen.point(to);
    pen.last = *to;
    return 0;
}

int conicTo(const FT_Vector *control, const FT_Vector *to, void *user)
{
    auto &pen = *static_cast<Pen *>(user);
    const FT_Vector from = pen.last;
    const FT_Vector c1{from.x + 2 * (control->x - from.x) / 3, from.y + 2 * (control->y - from.y) / 3};
    const FT_Vector c2{to->x + 2 * (control->x - to->x) / 3, to->y + 2 * (control->y - to->y) / 3};
    return cubicTo(&c1, &c2, to, user);
}


}

struct FreetypeGlyphOutlines::Impl {
    application::FontServicePort &fonts;
    Environment environment;
    std::mutex mutex;
    FT_Library library = nullptr;
    std::map<std::pair<std::string, long>, FT_Face> faces;
    // (family, bold, italic) -> the face libass takes for the family, and those it falls back to per character.
    struct Choice {
        std::optional<std::pair<std::string, long>> base;
        absl::flat_hash_map<char32_t, std::pair<std::string, long>> fallback;
    };
    std::map<std::tuple<std::string, bool, bool>, Choice> choices;

    Impl(application::FontServicePort &service, Environment env) : fonts(service), environment(std::move(env)) {}
    ~Impl()
    {
        for (const auto &[key, face] : faces)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }

    FT_Face open(const std::pair<std::string, long> &key)
    {
        if (const auto found = faces.find(key); found != faces.end())
            return found->second;
        FT_Face face = nullptr;
        if (FT_New_Face(library, key.first.c_str(), key.second, &face))
            face = nullptr;
        faces.emplace(key, face);
        return face;
    }

    void resolve(Choice &choice, const shape::GlyphStyle &style, const std::string &text)
    {
        const auto report = fonts.resolve(environment ? environment() : application::FontEnvironment{},
                                          {application::FontRequest{style.family, style.bold, style.italic, text}});
        if (!report || report->requests.empty())
            return;
        for (const std::size_t i : report->requests.front().faces) {
            const auto &face = report->faces[i];
            if (face.path.empty())
                continue;
            const std::pair<std::string, long> key{face.path, face.faceIndex};
            if (face.code == 0 && !choice.base)
                choice.base = key;
            else if (face.code != 0)
                choice.fallback.try_emplace(face.code, key);
        }
    }
};

FreetypeGlyphOutlines::FreetypeGlyphOutlines(application::FontServicePort &fonts, Environment environment)
    : m_impl(std::make_unique<Impl>(fonts, std::move(environment)))
{
    if (FT_Init_FreeType(&m_impl->library))
        m_impl->library = nullptr;
}

FreetypeGlyphOutlines::~FreetypeGlyphOutlines() = default;

std::optional<shape::TextOutline> FreetypeGlyphOutlines::outline(const shape::GlyphStyle &style, std::string_view utf8)
{
    if (!m_impl->library || !(style.size > 0) || !std::isfinite(style.size))
        return std::nullopt;
    const std::lock_guard lock(m_impl->mutex);
    const std::u32string text = decodeUtf8(utf8);
    auto &choice = m_impl->choices[{style.family, style.bold, style.italic}];
    if (!choice.base)
        m_impl->resolve(choice, style, {});
    if (!choice.base)
        return std::nullopt;
    FT_Face base = m_impl->open(*choice.base);
    if (!base)
        return std::nullopt;

    std::string missing;
    for (const char32_t c : text)
        if (c >= 0x20 && c != 0xFFFD && !FT_Get_Char_Index(base, c) && !choice.fallback.contains(c))
            appendUtf8(missing, c);
    if (!missing.empty())
        m_impl->resolve(choice, style, missing);

    // libass's ass_face_set_size: winAscent + winDescent is the size.
    const auto sizeFace = [&](FT_Face face) {
        const auto *os2 = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
        const auto *hhea = static_cast<const TT_HoriHeader *>(FT_Get_Sfnt_Table(face, FT_SFNT_HHEA));
        double mscale = 1;
        Metrics m;
        const int winHeight = os2 ? os2->usWinAscent + os2->usWinDescent : 0;
        if (os2 && hhea && winHeight)
            mscale = static_cast<double>(hhea->Ascender - hhea->Descender) / winHeight;
        FT_Size_RequestRec rq{};
        rq.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
        rq.height = static_cast<FT_Long>(std::llround(style.size * mscale * kUpscale * 64));
        FT_Request_Size(face, &rq);
        const double units = face->units_per_EM;
        m.unitsToOut = units ? FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / 64.0 / kUpscale / units : 0;
        if (os2 && winHeight) {
            m.ascent = style.size * os2->usWinAscent / winHeight;
            m.descent = style.size * os2->usWinDescent / winHeight;
        } else {
            m.ascent = face->size->metrics.ascender / 64.0 / kUpscale;
            m.descent = -face->size->metrics.descender / 64.0 / kUpscale;
        }
        return m;
    };

    const Metrics baseMetrics = sizeFace(base);
    const double scaleX = style.scaleX / 100, scaleY = style.scaleY / 100;
    shape::TextOutline result;
    double penX = 0;
    FT_Face previousFace = nullptr;
    FT_UInt previousGlyph = 0;
    for (const char32_t c : text) {
        FT_Face face = base;
        FT_UInt glyph = FT_Get_Char_Index(base, c);
        if (!glyph) {
            if (const auto found = choice.fallback.find(c); found != choice.fallback.end())
                if (FT_Face other = m_impl->open(found->second)) {
                    face = other;
                    glyph = FT_Get_Char_Index(other, c);
                }
        }
        if (face != base)
            sizeFace(face);
        if (face == previousFace && previousGlyph && glyph && FT_HAS_KERNING(face)) {
            FT_Vector kern{};
            if (!FT_Get_Kerning(face, previousGlyph, glyph, FT_KERNING_UNSCALED, &kern))
                penX += FT_MulFix(kern.x, face->size->metrics.x_scale) / 64.0 / kUpscale;
        }
        if (FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT) ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            previousFace = nullptr;
            continue;
        }
        FT_Outline &outline = face->glyph->outline;
        if (style.bold && !(face->style_flags & FT_STYLE_FLAG_BOLD)) {
            const FT_Pos strength = FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / 64;
            FT_Outline_Embolden(&outline, strength);
        }
        if (style.italic && !(face->style_flags & FT_STYLE_FLAG_ITALIC)) {
            FT_Matrix shear{0x10000, kItalicShear, 0, 0x10000};
            FT_Outline_Transform(&outline, &shear);
        }
        Pen pen{&result.drawing, penX, baseMetrics.ascent, 1 / (64.0 * kUpscale), scaleX, scaleY};
        const FT_Outline_Funcs funcs{moveTo, lineTo, conicTo, cubicTo, 0, 0};
        FT_Outline_Decompose(&outline, &funcs, &pen);
        penX += face->glyph->advance.x / (64.0 * kUpscale) + style.spacing;
        previousFace = face;
        previousGlyph = glyph;
    }
    const double width = penX;
    const auto lineAt = [&](double centre, double thickness) {
        rectangle(result.drawing, 0, (baseMetrics.ascent - centre - thickness / 2) * scaleY, width * scaleX,
                  (baseMetrics.ascent - centre + thickness / 2) * scaleY);
    };
    if (style.underline)
        if (const auto *post = static_cast<const TT_Postscript *>(FT_Get_Sfnt_Table(base, FT_SFNT_POST)))
            lineAt(post->underlinePosition * baseMetrics.unitsToOut,
                   std::max<double>(post->underlineThickness, 1) * baseMetrics.unitsToOut);
    if (style.strikeOut)
        if (const auto *os2 = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(base, FT_SFNT_OS2)))
            lineAt(os2->yStrikeoutPosition * baseMetrics.unitsToOut,
                   std::max<double>(os2->yStrikeoutSize, 1) * baseMetrics.unitsToOut);
    result.width = width * scaleX;
    result.ascent = baseMetrics.ascent * scaleY;
    result.descent = baseMetrics.descent * scaleY;
    result.height = result.ascent + result.descent;
    return result;
}

}
