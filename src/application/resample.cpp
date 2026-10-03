#include "hikari/application/resample.h"

#include "hikari/application/grid_split.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <clocale>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;
using u8 = std::u8string;

// wxString::ToCDouble: the whole text as a C-locale number.
bool toCDouble(u16v text, double &out)
{
    std::string ascii;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        ascii += static_cast<char>(c);
    }
    if (ascii.empty())
        return false;
    char *end = nullptr;
    const double value = std::strtod(ascii.c_str(), &end);
    if (end == ascii.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

bool toCDouble(const u8 &text, double &out)
{
    return toCDouble(core::toUtf16(text), out);
}

// wxAtoi.
int atoi8(const u8 &text)
{
    return std::atoi(reinterpret_cast<const char *>(text.c_str()));
}

u8 number(long long value)
{
    const std::string s = std::to_string(value);
    return u8(s.begin(), s.end());
}

u16 getfloat(double value)
{
    return core::toUtf16(legacy::floatText(static_cast<float>(value)));
}

u8 getfloat8(double value)
{
    return legacy::floatText(static_cast<float>(value));
}

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

struct TagData {
    u16 name;
    u16 value;
    bool multiValue = false;
    std::size_t start = 0; // where the value starts in the text
};

// Dialogue::ParseTags(tags, n, false): the named tags in blocks, and while a
// \p drawing is on, the text between blocks as "pvector".
std::vector<TagData> parseTags(u16v txt, const std::vector<u16v> &names)
{
    std::vector<TagData> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0, plainStart = 0;
    bool tagsBlock = false, drawing = false;
    if (len < 1)
        return out;
    while (pos < len) {
        const char16_t ch = txt[pos];
        if (ch == u'}') {
            tagsBlock = false;
            plainStart = pos + 1;
        } else if (ch == u'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
            if (drawing && plainStart + 1 <= pos)
                out.push_back({u"pvector", u16(txt.substr(plainStart, pos - plainStart)), false, plainStart});
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slash = txt.find(u'\\', pos), bracket = txt.find(u'}', pos);
            const std::size_t tagEnd = slash == u16v::npos && bracket == u16v::npos ? len
                                       : slash == u16v::npos                     ? bracket
                                       : bracket == u16v::npos                   ? slash
                                                                                 : std::min(slash, bracket);
            u16 tag(txt.substr(pos, tagEnd - pos));
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16v name : names) {
                if (tag.size() <= name.size() || u16v(tag).substr(0, name.size()) != name)
                    continue;
                const char16_t first = tag[name.size()];
                if (!(first == u'(' || isDigit(first) || first == u'.' || first == u'-' || first == u'+'))
                    continue;
                TagData data{u16(name), {}, false, pos + name.size()};
                u16 value = tag.substr(name.size());
                if (name == u"p") {
                    // Trim() both sides, in place.
                    const auto b = value.find_first_not_of(u" \t\r\n");
                    const auto e = value.find_last_not_of(u" \t\r\n");
                    value = b == u16::npos ? u16() : value.substr(b, e - b + 1);
                    drawing = value != u"0";
                    data.value = value;
                } else if (first == u'(') {
                    ++data.start;
                    const auto open = value.find(u'(');
                    u16 inner = value.substr(open + 1);
                    data.value = inner.substr(0, inner.find(u')'));
                    data.multiValue = true;
                } else {
                    double ignored = 0;
                    if (!toCDouble(value, ignored)) {
                        u16 digits;
                        for (const char16_t c : value) {
                            if (!isDigit(c) && c != u'.' && c != u'-' && c != u'+')
                                break;
                            digits += c;
                        }
                        value = digits;
                    }
                    data.value = value;
                }
                out.push_back(std::move(data));
                pos = tagEnd - 1;
                break;
            }
        }
        ++pos;
    }
    return out;
}

// wxStringTokenizer(text, delims, wxTOKEN_STRTOK): no empty tokens.
std::vector<u16> tokens(u16v text, char16_t delim)
{
    std::vector<u16> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto j = std::min(text.find(delim, i), text.size());
        if (j > i)
            out.emplace_back(text.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

u16 trimmed(u16v s)
{
    const auto b = s.find_first_not_of(u" \t\r\n");
    if (b == u16v::npos)
        return {};
    const auto e = s.find_last_not_of(u" \t\r\n");
    return u16(s.substr(b, e - b + 1));
}

} // namespace

Resolution scriptResolution(const core::Document &document)
{
    int x = atoi8(document.scriptInfo(u8"PlayResX").value_or(u8""));
    int y = atoi8(document.scriptInfo(u8"PlayResY").value_or(u8""));
    if (x < 1 && y < 1)
        return {1280, 720};
    if (x < 1)
        x = static_cast<int>(static_cast<float>(y) * (16.0 / 9.0));
    else if (y < 1)
        y = static_cast<int>(static_cast<float>(x) * (9.0 / 16.0));
    return {x, y};
}

void resizeSubtitles(core::Document &document, float xnsize, float ynsize, bool stretch, const ResampleWarning &warn)
{
    float val = xnsize, val1 = ynsize, valFscx = 1.f, vectorXScale = xnsize;
    bool resizeScale = false;
    if (ynsize != xnsize) {
        if (ynsize > xnsize) {
            resizeScale = stretch;
            valFscx = stretch ? ynsize / xnsize : 1.f;
        } else {
            val = ynsize;
            val1 = xnsize;
            resizeScale = stretch;
            valFscx = stretch ? xnsize / ynsize : 1.f;
        }
        if (stretch)
            vectorXScale /= valFscx;
    }

    const auto styles = core::decodeStyles(document);
    for (std::size_t i = 0; i < styles.size(); ++i) {
        core::StyleValues s = styles[i];
        // int *= float truncates.
        int ml = atoi8(s.marginLeft);
        ml = static_cast<int>(ml * xnsize);
        s.marginLeft = number(ml);
        int mr = atoi8(s.marginRight);
        mr = static_cast<int>(mr * xnsize);
        s.marginRight = number(mr);
        int mv = atoi8(s.marginVertical);
        mv = static_cast<int>(mv * ynsize);
        s.marginVertical = number(mv);
        if (resizeScale) {
            double fscx = 100;
            toCDouble(s.scaleX, fscx);
            fscx *= valFscx;
            s.scaleX = getfloat8(fscx);
        }
        // GetFontSizeDouble: a number, else its integer part.
        double fs = 0;
        if (!toCDouble(s.fontsize, fs))
            fs = atoi8(s.fontsize);
        fs *= val;
        s.fontsize = getfloat8(fs);
        double ol = 0;
        toCDouble(s.outlineWidth, ol);
        s.outlineWidth = getfloat8(ol * val);
        double sh = 0;
        toCDouble(s.shadow, sh);
        s.shadow = getfloat8(sh * val);
        double fsp = 0;
        toCDouble(s.spacing, fsp);
        s.spacing = getfloat8(fsp * val);
        document.editStyle(i, core::legacy::styleRawFields(s));
    }

    static const std::vector<u16v> names{u"pos", u"move", u"bord", u"shad", u"org", u"fsp", u"fscx", u"fs",
                                         u"clip", u"iclip", u"p", u"xbord", u"ybord", u"xshad", u"yshad"};
    int row = 0;
    for (const auto *line : document.lines()) {
        ++row;
        if (line->comment)
            continue;
        core::LineRecord changed = *line;
        bool marginChanged = false, textChanged = false;
        // Dialogue margins are ints: MarginL *= xnsize truncates.
        auto scaleMargin = [&](core::IntField &field, float scale) {
            const int v = static_cast<int>(field.value);
            if (!v)
                return;
            field.value = static_cast<int>(v * scale);
            field.lexeme = number(field.value);
            marginChanged = true;
        };
        scaleMargin(changed.marginLeft, xnsize);
        scaleMargin(changed.marginRight, xnsize);
        scaleMargin(changed.marginVertical, ynsize);

        const bool translated = !changed.translation.empty();
        u16 txt = core::toUtf16(translated ? changed.translation : changed.text);
        const auto tags = parseTags(txt, names);
        auto log = [&](const u16 &value, const u16 &tag) {
            if (warn)
                warn(row, value, tag);
        };
        for (auto it = tags.rbegin(); it != tags.rend(); ++it) {
            const TagData &tag = *it;
            u16 resized;
            double value = 0;
            const bool clipLike = tag.name.ends_with(u"clip") || tag.name == u"pvector";
            if (clipLike && tag.value.find(u'm') != u16::npos) {
                const auto mPos = tag.value.find(u'm');
                resized = tag.value.substr(0, mPos) + u"m ";
                const float xscale = tag.name == u"pvector" ? vectorXScale : xnsize;
                int ii = 0;
                for (u16 tkn : tokens(u16v(tag.value).substr(mPos + 1), u' ')) {
                    if (tkn != u"m" && tkn != u"l" && tkn != u"b" && tkn != u"s" && tkn != u"c") {
                        u16 lastC;
                        if (tkn.ends_with(u'c')) {
                            tkn.pop_back();
                            lastC = u"c";
                        }
                        if (toCDouble(tkn, value)) {
                            value *= (ii % 2 == 0) ? xscale : ynsize;
                            resized += getfloat(value) + lastC + u" ";
                        } else {
                            log(tkn, tag.name);
                            resized += tkn + lastC + u" ";
                        }
                        ++ii;
                    } else {
                        resized += tkn + u" ";
                    }
                }
                // Trim(): trailing whitespace.
                while (!resized.empty() && (resized.back() == u' ' || resized.back() == u'\t'))
                    resized.pop_back();
            } else if (tag.multiValue) {
                int ii = 0;
                for (u16 tkn : tokens(tag.value, u',')) {
                    tkn = trimmed(tkn);
                    if (ii < 4 && toCDouble(tkn, value)) {
                        value *= (ii % 2 == 0) ? xnsize : ynsize;
                        resized += getfloat(value) + u",";
                    } else {
                        if (ii < 4)
                            log(tkn, tag.name);
                        resized += tkn + u",";
                    }
                    ++ii;
                }
                // BeforeLast(','): empty when there is no comma.
                const auto comma = resized.rfind(u',');
                resized = comma == u16::npos ? u16() : resized.substr(0, comma);
            } else if (tag.name != u"p") {
                if (toCDouble(tag.value, value)) {
                    value *= tag.name == u"fscx"         ? valFscx
                             : tag.name == u"fs"         ? val1
                             : tag.name.starts_with(u'x') ? xnsize
                             : tag.name.starts_with(u'y') ? ynsize
                                                          : val;
                    resized = getfloat(value);
                } else {
                    log(tag.value, tag.name);
                    resized = tag.value;
                }
            } else {
                continue;
            }
            txt.replace(tag.start, tag.value.size(), resized);
            textChanged = true;
        }
        if (!marginChanged && !textChanged)
            continue;
        if (textChanged)
            (translated ? changed.translation : changed.text) = core::toUtf8(txt);
        document.editLine(line->id, [&](core::LineRecord &l) { l = changed; });
    }
}

std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, bool stretch, const ResampleWarning &warn)
{
    const auto format = session.document().format();
    if (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText)
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touches;
    for (const auto *line : session.document().lines())
        touches.insert(line->id);
    return session.run(Command{"Changing subtitles resolution", session.revision(), touches, [&](core::Document &d) {
        d.setScriptInfo(u8"PlayResX", number(to.width));
        d.setScriptInfo(u8"PlayResY", number(to.height));
        // Legacy tests LayoutResX twice.
        if (!d.scriptInfo(u8"LayoutResX").value_or(u8"").empty()) {
            d.setScriptInfo(u8"LayoutResX", number(to.width));
            d.setScriptInfo(u8"LayoutResY", number(to.height));
        }
        if (resample)
            resizeSubtitles(d, to.width / static_cast<float>(from.width), to.height / static_cast<float>(from.height),
                            stretch, warn);
        return true;
    }});
}

} // namespace hikari::application
