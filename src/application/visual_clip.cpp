#include "hikari/application/visual_clip.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <climits>
#include <cmath>

// Built without floating-point contraction so float results match MSVC x64.

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

constexpr u16v kVectorClip = u"(i?clip\\(.*m[^)]*)\\)";

u16 number(long long value)
{
    const std::string s = std::to_string(value);
    return u16(s.begin(), s.end());
}

std::size_t count(u16v text, char16_t c)
{
    std::size_t n = 0;
    for (const char16_t x : text)
        n += x == c;
    return n;
}

u16 beforeFirst(u16v text, char16_t c, u16 *rest = nullptr)
{
    const auto at = text.find(c);
    if (at == u16v::npos) {
        if (rest)
            rest->clear();
        return u16(text);
    }
    if (rest)
        *rest = u16(text.substr(at + 1));
    return u16(text.substr(0, at));
}

u16 afterFirst(u16v text, char16_t c)
{
    const auto at = text.find(c);
    return at == u16v::npos ? u16() : u16(text.substr(at + 1));
}

void replaceAll(u16 &text, u16v what, u16v with)
{
    std::size_t at = 0;
    while ((at = text.find(what, at)) != u16::npos) {
        text.replace(at, what.size(), with);
        at += with.size();
    }
}

void replaceFound(const core::legacy::TagEditor &found, u16v replaceTxt, u16 &text)
{
    const auto [x, y] = found.position();
    if (text.empty()) {
        text += u"{" + u16(replaceTxt) + u"}";
        return;
    }
    if (!found.inBracket()) {
        text.insert(static_cast<std::size_t>(x), u"{" + u16(replaceTxt) + u"}");
        return;
    }
    if (x < y) {
        if (static_cast<std::size_t>(y) + 1u >= text.length())
            text.erase(static_cast<std::size_t>(x));
        else
            text.erase(static_cast<std::size_t>(x), static_cast<std::size_t>(y - x + 1));
    }
    text.insert(static_cast<std::size_t>(x), replaceTxt);
}

core::legacy::TagEditor find(u16v text, u16v pattern, bool *found = nullptr)
{
    core::legacy::TagEditor editor({u16(text), 0, 0});
    const bool f = editor.findTag(pattern, 1, false);
    if (found)
        *found = f;
    return editor;
}

std::optional<core::LineRecord> activeRecord(const VisualHost &host)
{
    const EditSession *session = host.session();
    const auto active = host.activeLine();
    if (!session || !active)
        return std::nullopt;
    if (const auto draft = session->draftRecord(); draft && draft->id == *active)
        return draft;
    for (const auto *line : session->document().lines())
        if (line->id == *active)
            return *line;
    return std::nullopt;
}

bool editsTranslation(const VisualHost &host, const core::LineRecord &line)
{
    return host.session() && translationMode(*host.session()) && !line.translation.empty();
}

u16 editedText(const VisualHost &host, const core::LineRecord &line)
{
    return core::toUtf16(editsTranslation(host, line) ? line.translation : line.text);
}

// MUST KILL: `began` with no open gesture means Esc cancelled it; nullptr until the next press
Gesture *ensureGesture(VisualHost &host, Family family, bool &began)
{
    if (Gesture *g = host.gesture())
        return g;
    if (began)
        return nullptr;
    auto begun = host.beginGesture(host.batchTargets(), std::string(familyInfo(family).history));
    if (!begun)
        return nullptr;
    began = true;
    return *begun;
}

void stage(Gesture &g, core::LineId id, bool tl, const u16 &before, const u16 &after)
{
    if (after != before || g.staged(id, tl))
        g.stage(id, core::toUtf8(after), tl);
}

u16 currentText(const VisualHost &host, const Gesture &g, core::LineId id, bool *tl)
{
    const core::LineRecord &before = g.before(id);
    *tl = editsTranslation(host, before);
    if (const auto staged = g.staged(id, *tl))
        return core::toUtf16(*staged);
    return editedText(host, before);
}

void commitInvert(VisualHost &host, bool vector)
{
    const auto active = activeRecord(host);
    if (!active || host.gesture())
        return;
    const u16 name = clip::invertedName(editedText(host, *active), vector);
    if (name.empty())
        return;
    // MUST KILL: rectangle inversions record under the vector clip's history name
    auto begun = host.beginGesture(host.batchTargets(), std::string(familyInfo(Family::VectorClip).history));
    if (!begun)
        return;
    Gesture *g = *begun;
    for (const auto &id : g->targets()) {
        const core::LineRecord &before = g->before(id);
        const bool tl = editsTranslation(host, before);
        if (const auto renamed = clip::renameClips(editedText(host, before), name, vector))
            g->stage(id, core::toUtf8(*renamed), tl);
    }
    (void)host.commitGesture();
}

}

namespace clip {

u16 maskTag(u16v text)
{
    bool found = false;
    const auto tagged = find(text, u"(i?clip.)[^)]*\\)", &found);
    if (!found || tagged.finding().empty())
        return {};
    return tagged.finding()[0] == u'c' ? u"iclip(" : u"clip(";
}

u16 replaceTag(u16 text, u16v pattern, u16v tag)
{
    const auto found = find(text, pattern);
    replaceFound(found, tag, text);
    return text;
}

RectangleRead readRectangle(u16v text, int scriptWidth, int scriptHeight)
{
    RectangleRead out;
    out.x2 = scriptWidth;
    out.y2 = scriptHeight;
    bool found = false;
    const auto editor = find(text, u"(i?clip[^\\)]+)", &found);
    const u16 &finding = editor.finding();
    if (found && count(finding, u',') == 3) {
        static const core::LegacyRegex re(u"\\(([0-9.-]+)[, ]*([0-9.-]+)[, ]*([0-9.-]+)[, ]*([0-9.-]+)",
                                          core::LegacyRegex::Advanced);
        if (re.matches(finding)) {
            const auto group = [&](std::size_t i) {
                const auto m = re.match(i);
                return m ? legacy::atoi(u16v(finding).substr(m->first, m->second)) : 0;
            };
            out.x1 = group(1);
            out.y1 = group(2);
            out.x2 = group(3);
            out.y2 = group(4);
            out.shown = true;
            out.inverse = finding.starts_with(u"i");
        }
    } else {
        out.shown = false;
    }
    return out;
}

u16 putRectangle(u16 text, int x1, int y1, int x2, int y2, bool inverse)
{
    const u16 tag = u"\\" + u16(inverse ? u"i" : u"") + u"clip(" + number(x1) + u"," + number(y1) + u"," +
                    number(x2) + u"," + number(y2) + u")";
    return replaceTag(std::move(text), u"i?clip(.+)", tag);
}

u16 putVector(u16 text, u16v body, u16 *maskTag)
{
    bool fv = false;
    const auto found = find(text, kVectorClip, &fv);
    u16 tmp = u"clip(";
    if (!found.finding().empty())
        tmp = found.finding();
    if (body.empty() && fv) {
        replaceFound(found, u"", text);
        replaceAll(text, u"{}", u"");
        return text;
    }
    const u16 clipName = beforeFirst(tmp, u'(') + u"(";
    const u16 tclip = u"\\" + clipName + u16(body) + u")";
    replaceFound(found, tclip, text);
    if (maskTag)
        *maskTag = (clipName[0] == u'c') ? u"iclip(" : u"clip(";
    return text;
}

u16 invertedName(u16v text, bool vector)
{
    static const core::LegacyRegex re(u"\\\\(i?clip)\\(([^)]*)\\)", core::LegacyRegex::Advanced);
    u16 clip;
    std::size_t movement = 0;
    while (movement <= text.size()) {
        const u16v clipped = text.substr(movement);
        if (!re.matches(clipped))
            break;
        const auto body = re.match(2);
        if (!body)
            break;
        const u16v clipBody = text.substr(movement + body->first, body->second);
        if (vector ? clipBody.find(u'm') != u16v::npos : count(clipBody, u',') >= 3) {
            const auto name = re.match(1);
            clip = (name && clipped.substr(name->first, name->second).starts_with(u"i")) ? u"clip" : u"iclip";
        }
        movement += body->first + body->second;
    }
    return clip;
}

std::optional<u16> renameClips(u16v source, u16v name, bool vector)
{
    static const core::LegacyRegex re(u"\\\\(i?clip)\\(([^)]*)\\)", core::LegacyRegex::Advanced);
    u16 txt(source);
    bool changed = false;
    std::size_t movement = 0;
    while (movement <= txt.size()) {
        const u16 clipped = txt.substr(movement);
        if (!re.matches(clipped))
            break;
        auto at = re.match(2);
        if (!at)
            break;
        std::size_t start = at->first, len = at->second;
        const u16 clipBody = txt.substr(movement + start, len);
        if (vector ? clipBody.find(u'm') != u16::npos : count(clipBody, u',') >= 3) {
            if (const auto g1 = re.match(1)) {
                start = g1->first;
                len = g1->second;
                txt.replace(movement + start, len, name);
                changed = true;
            }
        }
        movement += start + len;
    }
    if (!changed)
        return std::nullopt;
    return txt;
}

VectorRead readVector(u16v text)
{
    VectorRead out;
    static const core::LegacyRegex drawscale(u"\\\\i?clip\\(([0-9]+)[, ]*m", core::LegacyRegex::Advanced);
    int dscale = 1;
    if (drawscale.matches(text)) {
        const auto m = drawscale.match(1);
        dscale = m ? legacy::atoi(text.substr(m->first, m->second)) : 0;
        out.vectorScale = dscale;
    }
    if (dscale > 1)
        dscale = legacy::toInt(std::pow(2.f, (dscale - 1.f)));
    else
        dscale = 1;
    out.divisor = dscale;
    bool found = false;
    const auto editor = find(text, kVectorClip, &found);
    u16 body = editor.finding();
    const auto mask = [&](const u16 &clip) {
        out.maskTag.clear();
        out.maskBody.clear();
        if (clip.empty())
            return;
        out.maskTag = maskTag(text);
        if (!out.maskTag.empty())
            out.maskBody = clip;
    };
    if (found) {
        const std::size_t rres = count(body, u',');
        if (rres >= 3) {
            body.clear();
            out.vectorScale = 1;
        } else {
            body = afterFirst(body, u'(');
            if (rres >= 1) {
                u16 clip1;
                const u16 vscale = beforeFirst(body, u',', &clip1);
                const int vscaleint = legacy::atoi(vscale);
                if (vscaleint > 0)
                    out.vectorScale = vscaleint;
                mask(body);
                out.body = clip1;
                return out;
            }
        }
    }
    mask(body);
    out.body = body;
    return out;
}

u16 maskText(u16v tag, u16v body, int nx, int ny)
{
    return u"{\\p1\\bord0\\shad0\\fscx100\\fscy100\\frz0\\1c&H000000&\\1a&H77&\\pos(0,0)\\an7\\" + u16(tag) +
           u16(body) + u")}m 0 0 l " + number(nx) + u" 0 " + number(nx) + u" " + number(ny) + u" 0 " + number(ny);
}

}

namespace {

enum { LEFT = 1, RIGHT, TOP = 4, BOTTOM = 8, INSIDE = 16, OUTSIDE = 32 };

constexpr std::uint32_t kMask = 0x88000000;
constexpr std::uint32_t kOutline = 0xFFBB0000;

}

void RectangleClipTool::reset(VisualHost &host)
{
    m_grabbed = -1;
    m_keyCommit = false;
    const auto active = activeRecord(host);
    const clip::RectangleRead read = clip::readRectangle(active ? editedText(host, *active) : u16(),
                                                         host.view().scriptWidth(), host.view().scriptHeight());
    if (read.shown)
        m_showClip = *read.shown;
    if (read.shown == true)
        m_invClip = read.inverse;
    m_corner[0] = {static_cast<float>(read.x1), static_cast<float>(read.y1)};
    m_corner[1] = {static_cast<float>(read.x2), static_cast<float>(read.y2)};
    host.toolChanged();
}

int RectangleClipTool::hitTest(PointF pos, bool diff, const VisualHost &host)
{
    const VideoView &v = host.view();
    const float coeffW = v.coeffW(), coeffH = v.coeffH();
    int resultX = 0, resultY = 0, resultInside = 0, resultFinal = 0, oldpointx = 0, oldpointy = 0;
    for (int i = 0; i < 2; i++) {
        const int pointx = legacy::toInt(((m_corner[i].x / coeffW) - v.zoomMove().x) * v.zoomScale().x);
        const int pointy = legacy::toInt(((m_corner[i].y / coeffH) - v.zoomMove().y) * v.zoomScale().y);
        if (std::fabs(pos.x - pointx) < 5 * unitOf(host)) {
            if (diff)
                m_diffs.x = (pointx)-pos.x;
            resultX |= (i + 1);
        }
        if (std::fabs(pos.y - pointy) < 5 * unitOf(host)) {
            if (diff)
                m_diffs.y = (pointy)-pos.y;
            resultY |= ((i + 1) * 4);
        }
        if (i) {
            resultInside |= (resultX || (oldpointx <= pointx && oldpointx <= pos.x && pointx >= pos.x) ||
                             (oldpointx >= pointx && oldpointx >= pos.x && pointx <= pos.x))
                                ? INSIDE
                                : OUTSIDE;
            // MUST KILL: the y inside test compares oldpointx, not oldpointy
            resultInside |= (resultY || (oldpointx <= pointx && oldpointy <= pos.y && pointy >= pos.y) ||
                             (oldpointx >= pointx && oldpointy >= pos.y && pointy <= pos.y))
                                ? INSIDE
                                : OUTSIDE;
        } else {
            oldpointx = pointx;
            oldpointy = pointy;
        }
    }
    resultFinal = (resultInside & OUTSIDE) ? OUTSIDE : INSIDE;
    if (resultFinal == INSIDE) {
        resultFinal |= resultX;
        resultFinal |= resultY;
        if (resultFinal > INSIDE)
            resultFinal ^= INSIDE;
    }
    return resultFinal;
}

u16 RectangleClipTool::change(u16 text) const
{
    int x1, x2, y1, y2;
    if (m_corner[0].x < m_corner[1].x) {
        x1 = legacy::toInt(m_corner[0].x);
        x2 = legacy::toInt(m_corner[1].x);
    } else {
        x1 = legacy::toInt(m_corner[1].x);
        x2 = legacy::toInt(m_corner[0].x);
    }
    if (m_corner[0].y < m_corner[1].y) {
        y1 = legacy::toInt(m_corner[0].y);
        y2 = legacy::toInt(m_corner[1].y);
    } else {
        y1 = legacy::toInt(m_corner[1].y);
        y2 = legacy::toInt(m_corner[0].y);
    }
    return clip::putRectangle(std::move(text), x1, y1, x2, y2, m_invClip);
}

void RectangleClipTool::apply(VisualHost &host, bool commit)
{
    Gesture *g = ensureGesture(host, Family::RectangleClip, m_began);
    if (!g) {
        if (commit)
            m_began = false;
        return;
    }
    const bool multi = g->targets().size() > 1;
    if (!commit || multi) {
        for (const auto &id : g->targets()) {
            const core::LineRecord &before = g->before(id);
            bool tl = editsTranslation(host, before);
            const u16 original = editedText(host, before);
            // MUST KILL: one target builds on its staged text, several on their originals
            const u16 base = multi ? original : currentText(host, *g, id, &tl);
            stage(*g, id, tl, original, change(base));
        }
    }
    if (commit) {
        m_began = false;
        (void)host.commitGesture();
    }
}

void RectangleClipTool::pointer(const Pointer &event, VisualHost &host)
{
    const VideoView &v = host.view();
    const bool click = event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left;
    const bool holding = event.leftDown;
    int x = event.x, y = event.y;
    const float coeffW = v.coeffW(), coeffH = v.coeffH();
    const PointF zm = v.zoomMove(), zs = v.zoomScale();
    if (event.kind == Pointer::Kind::Press && !host.gesture())
        m_began = false;

    if (event.kind == Pointer::Kind::Release) {
        if (m_showClip) {
            if (m_corner[1].y == m_corner[0].y || m_corner[1].x == m_corner[0].x)
                m_showClip = false;
            if (m_corner[1].y < m_corner[0].y)
                std::swap(m_corner[0].y, m_corner[1].y);
            if (m_corner[1].x < m_corner[0].x)
                std::swap(m_corner[0].x, m_corner[1].x);
        }
        if (m_showClip) {
            apply(host, true);
        } else if (host.gesture() && m_began) {
            // A rectangle without width or height drops the gesture.
            m_began = false;
            host.cancelGesture();
        }
        if (!host.hasArrow())
            host.setPointerShape(PointerShape::Arrow);
    }
    if (!holding && m_showClip) {
        const int test = hitTest({static_cast<float>(x), static_cast<float>(y)}, false, host);
        host.setPointerShape(test < INSIDE ? edgePointerShape(test) : PointerShape::Arrow);
    }
    if (click) {
        m_grabbed = OUTSIDE;
        const int pointx = legacy::toInt(((x / zs.x) + zm.x) * coeffW);
        const int pointy = legacy::toInt(((y / zs.y) + zm.y) * coeffH);
        if (m_showClip) {
            m_grabbed = hitTest({static_cast<float>(x), static_cast<float>(y)}, true, host);
            if (m_grabbed == INSIDE) {
                if (m_corner[0].x <= pointx && m_corner[1].x >= pointx && m_corner[0].y <= pointy &&
                    m_corner[1].y >= pointy) {
                    m_diffs = {static_cast<float>(x), static_cast<float>(y)};
                    m_grabbed = 100;
                }
            }
        }
        if (!m_showClip || m_grabbed == OUTSIDE) {
            m_corner[0].x = m_corner[1].x = static_cast<float>(pointx);
            m_corner[0].y = m_corner[1].y = static_cast<float>(pointy);
            m_grabbed = 1000;
            m_showClip = true;
        }
    } else if (holding && m_grabbed != -1) {
        const IntRect &video = v.videoRect();
        if (m_grabbed < 16) {
            if (m_grabbed & LEFT || m_grabbed & RIGHT) {
                x = std::max(video.left, std::min(x, video.right));
                m_corner[(m_grabbed & RIGHT) ? 1 : 0].x = ((((x + m_diffs.x) / zs.x) + zm.x) * coeffW);
                if (m_grabbed & LEFT && m_corner[0].x > m_corner[1].x)
                    m_corner[0].x = m_corner[1].x;
                if (m_grabbed & RIGHT && m_corner[1].x < m_corner[0].x)
                    m_corner[1].x = m_corner[0].x;
            }
            if (m_grabbed & TOP || m_grabbed & BOTTOM) {
                y = std::max(video.top, std::min(y, video.bottom));
                m_corner[(m_grabbed & BOTTOM) ? 1 : 0].y = ((((y + m_diffs.y) / zs.y) + zm.y) * coeffH);
                if (m_grabbed & TOP && m_corner[0].y > m_corner[1].y)
                    m_corner[0].y = m_corner[1].y;
                if (m_grabbed & BOTTOM && m_corner[1].y < m_corner[0].y)
                    m_corner[1].y = m_corner[0].y;
            }
        } else if (m_grabbed == 100) {
            const float movex = (((x - m_diffs.x) / zs.x) * coeffW);
            const float movey = (((y - m_diffs.y) / zs.y) * coeffH);
            m_corner[0].x += movex;
            m_corner[0].y += movey;
            m_corner[1].x += movex;
            m_corner[1].y += movey;
            m_diffs = {static_cast<float>(x), static_cast<float>(y)};
        } else if (m_grabbed == 1000) {
            const int pointx = legacy::toInt(((x / zs.x) + zm.x) * coeffW);
            const int pointy = legacy::toInt(((y / zs.y) + zm.y) * coeffH);
            m_corner[1].x = static_cast<float>(pointx);
            m_corner[1].y = static_cast<float>(pointy);
        }
        apply(host, false);
    }
    host.toolChanged();
}

bool RectangleClipTool::takesKey(const Key &event) const
{
    return nudgeDirection(event).has_value();
}

bool RectangleClipTool::key(const Key &event, VisualHost &host)
{
    if (event.release) {
        if (!isArrowKey(event.key))
            return false;
        if (!m_keyCommit)
            return false;
        if (event.autoRepeat)
            return true; // Qt sends auto-repeat releases while the key is held
        m_keyCommit = false;
        if (host.gesture() && m_began) {
            m_began = false;
            (void)host.commitGesture();
        }
        host.toolChanged();
        return true;
    }
    const auto direction = nudgeDirection(event);
    if (!direction || !m_showClip)
        return false;
    if (!host.gesture())
        m_began = false;
    float directionX = direction->x;
    float directionY = direction->y;
    if (event.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    m_corner[1].x += directionX;
    m_corner[1].y += directionY;
    apply(host, false);
    m_keyCommit = true;
    host.toolChanged();
    return true;
}

Overlay RectangleClipTool::overlay(const VisualHost &host) const
{
    Overlay out;
    out.unit = unitOf(host);
    if (!m_showClip)
        return out;
    const VideoView &v = host.view();
    const float coeffW = v.coeffW(), coeffH = v.coeffH();
    const PointF zm = v.zoomMove(), zs = v.zoomScale();
    int x1, x2, y1, y2;
    if (m_corner[0].x < m_corner[1].x) {
        x1 = legacy::toInt(m_corner[0].x);
        x2 = legacy::toInt(m_corner[1].x);
    } else {
        x1 = legacy::toInt(m_corner[1].x);
        x2 = legacy::toInt(m_corner[0].x);
    }
    if (m_corner[0].y < m_corner[1].y) {
        y1 = legacy::toInt(m_corner[0].y);
        y2 = legacy::toInt(m_corner[1].y);
    } else {
        y1 = legacy::toInt(m_corner[1].y);
        y2 = legacy::toInt(m_corner[0].y);
    }
    PointF v2[5];
    const float sx = static_cast<float>(v.videoRect().right);
    const float sy = static_cast<float>(v.videoRect().bottom);
    v2[0].x = ((x1 / coeffW) - zm.x) * zs.x;
    v2[0].y = ((y1 / coeffH) - zm.y) * zs.y;
    v2[1].x = v2[0].x;
    v2[1].y = (((y2 / coeffH) - zm.y) * zs.y) - 1;
    v2[2].x = (((x2 / coeffW) - zm.x) * zs.x) - 1;
    v2[2].y = v2[1].y;
    v2[3].x = v2[2].x;
    v2[3].y = v2[0].y;
    v2[4].x = v2[0].x;
    v2[4].y = v2[0].y;
    if (v2[0].x > v2[2].x)
        v2[2].x = v2[3].x = v2[0].x;
    if (v2[0].y > v2[1].y)
        v2[1].y = v2[2].y = v2[0].y;
    OverlayPolygon mask;
    mask.fill = kMask;
    mask.border = 0;
    if (!m_invClip) {
        mask.points = {{0, 0}, {sx, 0}, {v2[2].x, v2[0].y}, {v2[0].x, v2[0].y}, {v2[0].x, v2[2].y}, {0, sy}};
        mask.more.push_back({{sx, sy}, {0, sy}, {v2[0].x, v2[2].y}, {v2[2].x, v2[2].y}, {v2[2].x, v2[0].y}, {sx, 0}});
    } else {
        mask.points = {{v2[0].x, v2[0].y}, {v2[2].x, v2[0].y}, {v2[2].x, v2[2].y}, {v2[0].x, v2[2].y}};
    }
    out.polygons.push_back(std::move(mask));
    for (int i = 0; i < 4; ++i)
        out.lines.push_back({v2[i], v2[i + 1], 1, kOutline});
    return out;
}

std::vector<ToolOption> RectangleClipTool::options(const VisualHost &host) const
{
    (void)host;
    ToolOption invert;
    invert.name = "invert";
    invert.kind = ToolOption::Kind::Action;
    invert.iconRole = "clip-invert";
    invert.tooltip = u"Invert clip";
    return {invert};
}

bool RectangleClipTool::setOption(const std::string &name, int value, VisualHost &host)
{
    (void)value;
    if (name != "invert")
        return false;
    invert(host);
    return true;
}

void RectangleClipTool::invert(VisualHost &host)
{
    commitInvert(host, false);
    reset(host);
}

VectorFrame VectorClipTool::frame(const VisualHost &host) const
{
    const VideoView &v = host.view();
    VectorFrame f;
    f.unit = unitOf(host);
    f.coeffW = v.coeffW();
    f.coeffH = v.coeffH();
    f.coeffW /= m_scale;
    f.coeffH /= m_scale;
    f.zoomMove = v.zoomMove();
    f.zoomScale = v.zoomScale();
    f.videoRect = v.videoRect();
    return f;
}

void VectorClipTool::reset(VisualHost &host)
{
    m_editor.endDrag();
    m_inKey = m_keyCommit = false;
    const auto active = activeRecord(host);
    const clip::VectorRead read = clip::readVector(active ? editedText(host, *active) : u16());
    // MUST KILL: m_vectorScale carries over from the previous Line when a clip has no scale
    if (read.vectorScale)
        m_vectorScale = *read.vectorScale;
    m_scale = 1.f;
    m_scale /= read.divisor;
    m_maskTag = read.maskTag;
    m_maskBody = read.maskBody;
    m_editor.points = parseVectorPoints(read.body);
    m_editor.normaliseFirst();
    host.toolChanged();
}

std::u16string VectorClipTool::body() const
{
    u16 visual;
    if (m_vectorScale > 1)
        visual += number(m_vectorScale) + u",";
    return visual + serializeVectorPoints(m_editor.points, "6.0f");
}

VectorEditor::Callbacks VectorClipTool::callbacks(VisualHost &host)
{
    VectorEditor::Callbacks cb;
    cb.apply = [this, &host](bool commit) { apply(host, commit); };
    cb.bell = [&host] { host.bell(); };
    cb.notice = [&host](std::u16string_view text) { host.notice(text); };
    return cb;
}

void VectorClipTool::apply(VisualHost &host, bool commit)
{
    if (commit && m_inKey) {
        m_keyCommit = true;
        return;
    }
    Gesture *g = ensureGesture(host, Family::VectorClip, m_began);
    if (!g) {
        if (commit)
            m_began = false;
        return;
    }
    const u16 clip = body();
    const bool multi = g->targets().size() > 1;
    if (multi) {
        for (const auto &id : g->targets()) {
            const core::LineRecord &before = g->before(id);
            const bool tl = editsTranslation(host, before);
            const u16 text = editedText(host, before);
            stage(*g, id, tl, text, clip::putVector(text, clip));
        }
        if (!commit) {
            const auto active = activeRecord(host);
            m_maskTag = clip.empty() || !active ? u16() : clip::maskTag(editedText(host, *active));
            m_maskBody = m_maskTag.empty() ? u16() : clip;
        }
    } else {
        const core::LineId id = g->targets().front();
        bool tl = false;
        const u16 current = currentText(host, *g, id, &tl);
        const u16 original = editedText(host, g->before(id));
        // MUST KILL: an emptied clip commits mid-gesture and m_began stays set
        if (clip.empty()) {
            m_maskTag.clear();
            m_maskBody.clear();
            bool found = false;
            (void)find(current, kVectorClip, &found);
            if (found)
                stage(*g, id, tl, original, clip::putVector(current, u""));
            (void)host.commitGesture();
            return;
        } else if (!commit) {
            u16 tag;
            const u16 next = clip::putVector(current, clip, &tag);
            stage(*g, id, tl, original, next);
            m_maskTag = tag;
            m_maskBody = clip;
        }
    }
    if (commit) {
        m_began = false;
        (void)host.commitGesture();
    }
}

void VectorClipTool::pointer(const Pointer &event, VisualHost &host)
{
    if (event.kind == Pointer::Kind::Wheel) {
        // Ctrl+wheel resizes the video window.
        if (event.control || event.wheelSteps == 0)
            return;
        m_editor.wheel(event.wheelSteps);
        host.toolChanged();
        return;
    }
    if (event.kind == Pointer::Kind::Press && !host.gesture())
        m_began = false;
    m_inKey = false;
    m_editor.pointer(event, frame(host), callbacks(host));
    m_editor.normaliseFirst();
    host.toolChanged();
}

bool VectorClipTool::takesKey(const Key &event) const
{
    return nudgeDirection(event).has_value();
}

bool VectorClipTool::key(const Key &event, VisualHost &host)
{
    const bool nudge = isArrowKey(event.key) || event.key == keys::Delete;
    if (event.release) {
        if (!nudge || !m_keyCommit)
            return false;
        if (event.autoRepeat)
            return true; // Qt sends auto-repeat releases while the key is held
        m_keyCommit = false;
        m_inKey = false;
        if (host.gesture() && m_began) {
            m_began = false;
            (void)host.commitGesture();
        }
        host.toolChanged();
        return true;
    }
    if (!host.gesture())
        m_began = false;
    m_inKey = true;
    const bool used = m_editor.key(event, frame(host), callbacks(host));
    m_inKey = false;
    m_editor.normaliseFirst();
    if (used)
        host.toolChanged();
    return used;
}

Overlay VectorClipTool::overlay(const VisualHost &host) const
{
    Overlay out;
    out.unit = unitOf(host);
    m_editor.draw(out, frame(host));
    return out;
}

std::vector<ToolOption> VectorClipTool::options(const VisualHost &host) const
{
    (void)host;
    static const struct {
        const char *icon;
        const char16_t *label;
    } modes[] = {{"vector-drag", u"Move points"},        {"vector-line", u"Add line"},
                 {"vector-bezier", u"Add Bézier curve"}, {"vector-bspline", u"Add B-spline"},
                 {"vector-point", u"Add separate point"}, {"vector-delete", u"Delete point"}};
    std::vector<ToolOption> out;
    for (int i = 0; i < 6; ++i) {
        ToolOption mode;
        mode.name = "mode" + std::to_string(i);
        mode.iconRole = modes[i].icon;
        mode.tooltip = modes[i].label;
        mode.checked = m_editor.mode == i;
        mode.group = u"Points";
        out.push_back(std::move(mode));
    }
    ToolOption invert;
    invert.name = "invert";
    invert.kind = ToolOption::Kind::Action;
    invert.iconRole = "clip-invert";
    invert.tooltip = u"Invert clip";
    invert.checked = m_editor.mode == VectorEditor::InvertSlot;
    out.push_back(std::move(invert));
    return out;
}

bool VectorClipTool::setOption(const std::string &name, int value, VisualHost &host)
{
    (void)value;
    if (name == "invert") {
        invert(host);
        return true;
    }
    if (name.size() == 5 && name.starts_with("mode") && name[4] >= '0' && name[4] <= '5') {
        m_editor.mode = name[4] - '0';
        host.toolChanged();
        return true;
    }
    return false;
}

void VectorClipTool::invert(VisualHost &host)
{
    commitInvert(host, true);
    reset(host);
}

std::u16string VectorClipTool::mask(const VisualHost &host) const
{
    if (m_maskBody.empty())
        return {};
    return clip::maskText(m_maskTag, m_maskBody, host.view().scriptWidth(), host.view().scriptHeight());
}

std::vector<core::LineRecord> VectorClipTool::previewLines(const VisualHost &host) const
{
    const u16 text = mask(host);
    const auto active = activeRecord(host);
    if (text.empty() || !active)
        return {};
    core::LineRecord line = *active;
    line.text = core::toUtf8(text);
    line.translation.clear();
    line.originalSpan.reset();
    line.layer.value = INT_MAX;
    line.layer.lexeme = u8"2147483647";
    line.edited = true;
    return {line};
}

}
