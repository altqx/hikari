#include "hikari/application/visual_clip.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <climits>
#include <cmath>

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

// The pattern legacy's vector clip reads and writes (VisualClips.cpp:188, 552).
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

// wxString::BeforeFirst / AfterFirst.
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

// TagFindReplace::Replace (TagFindReplace.cpp:423-442).
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

// The active Line as the Line editor shows it (the pending draft applied).
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

// The text a visual tool edits: the translation in TLMode unless it is
// empty (legacy's TextEdit / TextEditOrig and Dialogue::GetTextNoCopy).
bool editsTranslation(const VisualHost &host, const core::LineRecord &line)
{
    return host.session() && translationMode(*host.session()) && !line.translation.empty();
}

u16 editedText(const VisualHost &host, const core::LineRecord &line)
{
    return core::toUtf16(editsTranslation(host, line) ? line.translation : line.text);
}

// The open gesture, or a new one on the batch picker's targets. A press
// that began a gesture which is no longer open was cancelled (Esc): nothing
// more is staged until the next press.
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

// Stages a target's new text; one equal to the text before is not staged
// (a gesture without changes records nothing).
void stage(Gesture &g, core::LineId id, bool tl, const u16 &before, const u16 &after)
{
    if (after != before || g.staged(id, tl))
        g.stage(id, core::toUtf8(after), tl);
}

// The current text of a target in a gesture: what it staged, else the text
// as the gesture began (legacy's single-Line path edits the editor's text,
// which its previews changed).
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
    // InvertClip: every target's clips get the opposite of the active Line's
    // last one, as one step. Legacy recorded both tools' inversions as
    // VISUAL_VECTOR_CLIP (VisualClipRect.cpp:450), so the rectangle's is
    // named "Visual vector clipping tool" too.
    const auto active = activeRecord(host);
    if (!active || host.gesture())
        return;
    const u16 name = clip::invertedName(editedText(host, *active), vector);
    if (name.empty())
        return;
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

} // namespace

namespace clip {

u16 maskTag(u16v text)
{
    // CreateClipMask: the opposite of the Line's first clip found from the
    // start (FindTag "(i?clip.)[^)]*\\)", mode 1), rectangular ones included.
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
    // ClipRect::SetCurVisual. Legacy read from the Line editor's caret
    // (FindTag mode 0); the tools read from the start (clipReadFromStart).
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
    // Visuals::GetPosnScale for VECTORCLIP (Visuals.cpp:688-708).
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
    // DrawingAndClip::SetCurVisual (VisualClips.cpp:187-220).
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
    // Legacy ended the text with "\r\n" before writing the Line; the Line
    // here is a record, so the break is the record's own.
    return u"{\\p1\\bord0\\shad0\\fscx100\\fscy100\\frz0\\1c&H000000&\\1a&H77&\\pos(0,0)\\an7\\" + u16(tag) +
           u16(body) + u")}m 0 0 l " + number(nx) + u" 0 " + number(nx) + u" " + number(ny) + u" 0 " + number(ny);
}

} // namespace clip

// ---------------------------------------------------------------------------
// The rectangle clip (legacy ClipRect).

namespace {

enum { LEFT = 1, RIGHT, TOP = 4, BOTTOM = 8, INSIDE = 16, OUTSIDE = 32 };

constexpr std::uint32_t kMask = 0x88000000;
constexpr std::uint32_t kOutline = 0xFFBB0000;

} // namespace

void RectangleClipTool::reset(VisualHost &host)
{
    // A reset during a drag (Esc, a resize) ends it: the next press starts
    // anew. (Legacy kept `grabbed`, which only a drag entering the video
    // with the button held could reuse; Qt's pointer grab never gives one.)
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
    // ClipRect::HitTest (VisualClipRect.cpp:254-294), its x/y mix-up kept.
    const VideoView &v = host.view();
    const float coeffW = v.coeffW(), coeffH = v.coeffH();
    int resultX = 0, resultY = 0, resultInside = 0, resultFinal = 0, oldpointx = 0, oldpointy = 0;
    for (int i = 0; i < 2; i++) {
        const int pointx = legacy::toInt(((m_corner[i].x / coeffW) - v.zoomMove().x) * v.zoomScale().x);
        const int pointy = legacy::toInt(((m_corner[i].y / coeffH) - v.zoomMove().y) * v.zoomScale().y);
        if (std::fabs(pos.x - pointx) < 5) {
            if (diff)
                m_diffs.x = (pointx)-pos.x;
            resultX |= (i + 1);
        }
        if (std::fabs(pos.y - pointy) < 5) {
            if (diff)
                m_diffs.y = (pointy)-pos.y;
            resultY |= ((i + 1) * 4);
        }
        if (i) {
            resultInside |= (resultX || (oldpointx <= pointx && oldpointx <= pos.x && pointx >= pos.x) ||
                             (oldpointx >= pointx && oldpointx >= pos.x && pointx <= pos.x))
                                ? INSIDE
                                : OUTSIDE;
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
    // ClipRect::ChangeVisual: the corners truncated and ordered.
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
    // Visuals::SetVisual(dummy) (Visuals.cpp:722-810). With several targets
    // (legacy: several Lines selected) every target is rewritten from its
    // text, on each sample and again on release; with one, each sample
    // rewrites the text the previous one left (legacy's editor) and the
    // release keeps it.
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
    // ClipRect::OnMouseEvent (VisualClipRect.cpp:120-224).
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
            // A rectangle without width or height: legacy wrote nothing on
            // release (with several Lines its previews were only rendered;
            // with one they stayed in the Line editor unsent). The gesture is
            // dropped (proposed T4-zero-rect-preview).
            m_began = false;
            host.cancelGesture();
        }
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
                x = std::max(video.left, std::min(x, video.right)); // MID(VideoSize.x, x, VideoSize.width)
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

bool RectangleClipTool::key(const Key &event, VisualHost &host)
{
    // ClipRect::OnKeyPress (VisualClipRect.cpp:347-379): A/D/W/S move the
    // second corner a pixel (Shift: a tenth), unless Alt alone is held.
    // The nudge commits on the key's release (the accepted transaction rule).
    const bool left = event.key == keys::A;
    const bool right = event.key == keys::D;
    const bool up = event.key == keys::W;
    const bool down = event.key == keys::S;
    if (!(left || right || up || down))
        return false;
    if (event.release) {
        if (!m_keyCommit)
            return false;
        if (event.autoRepeat)
            return true; // Qt's auto-repeat releases: the key is still held
        m_keyCommit = false;
        if (host.gesture() && m_began) {
            m_began = false;
            (void)host.commitGesture();
        }
        host.toolChanged();
        return true;
    }
    const bool altOnly = event.alt && !event.control && !event.shift;
    if (altOnly || !m_showClip)
        return false;
    if (!host.gesture())
        m_began = false;
    float directionX = left ? -1 : right ? 1 : 0;
    float directionY = up ? -1 : down ? 1 : 0;
    if (event.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    m_corner[1].x += directionX;
    m_corner[1].y += directionY;
    apply(host, false);
    m_keyCommit = true; // SetVisual(false) on the release
    host.toolChanged();
    return true;
}

Overlay RectangleClipTool::overlay(const VisualHost &host) const
{
    // ClipRect::DrawVisual (VisualClipRect.cpp:45-118): the darkened outside
    // (inside for \iclip) and the one-pixel outline.
    Overlay out;
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
    // s: VideoSize.GetSize(), the video rectangle's right and bottom edges.
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
        // The two triangle fans around the rectangle, filled as one path.
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
    // ClipRectangleItem: the one button (VideoToolbar.cpp:67, icon 18).
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
    // ClipRect::ChangeTool(_, false) -> InvertClip; the edit's
    // SetModified then reset the tool (ShowEditOnVideo -> SetVisual).
    commitInvert(host, false);
    reset(host);
}

// ---------------------------------------------------------------------------
// The vector clip (legacy DrawingAndClip as VECTORCLIP).

VectorFrame VectorClipTool::frame(const VisualHost &host) const
{
    const VideoView &v = host.view();
    VectorFrame f;
    f.coeffW = v.coeffW();
    f.coeffH = v.coeffH();
    f.coeffW /= m_scale; // SetCurVisual: coeffW /= scale.x
    f.coeffH /= m_scale;
    f.zoomMove = v.zoomMove();
    f.zoomScale = v.zoomScale();
    f.videoRect = v.videoRect();
    return f;
}

void VectorClipTool::reset(VisualHost &host)
{
    // Visuals::SetVisual -> ChangeTool(tool, true) -> SetCurVisual. Legacy's
    // ChangeTool inverted the clip again on every reset while the wheel had
    // left the mode on the Invert clip button (6); the inversion is the
    // button's only (T4-wheel-invert-slot, proposed).
    m_editor.endDrag(); // as RectangleClipTool::reset
    m_inKey = m_keyCommit = false;
    const auto active = activeRecord(host);
    const clip::VectorRead read = clip::readVector(active ? editedText(host, *active) : u16());
    // A clip without its own scale keeps the previous one (legacy's member).
    if (read.vectorScale)
        m_vectorScale = *read.vectorScale;
    m_scale = 1.f;
    m_scale /= read.divisor;
    m_maskTag = read.maskTag;
    m_maskBody = read.maskBody;
    m_editor.points = parseVectorPoints(read.body);
    // Legacy rendered right after SetVisual, and DrawVisual makes the first
    // point an "m".
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
    // DrawingAndClip::SetClip(!commit) (VisualClips.cpp:365-543).
    if (commit && m_inKey) {
        m_keyCommit = true; // the nudge or Delete commits on its key's release
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
        // Several Lines: each one's clip gets the body, on every sample and
        // again on release.
        for (const auto &id : g->targets()) {
            const core::LineRecord &before = g->before(id);
            const bool tl = editsTranslation(host, before);
            const u16 text = editedText(host, before);
            stage(*g, id, tl, text, clip::putVector(text, clip));
        }
        if (!commit) {
            // CreateClipMask(clip): the tag from the active Line's text.
            const auto active = activeRecord(host);
            m_maskTag = clip.empty() || !active ? u16() : clip::maskTag(editedText(host, *active));
            m_maskBody = m_maskTag.empty() ? u16() : clip;
        }
    } else {
        const core::LineId id = g->targets().front();
        bool tl = false;
        const u16 current = currentText(host, *g, id, &tl);
        const u16 original = editedText(host, g->before(id));
        if (clip.empty()) {
            // All points gone: the clip leaves the text at once, as legacy's
            // empty-clip branch sent it then (VisualClips.cpp:444-455,
            // edit->Send) whether or not the edit was a preview; without one,
            // nothing changes. The commit cannot wait for the release: in the
            // add modes the hover block returns before it once no point is
            // left (VisualClips.cpp:906-907). m_began stays set, so a release
            // that does arrive opens no new gesture.
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
        // Ctrl+wheel resizes the video window (VideoBox::OnMouseEvent) and
        // never reaches the tool.
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

bool VectorClipTool::key(const Key &event, VisualHost &host)
{
    const bool nudge = event.key == keys::W || event.key == keys::S || event.key == keys::A ||
                       event.key == keys::D || event.key == keys::Delete;
    if (event.release) {
        if (!nudge || !m_keyCommit)
            return false;
        if (event.autoRepeat)
            return true; // Qt's auto-repeat releases: the key is still held
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
    m_editor.draw(out, frame(host));
    return out;
}

std::vector<ToolOption> VectorClipTool::options(const VisualHost &host) const
{
    // VectorItem for the clip (VideoToolbar.cpp:61-67): the six modes and
    // the Invert clip button, shown checked when the wheel left the mode on it.
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
    // A mode's button selects it whatever its state (VectorItem: toggled = elem).
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
    // The Invert clip button: ChangeTool(6) -> InvertClip, and the edit's
    // SetModified resets the tool.
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
    // Visuals::AppendClipMask: a copy of the active Line with the mask's
    // text on the highest layer (MAXINT), after every other Line.
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

} // namespace hikari::application::visual
