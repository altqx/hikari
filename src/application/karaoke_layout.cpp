#include "hikari/application/karaoke_layout.h"

#include "hikari/application/karaoke_syllables.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/script_resolution.h"
#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <variant>

namespace hikari::application::karaoke_layout {

namespace {

enum StyleField { Name, Fontname, Fontsize, Primary, Secondary, OutlineColour, Back, ScaleX = 11, ScaleY, Spacing,
                  Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV };

std::string narrow(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::string trimmed(const std::string &s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

double number(const std::vector<std::string> &fields, int at)
{
    std::string text = at < static_cast<int>(fields.size()) ? trimmed(fields[at]) : std::string();
    if (!text.empty() && text.front() == '+')
        text.erase(0, 1);
    double v = 0;
    std::from_chars(text.data(), text.data() + text.size(), v);
    return v;
}

StyleColour colour(const std::vector<std::string> &fields, int at)
{
    std::string text = at < static_cast<int>(fields.size()) ? trimmed(fields[at]) : std::string();
    if (text.size() >= 2 && (text[0] == '&') && (text[1] == 'H' || text[1] == 'h'))
        text = text.substr(2);
    const unsigned long v = std::strtoul(text.c_str(), nullptr, 16);
    return {static_cast<int>(v & 0xFF), static_cast<int>((v >> 8) & 0xFF), static_cast<int>((v >> 16) & 0xFF)};
}

StyleInfo styleOf(std::u8string name, std::vector<std::string> fields)
{
    StyleInfo s;
    s.name = std::move(name);
    if (!fields.empty())
        fields.front() = narrow(s.name);
    s.fields = std::move(fields);
    const auto &f = s.fields;
    s.fontsize = number(f, Fontsize);
    s.scaleX = number(f, ScaleX);
    s.scaleY = number(f, ScaleY);
    s.spacing = number(f, Spacing);
    s.angle = number(f, Angle);
    s.outline = number(f, Outline);
    s.shadow = number(f, Shadow);
    s.align = static_cast<int>(number(f, Alignment));
    s.marginL = static_cast<int>(number(f, MarginL));
    s.marginR = static_cast<int>(number(f, MarginR));
    s.marginV = static_cast<int>(number(f, MarginV));
    s.primary = colour(f, Primary);
    s.secondary = colour(f, Secondary);
    s.outlineColour = colour(f, OutlineColour);
    s.back = colour(f, Back);
    return s;
}

// karaskel.collect_head's furigana Style.
StyleInfo furiganaOf(const StyleInfo &base)
{
    std::vector<std::string> fields = base.fields;
    fields.resize(std::max<std::size_t>(fields.size(), MarginV + 2));
    fields[Fontsize] = numberText(base.fontsize * 0.5);
    fields[Outline] = numberText(base.outline * 0.5);
    fields[Shadow] = numberText(base.shadow * 0.5);
    return styleOf(base.name + u8"-furigana", std::move(fields));
}

double rounded(double v)
{
    return std::floor(v + 0.5);
}

std::size_t firstCharBytes(const std::string &s)
{
    if (s.empty())
        return 0;
    const auto c = static_cast<unsigned char>(s[0]);
    const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    return std::min(n, s.size());
}


// syl.text:match("%{.*\\%-([^}\\]+)")
std::string inlineFx(const std::string &text)
{
    const auto open = text.find('{');
    if (open == std::string::npos)
        return {};
    for (std::size_t at = text.size(); at-- > open + 1;) {
        if (text[at] != '-' || at < open + 2 || text[at - 1] != '\\')
            continue;
        const std::size_t from = at + 1;
        std::size_t to = from;
        while (to < text.size() && text[to] != '}' && text[to] != '\\')
            ++to;
        if (to > from)
            return text.substr(from, to - from);
    }
    return {};
}

bool isBlank(const Unit &u, double duration)
{
    if (duration <= 0)
        return true;
    std::string t = u.text;
    std::string out;
    for (std::size_t at = 0; at < t.size();) {
        if (t.compare(at, 3, "\xE3\x80\x80") == 0) {
            at += 3;
            continue;
        }
        if (t[at] != ' ' && t[at] != '\t' && t[at] != '\n' && t[at] != '\r')
            out += t[at];
        ++at;
    }
    return out.empty();
}

struct Work {
    bool hasText = false;
    std::string text, stripped, spaceStripped, prespace, postspace, tag, inlineFx;
    std::vector<Highlight> highlights;
    double start = 0, end = 0, duration = 0, kdur = 0;
    int i = 0;
    std::vector<int> furi;
    double width = 0, height = 0, preWidth = 0, postWidth = 0;
    double left = 0, center = 0, right = 0;
};

struct Furi {
    std::string text, tag, inlineFx;
    double start = 0, end = 0, duration = 0, kdur = 0;
    int i = 0;
    std::size_t highlight = 0;
    bool isBreak = false, spillback = false;
    double width = 0, height = 0, left = 0, center = 0, right = 0;
};

struct Group {
    double basewidth = 0, furiwidth = 0;
    std::vector<int> syls, furi;
    bool spillback = false;
    double left = 0, right = 0;
    std::optional<double> leftspill, rightspill;
};

// karaskel.do_furigana_layout; returns the Line's width, which a wide layout widens.
double furiganaLayout(std::vector<Work> &syls, std::vector<Furi> &furi, double lineWidth)
{
    std::vector<Group> groups;
    groups.emplace_back();
    bool lastHadFuri = false;
    Group lg;
    for (std::size_t s = 0; s < syls.size(); ++s) {
        Work &syl = syls[s];
        const bool sylBreak = !syl.furi.empty() && furi[syl.furi.front()].isBreak;
        if ((syl.furi.empty() || sylBreak || !lastHadFuri) && lg.basewidth > 0) {
            groups.push_back(lg);
            lg = Group{};
            lastHadFuri = false;
        }
        lg.basewidth = lg.basewidth + syl.preWidth + syl.width + syl.postWidth;
        lg.syls.push_back(static_cast<int>(s));
        for (const int f : syl.furi) {
            lg.furiwidth = lg.furiwidth + furi[f].width;
            lg.spillback = lg.spillback || furi[f].spillback;
            lg.furi.push_back(f);
            lastHadFuri = true;
        }
    }
    groups.push_back(lg);
    groups.emplace_back();

    double curx = 0;
    for (std::size_t i = 1; i + 1 < groups.size(); ++i) {
        Group &g = groups[i];
        Group &prev = groups[i - 1];
        if (g.furiwidth == 0) {
            g.left = curx;
            g.right = g.left + g.basewidth;
            if (prev.rightspill && *prev.rightspill > 0) {
                g.leftspill = 0;
                g.rightspill = *prev.rightspill - g.basewidth;
                prev.rightspill = 0;
            }
            curx = curx + g.basewidth;
        } else if (g.furiwidth <= g.basewidth) {
            if (prev.rightspill && *prev.rightspill > 0) {
                curx = curx + *prev.rightspill;
                prev.rightspill = 0;
            }
            g.left = curx;
            g.right = g.left + g.basewidth;
            curx = curx + g.basewidth;
            g.leftspill = (g.furiwidth - g.basewidth) / 2;
            g.rightspill = g.leftspill;
        } else {
            if (prev.rightspill && *prev.rightspill > 0) {
                curx = curx + *prev.rightspill;
                prev.rightspill = 0;
            }
            if (g.spillback) {
                g.leftspill = (g.furiwidth - g.basewidth) / 2;
                g.rightspill = g.leftspill;
                g.left = prev.rightspill ? curx + *g.leftspill : curx;
            } else {
                g.leftspill = 0;
                g.rightspill = g.furiwidth - g.basewidth;
                g.left = curx;
            }
            g.right = g.left + g.basewidth;
            curx = g.right;
        }
    }

    for (Group &g : groups) {
        double x = g.left;
        for (const int s : g.syls) {
            Work &syl = syls[s];
            syl.left = x + syl.preWidth;
            syl.center = syl.left + syl.width / 2;
            syl.right = syl.left + syl.width;
            x = syl.right + syl.postWidth;
        }
        if (x > lineWidth)
            lineWidth = x;
        x = g.furiwidth < g.basewidth || g.spillback ? g.left + (g.basewidth - g.furiwidth) / 2 : g.left;
        for (const int f : g.furi) {
            furi[f].left = x;
            furi[f].center = furi[f].left + furi[f].width / 2;
            furi[f].right = furi[f].left + furi[f].width;
            x = furi[f].right;
        }
    }
    return lineWidth;
}

void basicLayout(std::vector<Work> &syls)
{
    double curx = 0;
    for (Work &syl : syls) {
        syl.left = curx + syl.preWidth;
        syl.center = syl.left + syl.width / 2;
        syl.right = syl.left + syl.width;
        curx = curx + syl.preWidth + syl.width + syl.postWidth;
    }
}

}

std::vector<std::string> charsOf(const std::string &s, CharSplit split)
{
    if (split == CharSplit::CodePoints) {
        std::vector<std::string> out;
        for (std::size_t at = 0; at < s.size();) {
            const std::size_t n = firstCharBytes(s.substr(at, 4));
            out.push_back(s.substr(at, n));
            at += n;
        }
        return out;
    }
    const std::u16string text = core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(s.data()), s.size()));
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = core::graphemeEnd(text, at);
        out.push_back(narrow(core::toUtf8(std::u16string_view(text).substr(at, end - at))));
        at = end;
    }
    return out;
}

std::string numberText(double v)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v < 0 ? "-inf" : "inf";
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::general, 14).ptr;
    return std::string(buffer, end);
}

Layout::Layout(const core::Document &document, ExtentsCache &extents, std::optional<Resolution> video)
    : m_extents(extents)
{
    std::vector<std::pair<std::u8string, std::vector<std::string>>> styles;
    for (const auto &section : document.sections()) {
        if (section.kind != core::SectionKind::Styles)
            continue;
        for (const auto &record : section.records)
            if (const auto *st = std::get_if<core::StyleRecord>(&record)) {
                std::vector<std::string> fields;
                for (const auto &f : st->fields)
                    fields.push_back(narrow(f));
                styles.emplace_back(st->name, std::move(fields));
            }
    }
    addStyles(styles, writtenScriptResolution(document), video);
}

Layout::Layout(const std::vector<MacroInfoLine> &info, const std::vector<MacroStyleLine> &styles,
               ExtentsCache &extents, std::optional<Resolution> video)
    : m_extents(extents)
{
    std::vector<std::pair<std::u8string, std::vector<std::string>>> named;
    for (const MacroStyleLine &st : styles) {
        const std::string name = st.fields.empty() ? std::string() : st.fields.front();
        named.emplace_back(std::u8string(name.begin(), name.end()), st.fields);
    }
    addStyles(named, writtenScriptResolution(info), video);
}

void Layout::addStyles(const std::vector<std::pair<std::u8string, std::vector<std::string>>> &styles,
                       Resolution written, std::optional<Resolution> video)
{
    std::vector<const StyleInfo *> order;
    for (const auto &[name, fields] : styles) {
        StyleInfo &info = m_styles[name] = styleOf(name, fields);
        order.push_back(&info);
    }
    if (!order.empty())
        m_first = order.front();
    for (const StyleInfo *base : order) {
        if (base->name.find(u8"furigana") != std::u8string::npos)
            continue;
        const std::u8string name = base->name + u8"-furigana";
        if (m_styles.contains(name) || m_added_styles.contains(name))
            continue;
        m_added_styles.emplace(name, furiganaOf(*base));
        ++m_added;
    }
    const Resolution res = lacksScriptResolution(written) ? legacyScriptResolution(written) : written;
    m_resX = res.width;
    m_resY = res.height;
    if (video && video->width > 0 && video->height > 0 && res.width > 0 && res.height > 0)
        m_factor = (double(video->height) / double(video->width)) / (m_resY / m_resX);
}

const StyleInfo *Layout::style(std::u8string_view name) const
{
    if (const auto it = m_styles.find(name); it != m_styles.end())
        return &it->second;
    if (const auto it = m_added_styles.find(name); it != m_added_styles.end())
        return &it->second;
    return nullptr;
}

std::optional<std::vector<std::u8string>> Layout::addedStyle(std::u8string_view name) const
{
    const auto it = m_added_styles.find(name);
    if (it == m_added_styles.end())
        return std::nullopt;
    std::vector<std::u8string> out;
    for (const std::string &f : it->second.fields)
        out.emplace_back(f.begin(), f.end());
    return out;
}

ExtentsCache::Result ExtentsCache::measure(const std::vector<std::string> &style, const std::string &text)
{
    std::string key;
    for (const std::string &f : style)
        key += f + ',';
    if (const auto at = m_cache.find(std::pair{key, text}); at != m_cache.end())
        return {at->second, false};
    ++m_measured;
    const auto e = m_measure.measure(style, text);
    m_cache.emplace(std::pair{std::move(key), text}, e);
    return {e, true};
}

Layout::Extents Layout::measure(const StyleInfo &style, const std::string &text, std::vector<std::string> &problems)
{
    const ExtentsCache::Result r = m_extents.measure(style.fields, text);
    if (r.extents)
        return {r.extents->width, r.extents->height, r.extents->descent, r.extents->externalLeading};
    if (r.measuredNow)
        problems.push_back("could not measure text in the Style \"" + narrow(style.name) + "\"");
    return {};
}

Line Layout::line(const MacroDialogueLine &source, double li, const LineOptions &options,
                  std::vector<std::string> &problems)
{
    Line out;
    out.macro = source;
    out.li = li;
    const std::u8string styleName(source.style.begin(), source.style.end());
    const bool chars = options.chars;
    const MacroDialogueLine &d = out.macro;
    const auto kara = karaokeSyllables(d.text, d.startMs, d.endMs);

    std::vector<Work> syls;
    std::vector<Furi> furi;
    std::string lineStripped;
    std::string inlineEffect;
    Work work;
    for (std::size_t i = 0; i < kara.size(); ++i) {
        const KaraokeSyllable &syl = kara[i];
        if (std::string fx = inlineFx(syl.text); !fx.empty())
            inlineEffect = fx;
        const std::string &st = syl.stripped;
        const std::size_t pre = std::min(st.find_first_not_of(" \t"), st.size());
        const std::size_t post = pre == st.size() ? st.size() : st.find_last_not_of(" \t") + 1;
        const std::string prespace = st.substr(0, pre), postspace = st.substr(post);
        std::string text = st.substr(pre, post - pre);
        const std::string prefix = text.substr(0, firstCharBytes(text));
        const bool hash = prefix == "#" || prefix == "\xEF\xBC\x83";
        if (!hash && i > 0) {
            work.i = static_cast<int>(syls.size());
            syls.push_back(std::move(work));
            work = Work{};
        }
        work.highlights.push_back({double(syl.start), double(syl.end), double(syl.duration)});
        constexpr std::string_view kWidePipe = "\xEF\xBD\x9C";
        if (text.find('|') != std::string::npos || text.find(kWidePipe) != std::string::npos) {
            for (std::size_t at; (at = text.find(kWidePipe)) != std::string::npos;)
                text.replace(at, kWidePipe.size(), "|");
            const auto bar = text.find('|');
            std::string furiText = text.substr(bar + 1);
            text = text.substr(0, bar);
            Furi f;
            const std::string first = furiText.substr(0, firstCharBytes(furiText));
            if (first == "!" || first == "\xEF\xBC\x81") {
                f.isBreak = true;
            } else if (first == "<" || first == "\xEF\xBC\x9C") {
                f.isBreak = true;
                f.spillback = true;
            }
            if (f.isBreak)
                furiText = furiText.substr(first.size());
            f.text = furiText;
            f.start = syl.start;
            f.end = syl.end;
            f.duration = syl.duration;
            f.kdur = syl.duration / 10.0;
            f.i = static_cast<int>(syls.size());
            f.tag = syl.tag;
            f.inlineFx = inlineEffect;
            f.highlight = work.highlights.size() - 1;
            work.furi.push_back(static_cast<int>(furi.size()));
            furi.push_back(std::move(f));
        }
        if (!work.hasText || !hash) {
            lineStripped += prespace + text + postspace;
            work.hasText = true;
            work.text = syl.text;
            work.duration = syl.duration;
            work.kdur = syl.duration / 10.0;
            work.start = syl.start;
            work.end = syl.end;
            work.tag = syl.tag;
            work.inlineFx = inlineEffect;
            work.stripped = prespace + text + postspace;
            work.spaceStripped = text;
            work.prespace = prespace;
            work.postspace = postspace;
        } else {
            work.duration += syl.duration;
            work.kdur += syl.duration / 10.0;
            work.end = syl.end;
        }
    }
    work.i = static_cast<int>(syls.size());
    syls.push_back(std::move(work));
    out.syln = static_cast<int>(syls.size()) - 1;

    const StyleInfo *styleref = style(styleName);
    if (!styleref) {
        problems.push_back("has the Style \"" + source.style + "\", which the Document lacks");
        styleref = m_first;
    }
    static const StyleInfo kNoStyle = styleOf(u8"Default", {});
    if (!styleref)
        styleref = &kNoStyle;
    out.style = styleref;
    const StyleInfo *furiStyle = style(styleName + u8"-furigana");

    const Extents whole = measure(*styleref, lineStripped, problems);
    double width = whole.width * m_factor;
    const double height = whole.height;
    for (Work &s : syls) {
        const Extents e = measure(*styleref, s.spaceStripped, problems);
        s.width = e.width * m_factor;
        s.height = e.height;
        s.preWidth = measure(*styleref, s.prespace, problems).width * m_factor;
        s.postWidth = measure(*styleref, s.postspace, problems).width * m_factor;
    }
    if (furiStyle)
        for (Furi &f : furi) {
            const Extents e = measure(*furiStyle, f.text, problems);
            f.width = e.width * m_factor;
            f.height = e.height;
        }
    if (furiStyle)
        width = furiganaLayout(syls, furi, width);
    else
        basicLayout(syls);

    const int marginL = d.marginL > 0 ? d.marginL : styleref->marginL;
    const int marginR = d.marginR > 0 ? d.marginR : styleref->marginR;
    const int marginV = d.marginV > 0 ? d.marginV : styleref->marginV;
    int align = styleref->align;
    if (align < 1 || align > 9) {
        problems.push_back("has a Style alignment outside 1 to 9");
        align = 2;
    }
    double left = 0, x = 0, top = 0, middle = 0, bottom = 0, y = 0;
    const int column = (align - 1) % 3;
    if (column == 0) {
        left = marginL;
        x = left;
    } else if (column == 1) {
        left = (m_resX - marginL - marginR - width) / 2 + marginL;
        x = left + width / 2;
    } else {
        left = m_resX - marginR - width;
        x = left + width;
    }
    if (align <= 3) {
        bottom = m_resY - marginV;
        middle = bottom - height / 2;
        top = bottom - height;
        y = bottom;
    } else if (align <= 6) {
        top = (m_resY - marginV - marginV - height) / 2 + marginV;
        middle = top + height / 2;
        bottom = top + height;
        y = middle;
    } else {
        top = marginV;
        middle = top + height / 2;
        bottom = top + height;
        y = top;
    }

    KaraLine &k = out.kara;
    k.textStripped = lineStripped;
    k.duration = static_cast<double>(d.endMs - d.startMs);
    k.width = width;
    k.height = height;
    k.descent = whole.descent;
    k.extlead = whole.extlead;
    k.effMarginL = marginL;
    k.effMarginR = marginR;
    k.effMarginT = k.effMarginB = k.effMarginV = marginV;
    k.hasFuriStyle = furiStyle != nullptr;
    if (styleref->align >= 1 && styleref->align <= 9) {
        k.left = left;
        k.center = left + width / 2;
        k.right = left + width;
        k.x = x;
        k.top = top;
        k.middle = middle;
        k.bottom = bottom;
        k.y = y;
        k.halign = column == 0 ? "left" : column == 1 ? "center" : "right";
        k.valign = align <= 3 ? "bottom" : align <= 6 ? "middle" : "top";
    }
    for (const Work &w : syls)
        out.karaSyllables.push_back({.text = w.text,
                                     .stripped = w.stripped,
                                     .spaceStripped = w.spaceStripped,
                                     .prespace = w.prespace,
                                     .postspace = w.postspace,
                                     .tag = w.tag,
                                     .inlineFx = w.inlineFx,
                                     .start = w.start,
                                     .end = w.end,
                                     .duration = w.duration,
                                     .kdur = w.kdur,
                                     .i = w.i,
                                     .highlights = w.highlights,
                                     .furi = w.furi,
                                     .width = w.width,
                                     .height = w.height,
                                     .prespaceWidth = w.preWidth,
                                     .postspaceWidth = w.postWidth,
                                     .left = w.left,
                                     .center = w.center,
                                     .right = w.right});
    for (const Furi &f : furi)
        out.karaFuri.push_back({.text = f.text,
                                .tag = f.tag,
                                .inlineFx = f.inlineFx,
                                .start = f.start,
                                .end = f.end,
                                .duration = f.duration,
                                .kdur = f.kdur,
                                .i = f.i,
                                .highlight = f.highlight,
                                .isBreak = f.isBreak,
                                .spillback = f.spillback,
                                .width = f.width,
                                .height = f.height,
                                .left = f.left,
                                .center = f.center,
                                .right = f.right});

    const double lstart = static_cast<double>(d.startMs), lend = static_cast<double>(d.endMs);
    Unit &l = out.line;
    l.text = lineStripped;
    l.raw = d.text;
    l.start = lstart;
    l.end = lend;
    l.duration = lend - lstart;
    l.kdur = std::floor(l.duration / 10);
    l.mid = lstart + l.duration / 2;
    l.relStart = 0;
    l.relEnd = l.duration;
    l.i = li;
    l.left = rounded(left);
    l.center = rounded(left + width / 2);
    l.right = rounded(left + width);
    l.width = rounded(width);
    l.top = rounded(top);
    l.middle = rounded(middle);
    l.bottom = rounded(bottom);
    l.height = rounded(height);
    l.x = rounded(x);
    l.y = rounded(y);
    l.style = styleName;

    const auto place = [&](Unit &u, double sylLeft, double sylCenter, double sylRight, double sylWidth,
                           double sylHeight, bool isFuri) {
        u.left = rounded(left + sylLeft);
        u.center = rounded(left + sylCenter);
        u.right = rounded(left + sylRight);
        u.width = rounded(sylWidth);
        if (isFuri) {
            u.bottom = l.top;
            u.top = rounded(l.top - sylHeight);
            u.middle = rounded(l.top - sylHeight / 2);
        } else {
            u.top = l.top;
            u.middle = l.middle;
            u.bottom = l.bottom;
        }
        u.height = sylHeight;
        u.x = column == 0 ? rounded(left + sylLeft) : column == 1 ? rounded(left + sylCenter) : rounded(left + sylRight);
        u.y = align >= 7 ? u.top : align >= 4 ? u.middle : u.bottom;
    };
    const auto timed = [](Unit &u, double start, double end, double duration, double kdur) {
        u.start = start;
        u.end = end;
        u.duration = duration;
        u.kdur = kdur;
        u.mid = start + duration / 2;
        u.relStart = start;
        u.relEnd = end;
    };

    for (const Work &s : syls) {
        Unit u;
        u.text = s.stripped;
        u.raw = s.text;
        u.spaceStripped = s.spaceStripped;
        u.prespace = s.prespace;
        u.postspace = s.postspace;
        u.prespaceWidth = s.preWidth;
        u.postspaceWidth = s.postWidth;
        timed(u, s.start, s.end, s.duration, s.kdur);
        u.i = s.i;
        u.style = styleName;
        place(u, s.left, s.center, s.right, s.width, s.height, false);
        u.blank = isBlank(u, s.duration);
        out.syls.push_back(std::move(u));
    }
    if (chars)
        for (std::size_t s = 0; s < syls.size(); ++s) {
            std::vector<Unit> list;
            double at = syls[s].left;
            for (const std::string &c : charsOf(syls[s].stripped, options.split)) {
                const double w = measure(*styleref, c, problems).width;
                Unit u;
                u.text = c;
                u.raw = c;
                u.spaceStripped = c;
                timed(u, syls[s].start, syls[s].end, syls[s].duration, syls[s].kdur);
                u.i = syls[s].i;
                u.style = styleName;
                place(u, at, at + w / 2, at + w, w, syls[s].height, false);
                u.blank = isBlank(u, syls[s].duration);
                at += w;
                list.push_back(std::move(u));
            }
            out.chars.push_back(std::move(list));
        }
    if (furiStyle)
        for (const Furi &f : furi) {
            Unit u;
            u.text = f.text;
            u.raw = f.text;
            timed(u, f.start, f.end, f.duration, f.kdur);
            u.i = f.i;
            u.style = furiStyle->name;
            place(u, f.left, f.center, f.right, f.width, f.height, true);
            u.blank = isBlank(u, f.duration);
            out.furi.push_back(std::move(u));
        }

    std::string sig = d.text + '\x1f' + std::to_string(d.startMs) + '\x1f' + std::to_string(d.endMs) + '\x1f' +
                      std::to_string(d.layer) + '\x1f' + d.style + '\x1f' + d.actor + '\x1f' +
                      std::to_string(d.marginL) + ',' + std::to_string(d.marginR) + ',' + std::to_string(d.marginV) +
                      '\x1f' + d.effect + '\x1f' + numberText(li) + '\x1f' + numberText(m_resX) + 'x' +
                      numberText(m_resY) + '\x1f' + numberText(m_factor) + (chars ? (options.split == CharSplit::Graphemes ? "g" : "c") : "") + '\x1f';
    for (const StyleInfo *s : {styleref, furiStyle})
        if (s)
            for (const std::string &f : s->fields)
                sig += f + ',';
    for (const auto &[k, v] : d.extra)
        sig += '\x1f' + k + '=' + v;
    out.signature = std::move(sig);
    return out;
}

}
