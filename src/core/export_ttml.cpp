#include "export_flat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <map>
#include <numeric>
#include <set>

namespace hikari::core::detail {

namespace {

enum Band { Top, Middle, Bottom };

constexpr std::array<u8sv, 3> kRegionIds = {u8"top", u8"middle", u8"bottom"};
constexpr std::array<u8sv, 3> kDisplayAlign = {u8"before", u8"center", u8"after"};
constexpr std::array<u8sv, 3> kTextAlign = {u8"left", u8"center", u8"right"};

std::u8string escaped(u8sv text)
{
    std::u8string out;
    for (const char8_t c : text) {
        if (c == u8'&')
            out += u8"&amp;";
        else if (c == u8'<')
            out += u8"&lt;";
        else if (c == u8'>')
            out += u8"&gt;";
        else if (c == u8'"')
            out += u8"&quot;";
        else
            out += c;
    }
    return out;
}

std::u8string attribute(u8sv name, u8sv value)
{
    return u8" " + std::u8string(name) + u8"=\"" + escaped(value) + u8"\"";
}

std::u8string rgba(const Colour &c)
{
    const std::u8string alpha = hex(255 - (c.a & 0xFF), true);
    return u8"#" + hex(rgbOf(c), true) + alpha.substr(alpha.size() - 2);
}

std::u8string flooredPercent(double v)
{
    return decimal(std::floor(std::clamp(v, 0.0, 100.0) * 100) / 100) + u8"%";
}

bool unquotedFamily(u8sv name)
{
    static constexpr std::array<u8sv, 9> kGeneric = {u8"default", u8"monospace", u8"sansSerif", u8"serif",
                                                     u8"monospaceSansSerif", u8"monospaceSerif",
                                                     u8"proportionalSansSerif", u8"proportionalSerif", u8"inherit"};
    if (name.empty() || (name.front() >= u8'0' && name.front() <= u8'9') || name.front() == u8'-' ||
        std::ranges::find(kGeneric, name) != kGeneric.end())
        return false;
    return std::ranges::all_of(name, [](char8_t c) {
        return (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9') ||
               c == u8'-' || c == u8'_';
    });
}

std::u8string fontFamily(u8sv name)
{
    if (unquotedFamily(name))
        return std::u8string(name) + u8",proportionalSansSerif";
    std::u8string quoted = u8"\"";
    for (const char8_t c : name) {
        if (c == u8'"' || c == u8'\\')
            quoted += u8'\\';
        quoted += c;
    }
    return quoted + u8"\",proportionalSansSerif";
}

struct Rate {
    std::int64_t frameRate = 24, numerator = 1, denominator = 1;
    double frames() const { return static_cast<double>(frameRate * numerator) / static_cast<double>(denominator); }
};

// ttp:frameRate is a whole number; NTSC rates take a 1000/1001 multiplier.
Rate rateOf(double fps)
{
    if (!std::isfinite(fps) || fps < 1)
        fps = 24000.0 / 1001.0;
    if (const auto ntsc = std::llround(fps * 1.001); std::abs(fps - static_cast<double>(ntsc) * 1000.0 / 1001.0) < 0.0005)
        return {ntsc, 1000, 1001};
    const std::int64_t whole = std::max<std::int64_t>(1, std::llround(fps));
    std::int64_t numerator = std::llround(fps * 1000), denominator = whole * 1000;
    const auto g = std::gcd(numerator, denominator);
    return {whole, numerator / g, denominator / g};
}

std::u8string timeOf(std::int64_t us, const Rate *rate)
{
    if (rate)
        return ascii(std::to_string(std::llround(static_cast<double>(us) * rate->frames() / 1e6))) + u8"f";
    return clock(us, 2, u8'.');
}

struct Margins {
    int left = 0, right = 0, vertical = 0;
    auto operator<=>(const Margins &) const = default;
};

Margins marginsOf(const Flat &f)
{
    return {f.marginL, f.marginR, f.marginV};
}

// The margins most Lines use, the earliest on a tie.
Margins commonMargins(const std::vector<Flat> &cues, PlayRes res)
{
    std::map<Margins, std::pair<int, std::size_t>> counts;
    for (std::size_t i = 0; i < cues.size(); ++i) {
        auto &[count, first] = counts.try_emplace(marginsOf(cues[i]), 0, i).first->second;
        ++count;
    }
    Margins best;
    std::pair<int, std::size_t> bestCount{0, 0};
    for (const auto &[margins, count] : counts)
        if (count.first > bestCount.first || (count.first == bestCount.first && count.second < bestCount.second)) {
            best = margins;
            bestCount = count;
        }
    if (best.left < 0 || best.right < 0 || best.left + best.right >= res.x)
        best.left = best.right = 0;
    if (best.vertical < 0 || best.vertical * 2 >= res.y)
        best.vertical = 0;
    return best;
}

struct Placement {
    Band band = Bottom;
    int horizontal = 1; // 0 left, 1 centre, 2 right
    bool approximated = false;
};

Placement placement(const Flat &f, const Margins &margins, PlayRes res, bool netflix)
{
    Placement p;
    p.horizontal = (f.alignment - 1) % 3;
    const int vertical = (f.alignment - 1) / 3;
    p.band = vertical == 2 ? Top : vertical == 1 ? Middle : Bottom;
    if (netflix) {
        if (f.pos)
            p.band = f.pos->second < res.y / 2 ? Top : Bottom;
        p.approximated = f.pos.has_value() || p.band == Middle || p.horizontal != 1;
        if (p.band == Middle)
            p.band = Bottom;
        p.horizontal = 1;
        return p;
    }
    if (f.pos) {
        const double y = f.pos->second;
        p.band = y < res.y / 3 ? Top : y < res.y * 2 / 3 ? Middle : Bottom;
        p.approximated = true;
        return p;
    }
    p.approximated = f.marginL != margins.left || f.marginR != margins.right ||
                     (p.band != Middle && f.marginV != margins.vertical);
    return p;
}

std::u8string region(Band band, const Margins &m, PlayRes res, bool netflix)
{
    std::u8string origin, extent;
    if (netflix) {
        origin = band == Top ? u8"10% 10%" : u8"10% 50%";
        extent = u8"80% 40%";
    } else {
        const double x = m.left * 100 / res.x, width = (res.x - m.left - m.right) * 100 / res.x;
        const double y = m.vertical * 100 / res.y, third = (res.y - 2.0 * m.vertical) * 100 / res.y / 3;
        origin = flooredPercent(x) + u8" " + flooredPercent(y + third * static_cast<int>(band));
        extent = flooredPercent(width) + u8" " + flooredPercent(third);
    }
    return u8"      <region" + attribute(u8"xml:id", kRegionIds[band]) + attribute(u8"tts:origin", origin) +
           attribute(u8"tts:extent", extent) + attribute(u8"tts:displayAlign", kDisplayAlign[band]) + u8"/>\n";
}

std::u8string decorations(bool underline, bool strike)
{
    if (underline && strike)
        return u8"underline lineThrough";
    return underline ? u8"underline" : u8"lineThrough";
}

std::u8string styleAttributes(const StyleValues &s, PlayRes res, bool netflix)
{
    if (netflix)
        return s.italic ? attribute(u8"tts:fontStyle", u8"italic") : std::u8string();
    std::u8string a;
    if (!s.fontname.empty())
        a += attribute(u8"tts:fontFamily", fontFamily(s.fontname));
    const double size = leadingDouble(s.fontsize);
    // ttp:cellResolution is 32 15 by default, so the initial font size, 1c, is a 15th of the height.
    if (size > 0)
        a += attribute(u8"tts:fontSize", decimal(size * 1500 / res.y) + u8"%");
    a += attribute(u8"tts:color", rgba(s.primary));
    if (s.bold)
        a += attribute(u8"tts:fontWeight", u8"bold");
    if (s.italic)
        a += attribute(u8"tts:fontStyle", u8"italic");
    if (s.underline || s.strikeOut)
        a += attribute(u8"tts:textDecoration", decorations(s.underline, s.strikeOut));
    const double outline = std::round(std::min(0.1, leadingDouble(s.outlineWidth) / std::max(size, 1.0)) * 100) / 100;
    if (s.borderStyle != 3 && s.borderStyle != 4 && size > 0 && outline > 0)
        a += attribute(u8"tts:textOutline", rgba(s.outline) + u8" " + decimal(outline) + u8"em");
    return a;
}

bool boxed(const StyleValues *s, bool netflix)
{
    return !netflix && s && (s->borderStyle == 3 || s->borderStyle == 4);
}

std::u8string spanAttributes(const Look &look, const Look &base, bool netflix)
{
    std::u8string a;
    if (look.italic != base.italic)
        a += attribute(u8"tts:fontStyle", look.italic ? u8"italic" : u8"normal");
    if (netflix)
        return a;
    if (look.bold != base.bold)
        a += attribute(u8"tts:fontWeight", look.bold ? u8"bold" : u8"normal");
    if (look.underline != base.underline || look.strike != base.strike)
        a += attribute(u8"tts:textDecoration", std::u8string(look.underline ? u8"underline" : u8"noUnderline") +
                                                   (look.strike ? u8" lineThrough" : u8" noLineThrough"));
    if (look.colour != base.colour)
        a += attribute(u8"tts:color", u8"#" + hex(look.colour, true));
    return a;
}

class Ids {
public:
    Ids() : m_taken(kRegionIds.begin(), kRegionIds.end()) {}
    std::u8string unique(const std::u8string &base)
    {
        std::u8string id = base;
        for (int n = 2; m_taken.contains(id); ++n)
            id = base + u8"_" + ascii(std::to_string(n));
        m_taken.insert(id);
        return id;
    }

private:
    std::set<std::u8string, std::less<>> m_taken;
};

}

std::uint32_t ttmlLosses(const ExportOptions &o, const Flat &f)
{
    std::uint32_t l = f.dropped;
    if (f.karaoke)
        l |= bit(ExportLoss::Karaoke);
    if (o.ttmlProfile == TtmlProfile::Netflix) {
        if (f.colourTag)
            l |= bit(ExportLoss::Colour);
        if (f.formattingTag && std::ranges::any_of(f.rows, [](const Row &r) {
                return std::ranges::any_of(r, [](const Piece &p) { return p.look.bold || p.look.underline || p.look.strike; });
            }))
            l |= bit(ExportLoss::Formatting);
    }
    return l;
}

void writeTtml(std::u8string &out, const std::vector<Flat> &cues, const ExportOptions &o, PlayRes res, bool imsc,
               std::map<std::uint64_t, std::uint32_t> &losses)
{
    const bool netflix = o.ttmlProfile == TtmlProfile::Netflix;
    const Margins margins = commonMargins(cues, res);
    const Rate rate = rateOf(o.fps);
    const Rate *frames = netflix || o.ttmlFrames ? &rate : nullptr;

    Ids ids;
    std::map<const StyleValues *, std::u8string> styleIds, boxIds;
    std::u8string styling;
    std::array<bool, 3> bands{};
    std::u8string body;
    for (const auto &cue : cues) {
        if (cue.style && !styleIds.contains(cue.style)) {
            const auto attributes = styleAttributes(*cue.style, res, netflix);
            const auto id = attributes.empty() ? std::u8string()
                                               : ids.unique(identifierFor(cue.style->name));
            styleIds.emplace(cue.style, id);
            if (!id.empty())
                styling += u8"      <style" + attribute(u8"xml:id", id) + attributes + u8"/>\n";
            if (boxed(cue.style, netflix)) {
                const auto box = boxIds.emplace(cue.style, ids.unique(id + u8"_box")).first->second;
                styling += u8"      <style" + attribute(u8"xml:id", box) +
                           attribute(u8"tts:backgroundColor", rgba(cue.style->outline)) + u8"/>\n";
            }
        }
        const Placement place = placement(cue, margins, res, netflix);
        if (place.approximated)
            losses[cue.line->id.value] |= bit(ExportLoss::Position);
        bands[place.band] = true;

        body += u8"      <p" + attribute(u8"begin", timeOf(cue.startUs, frames)) +
                attribute(u8"end", timeOf(cue.endUs, frames)) + attribute(u8"region", kRegionIds[place.band]);
        if (cue.style && !styleIds.at(cue.style).empty())
            body += attribute(u8"style", styleIds.at(cue.style));
        body += attribute(u8"tts:textAlign", kTextAlign[place.horizontal]) + u8">";
        const auto box = boxIds.find(cue.style);
        if (box != boxIds.end())
            body += u8"<span" + attribute(u8"style", box->second) + u8">";
        const Look base = lookOf(cue.style);
        body += joinedRows(cue, u8"<br/>", [&](const Row &row) {
            std::u8string text;
            for (const auto &piece : row) {
                const auto span = spanAttributes(piece.look, base, netflix);
                text += span.empty() ? escaped(piece.text) : u8"<span" + span + u8">" + escaped(piece.text) + u8"</span>";
            }
            return text;
        });
        if (box != boxIds.end())
            body += u8"</span>";
        body += u8"</p>\n";
    }

    out += u8"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tt xmlns=\"http://www.w3.org/ns/ttml\" "
           u8"xmlns:ttp=\"http://www.w3.org/ns/ttml#parameter\" xmlns:tts=\"http://www.w3.org/ns/ttml#styling\"" +
           attribute(u8"xml:lang", o.language);
    if (imsc)
        out += attribute(u8"ttp:profile", u8"http://www.w3.org/ns/ttml/profile/imsc1/text");
    if (frames) {
        out += attribute(u8"ttp:frameRate", ascii(std::to_string(rate.frameRate)));
        if (rate.numerator != rate.denominator)
            out += attribute(u8"ttp:frameRateMultiplier",
                             ascii(std::to_string(rate.numerator) + " " + std::to_string(rate.denominator)));
    }
    out += u8">\n";
    const bool anyBand = std::ranges::any_of(bands, [](bool b) { return b; });
    if (!styling.empty() || anyBand) {
        out += u8"  <head>\n";
        if (!styling.empty())
            out += u8"    <styling>\n" + styling + u8"    </styling>\n";
        if (anyBand) {
            out += u8"    <layout>\n";
            for (const Band band : {Top, Middle, Bottom})
                if (bands[band])
                    out += region(band, margins, res, netflix);
            out += u8"    </layout>\n";
        }
        out += u8"  </head>\n";
    }
    out += u8"  <body>\n    <div>\n" + body + u8"    </div>\n  </body>\n</tt>\n";
}

}
