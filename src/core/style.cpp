#include "hikari/core/style.h"

#include "text_util.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace hikari::core {

using namespace detail;

namespace {

// wxStringTokenizer(text, ",") in its default mode for a non-space delimiter
// (wxTOKEN_RET_EMPTY): empty tokens between commas, but none after a final comma.
std::vector<u8sv> tokens(u8sv s)
{
    std::vector<u8sv> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == u8',') {
            if (i == s.size() && start == s.size() && (s.empty() || s.back() == u8','))
                break;
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

// wxString::ToLong: strtol over the whole string; on failure the target is unchanged.
bool toLong(u8sv text, int base, std::int64_t &value)
{
    const std::string s(text.begin(), text.end());
    if (s.empty())
        return false;
    errno = 0;
    char *end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, base);
    if (errno == ERANGE || end != s.c_str() + s.size())
        return false;
    value = v;
    return true;
}

// wxString::IsNumber: an optional sign, then only digits.
bool isNumber(u8sv s)
{
    if (s.empty())
        return false;
    std::size_t i = s.front() == u8'+' || s.front() == u8'-' ? 1 : 0;
    for (; i < s.size(); ++i)
        if (s[i] < u8'0' || s[i] > u8'9')
            return false;
    return true;
}

} // namespace

namespace legacy {

Colour colour(std::u8string_view text)
{
    Colour c;
    if (isNumber(text)) {
        std::int64_t v = 0;
        toLong(text, 10, v);
        c.r = v & 0xFF;
        c.g = (v >> 8) & 0xFF;
        c.b = (v >> 16) & 0xFF;
        c.a = (v >> 24) & 0xFF;
        return c;
    }
    std::u8string s;
    for (char8_t ch : text)
        s += ch >= u8'a' && ch <= u8'z' ? static_cast<char8_t>(ch - 32) : ch;
    const bool html = !s.empty() && s.front() == u8'#';
    std::erase(s, u8'&');
    std::erase(s, u8'H');
    std::erase(s, u8'#');
    if (s.size() > 7) {
        toLong(wxSubString(s, 0, 1), 16, c.a);
        s = s.substr(2);
    }
    u8sv red = wxSubString(s, 4, 5), green = wxSubString(s, 2, 3), blue = wxSubString(s, 0, 1);
    if (html)
        std::swap(red, blue);
    toLong(red, 16, c.r);
    toLong(green, 16, c.g);
    toLong(blue, 16, c.b);
    return c;
}

StyleValues decodeStyle(std::u8string_view styleLine, bool ssa)
{
    StyleValues v;
    const auto t = tokens(styleLine);
    std::size_t i = 0;
    auto next = [&](u8sv &out) {
        if (i >= t.size())
            return false;
        out = t[i++];
        return true;
    };
    auto flag = [&](bool &out) {
        u8sv tok;
        if (!next(tok))
            return false;
        out = tok != u8"0";
        return true;
    };
    auto text = [&](std::u8string &out) {
        u8sv tok;
        if (!next(tok))
            return false;
        out = std::u8string(tok);
        return true;
    };
    auto col = [&](Colour &out) {
        u8sv tok;
        if (!next(tok))
            return false;
        out = colour(tok);
        return true;
    };
    u8sv tok;
    if (!next(tok))
        return v;
    const auto space = tok.find(u8' ');
    v.name = space == u8sv::npos ? std::u8string{} : std::u8string(tok.substr(space + 1));
    if (!text(v.fontname) || !text(v.fontsize) || !col(v.primary) || !col(v.secondary))
        return v;
    if (!ssa) {
        if (!col(v.outline) || !col(v.back))
            return v;
    } else {
        std::u8string tertiary;
        if (!text(tertiary))
            return v;
        v.ssaTertiaryColour = tertiary;
        if (!col(v.outline))
            return v;
        v.back = v.outline;
    }
    if (!flag(v.bold) || !flag(v.italic))
        return v;
    if (!ssa) {
        if (!flag(v.underline) || !flag(v.strikeOut) || !text(v.scaleX) || !text(v.scaleY) || !text(v.spacing) ||
            !text(v.angle))
            return v;
    } else {
        v.scaleX = u8"100";
        v.scaleY = u8"100";
        v.spacing = u8"0";
        v.angle = u8"0";
    }
    if (!next(tok))
        return v;
    v.borderStyle = tok == u8"3";
    if (!text(v.outlineWidth) || !text(v.shadow) || !text(v.alignment))
        return v;
    if (ssa) {
        // SSA numbering: 1-3 bottom, 9-11 middle, 5-7 top.
        static constexpr std::pair<std::u8string_view, std::u8string_view> renumber[] = {
            {u8"9", u8"4"}, {u8"10", u8"5"}, {u8"11", u8"6"}, {u8"5", u8"7"}, {u8"6", u8"8"}, {u8"7", u8"9"}};
        for (const auto &[from, to] : renumber)
            if (v.alignment == from) {
                v.alignment = to;
                break;
            }
    }
    if (!text(v.marginLeft) || !text(v.marginRight) || !text(v.marginVertical))
        return v;
    if (ssa) {
        std::u8string alpha;
        if (!text(alpha))
            return v;
        v.ssaAlphaLevel = alpha;
    }
    if (!text(v.encoding))
        return v;
    v.encoding = std::u8string(trimRight(v.encoding));
    v.complete = true;
    return v;
}

std::optional<std::u8string> styleTagValue(const StyleValues &style, std::u8string_view tag)
{
    auto hex2 = [](std::int64_t v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned int>(v));
        return std::u8string(reinterpret_cast<const char8_t *>(buf));
    };
    auto ass = [&](const Colour &c) { return u8"&H" + hex2(c.b) + hex2(c.g) + hex2(c.r) + u8"&"; };
    auto flag = [](bool on) { return std::u8string(on ? u8"1" : u8"0"); };
    if (tag == u8"fs")
        return style.fontsize;
    if (tag == u8"bord")
        return style.outlineWidth;
    if (tag == u8"shad")
        return style.shadow;
    if (tag == u8"fsp")
        return style.spacing;
    if (tag == u8"fscx")
        return style.scaleX;
    if (tag == u8"fscy")
        return style.scaleY;
    if (tag == u8"c" || tag == u8"1c")
        return ass(style.primary);
    if (tag == u8"2c")
        return ass(style.secondary);
    if (tag == u8"3c")
        return ass(style.outline);
    if (tag == u8"4c")
        return ass(style.back);
    if (tag == u8"1a")
        return hex2(style.primary.a);
    if (tag == u8"2a")
        return hex2(style.secondary.a);
    if (tag == u8"3a")
        return hex2(style.outline.a);
    if (tag == u8"4a")
        return hex2(style.back.a);
    if (tag == u8"fn")
        return style.fontname;
    if (tag == u8"b")
        return flag(style.bold);
    if (tag == u8"i")
        return flag(style.italic);
    if (tag == u8"u")
        return flag(style.underline);
    if (tag == u8"s")
        return flag(style.strikeOut);
    if (tag == u8"fr" || tag == u8"frz")
        return style.angle;
    return std::nullopt;
}

} // namespace legacy

std::vector<StyleValues> decodeStyles(const Document &document)
{
    std::vector<StyleValues> out;
    const auto &bytes = document.source().bytes;
    bool ssa = false;
    for (const auto &section : document.sections()) {
        if (section.kind == SectionKind::SsaStyles)
            ssa = true; // never reset: "[V4+" after "[V4" keeps the SSA layout
        for (const auto &record : section.records)
            if (const auto *style = std::get_if<StyleRecord>(&record)) {
                if (style->inserted || style->edited) {
                    // Added or changed in the editor: the fields as written.
                    std::u8string line = u8"Style: ";
                    for (std::size_t i = 0; i < style->fields.size(); ++i)
                        line += (i ? u8"," : u8"") + style->fields[i];
                    out.push_back(legacy::decodeStyle(line, ssa));
                    continue;
                }
                const u8sv raw(reinterpret_cast<const char8_t *>(bytes.data() + style->span.offset),
                               style->span.length);
                out.push_back(legacy::decodeStyle(trimLeft(raw), ssa));
            }
    }
    return out;
}

namespace legacy {

namespace {

std::u8string hexColour(const Colour &c)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "&H%02X%02X%02X%02X", static_cast<unsigned>(c.a & 0xFF), static_cast<unsigned>(c.b & 0xFF),
                  static_cast<unsigned>(c.g & 0xFF), static_cast<unsigned>(c.r & 0xFF));
    return std::u8string(reinterpret_cast<const char8_t *>(buf));
}

} // namespace

std::vector<std::u8string> styleRawFields(const StyleValues &s)
{
    using u8 = std::u8string;
    const auto flag = [](bool v) { return v ? u8(u8"-1") : u8(u8"0"); };
    return {s.name,         s.fontname,       s.fontsize,          hexColour(s.primary), hexColour(s.secondary),
            hexColour(s.outline), hexColour(s.back), flag(s.bold), flag(s.italic),   flag(s.underline),
            flag(s.strikeOut), s.scaleX,       s.scaleY,            s.spacing,            s.angle,
            s.borderStyle ? u8(u8"3") : u8(u8"1"), s.outlineWidth, s.shadow, s.alignment, s.marginLeft,
            s.marginRight,  s.marginVertical, s.encoding};
}

} // namespace legacy

} // namespace hikari::core
