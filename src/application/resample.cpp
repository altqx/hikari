#include "hikari/application/resample.h"

#include "hikari/application/grid_split.h"
#include "hikari/application/script_resolution.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;
using u8 = std::u8string;

// The whole text as a C-locale number.
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
    std::size_t start = 0; // value offset in the text
};

// Named tags in override blocks; while a \p drawing is on, the text between blocks as "pvector".
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

// Skips empty tokens.
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

}

Resolution scriptResolution(const core::Document &document)
{
    return legacyScriptResolution(writtenScriptResolution(document));
}

void resizeSubtitles(core::Document &document, float xnsize, float ynsize, bool stretch, const ResampleWarning &warn)
{
    const float val = std::min(xnsize, ynsize), val1 = std::max(xnsize, ynsize);
    const float stretched = stretch ? val1 / val : 1.f;
    const bool taller = ynsize > xnsize;
    const float valFscx = taller ? 1.f : stretched, valFscy = taller ? stretched : 1.f;
    const float vectorXScale = xnsize / valFscx, vectorYScale = ynsize / valFscy;

    const auto styles = core::decodeStyles(document);
    for (std::size_t i = 0; i < styles.size(); ++i) {
        core::StyleValues s = styles[i];
        int ml = atoi8(s.marginLeft);
        ml = static_cast<int>(ml * xnsize);
        s.marginLeft = number(ml);
        int mr = atoi8(s.marginRight);
        mr = static_cast<int>(mr * xnsize);
        s.marginRight = number(mr);
        int mv = atoi8(s.marginVertical);
        mv = static_cast<int>(mv * ynsize);
        s.marginVertical = number(mv);
        const auto stretchScale = [](u8 &field, float by) {
            if (by == 1.f)
                return;
            double scale = 100;
            toCDouble(field, scale);
            field = getfloat8(scale * by);
        };
        stretchScale(s.scaleX, valFscx);
        stretchScale(s.scaleY, valFscy);
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

    static const std::vector<u16v> names{u"pos", u"move", u"bord", u"shad", u"org", u"fsp", u"fscx", u"fscy", u"fs",
                                         u"clip", u"iclip", u"p", u"xbord", u"ybord", u"xshad", u"yshad"};
    int row = 0;
    for (const auto *line : document.lines()) {
        ++row;
        if (line->comment)
            continue;
        core::LineRecord changed = *line;
        bool marginChanged = false, textChanged = false;
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
            if (tag.name == u"fscy" && valFscy == 1.f)
                continue;
            u16 resized;
            double value = 0;
            const bool clipLike = tag.name.ends_with(u"clip") || tag.name == u"pvector";
            if (clipLike && tag.value.find(u'm') != u16::npos) {
                const auto mPos = tag.value.find(u'm');
                resized = tag.value.substr(0, mPos) + u"m ";
                const bool drawing = tag.name == u"pvector";
                const float xscale = drawing ? vectorXScale : xnsize, yscale = drawing ? vectorYScale : ynsize;
                int ii = 0;
                for (u16 tkn : tokens(u16v(tag.value).substr(mPos + 1), u' ')) {
                    if (tkn != u"m" && tkn != u"l" && tkn != u"b" && tkn != u"s" && tkn != u"c") {
                        u16 lastC;
                        if (tkn.ends_with(u'c')) {
                            tkn.pop_back();
                            lastC = u"c";
                        }
                        if (toCDouble(tkn, value)) {
                            value *= (ii % 2 == 0) ? xscale : yscale;
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
                const auto comma = resized.rfind(u',');
                resized = comma == u16::npos ? u16() : resized.substr(0, comma);
            } else if (tag.name != u"p") {
                if (toCDouble(tag.value, value)) {
                    value *= tag.name == u"fscx"         ? valFscx
                             : tag.name == u"fscy"       ? valFscy
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

namespace {

namespace persp = visual::perspective;

int infoNumber(const core::Document &d, std::u8string_view key)
{
    const auto v = d.scriptInfo(key);
    return v ? atoi8(*v) : 0;
}

struct Rotation {
    double rx = 1, ry = 1, z0 = 0, z1 = 0;
    bool stretch = false;

    bool anamorphic() const { return rx != ry; }
    bool reaches(const persp::LineState &s) const
    {
        const persp::Transform &t = s.transform;
        if (t.frx != 0 || t.fry != 0)
            return anamorphic() || std::abs(z1 - z0 * ry) > 1e-9 * z1;
        const bool turned = std::remainder(t.frz, 360.) != 0;
        if (!anamorphic())
            return false;
        return stretch ? turned || t.fax != 0 || t.fay != 0 : turned && s.ownOrg;
    }
};

struct Turned {
    core::LineId id;
    int row = 0;
    persp::LineState before;
};

persp::Reader readerOf(const core::Document &d, Resolution r, const persp::TextMeasure &measure)
{
    persp::Reader out;
    out.document = &d;
    out.width = r.width;
    out.height = r.height;
    out.measure = measure;
    return out;
}

std::vector<Turned> turnedLines(const core::Document &d, Resolution from, const Rotation &rotation,
                                const ResampleOptions &options)
{
    const persp::Reader reader = readerOf(d, from, options.measure);
    persp::StyleCache styles;
    std::vector<Turned> out;
    int row = 0;
    for (const auto *line : d.lines()) {
        ++row;
        if (line->comment)
            continue;
        const persp::LineState tags = persp::readTags(reader, *line, styles);
        if (!rotation.reaches(tags))
            continue;
        const auto read = tags.hasMove ? std::nullopt : persp::readLine(reader, *line, styles);
        if (read)
            out.push_back({line->id, row, *read});
        else if (options.unadjusted)
            options.unadjusted(row);
    }
    return out;
}

std::optional<persp::Transform> keptShape(const persp::LineState &before, const persp::LineState &after,
                                          const Rotation &rotation)
{
    double dx = rotation.rx, dy = rotation.ry;
    if (rotation.anamorphic() && !rotation.stretch) {
        const double w0 = before.box.width() * before.transform.fscx;
        const double h0 = before.box.layoutHeight() * before.transform.fscy;
        if (!(w0 > 0) || !(h0 > 0))
            return std::nullopt;
        dx = after.box.width() * after.transform.fscx / w0;
        dy = after.box.layoutHeight() * after.transform.fscy / h0;
    }
    const visual::PointF c0 = persp::shownPosition(before.transform, rotation.z0);
    const auto map = [&](visual::PointF p) {
        return visual::PointF{static_cast<float>(c0.x * rotation.rx + dx * (p.x - c0.x)),
                              static_cast<float>(c0.y * rotation.ry + dy * (p.y - c0.y))};
    };
    persp::Quad quad = persp::textToQuad(before.transform, before.box, rotation.z0);
    for (visual::PointF &p : quad)
        p = map(p);
    persp::Transform from = after.transform;
    from.org = map(before.transform.org);
    from.bord = {static_cast<float>(before.transform.bord.x * dx), static_cast<float>(before.transform.bord.y * dy)};
    from.shad = {static_cast<float>(before.transform.shad.x * dx), static_cast<float>(before.transform.shad.y * dy)};
    return persp::quadToTransform(quad, after.box, persp::OrgMode::Keep, from, rotation.z1);
}

void keepShapes(core::Document &d, Resolution to, const std::vector<Turned> &turned, const Rotation &rotation,
                const ResampleOptions &options)
{
    const persp::Reader reader = readerOf(d, to, options.measure);
    persp::StyleCache styles;
    for (const Turned &line : turned) {
        const core::LineRecord *now = nullptr;
        for (const auto *l : d.lines())
            if (l->id == line.id)
                now = l;
        const auto after = now ? persp::readLine(reader, *now, styles) : std::nullopt;
        const auto t = after ? keptShape(line.before, *after, rotation) : std::nullopt;
        if (!t) {
            if (options.unadjusted)
                options.unadjusted(line.row);
            continue;
        }
        const u8 text = core::toUtf8(persp::writeTransform(reader, styles, *now, *t));
        d.editLine(line.id, [&](core::LineRecord &l) { (l.translation.empty() ? l.text : l.translation) = text; });
    }
}

}

std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, const ResampleOptions &options)
{
    const auto format = session.document().format();
    if (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText)
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touches;
    for (const auto *line : session.document().lines())
        touches.insert(line->id);
    return session.run(Command{"Changing subtitles resolution", session.revision(), touches, [&](core::Document &d) {
        const int layoutX = infoNumber(d, u8"LayoutResX"), layoutY = infoNumber(d, u8"LayoutResY");
        const bool layout = layoutX > 0 && layoutY > 0;
        const auto layoutHeight = [&](int scriptHeight) {
            return layout ? layoutY : options.frameHeight > 0 ? options.frameHeight : scriptHeight;
        };
        const Rotation rotation{to.width / static_cast<double>(from.width), to.height / static_cast<double>(from.height),
                                persp::screenZ(from.height, layoutHeight(from.height)),
                                persp::screenZ(to.height, layoutHeight(to.height)), options.stretch};
        const auto turned =
            resample && options.adjustRotations ? turnedLines(d, from, rotation, options) : std::vector<Turned>{};

        d.setScriptInfo(u8"PlayResX", number(to.width));
        d.setScriptInfo(u8"PlayResY", number(to.height));
        if (!resample)
            return true;
        if (layout) {
            const double aspectChange = (to.width / static_cast<double>(to.height)) /
                                        (from.width / static_cast<double>(from.height));
            d.setScriptInfo(u8"LayoutResX", number(std::llround(layoutX * aspectChange)));
        }
        resizeSubtitles(d, to.width / static_cast<float>(from.width), to.height / static_cast<float>(from.height),
                        options.stretch, options.unscaled);
        keepShapes(d, to, turned, rotation, options);
        return true;
    }});
}

}
