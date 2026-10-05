#include "hikari/application/visual_rotation.h"

#include "hikari/core/text_projection.h"

#include <cmath>

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

using namespace transform;

namespace {

constexpr float kRad = 0.01745329251994329576923690768489f; // VisualRotationZ.cpp:61
constexpr float kDegrees = 180.f / 3.1415926536f;           // VisualRotationZ.cpp:154

std::u8string u8(const std::u16string &text)
{
    return core::toUtf8(text);
}

// The legacy tools' In and Out conversions with the coefficients and zoom of
// their last SizeChanged / SetZoom (Visuals.h:139-154).
PointF inPos(PointF p, float coeffW, float coeffH, PointF zm, PointF zs)
{
    return {((p.x / coeffW) - zm.x) * zs.x, ((p.y / coeffH) - zm.y) * zs.y};
}

PointF outPos(PointF p, float coeffW, float coeffH, PointF zm, PointF zs)
{
    return {((p.x / zs.x) + zm.x) * coeffW, ((p.y / zs.y) + zm.y) * coeffH};
}

// ChangeOrg's offsets: the \org's move with the zoom's offset added
// (VisualRotationZ.cpp:302-303, VisualRotationXY.cpp:268-269).
PointF orgOffset(PointF org, PointF lastOrg, float coeffW, float coeffH, PointF zm, PointF zs)
{
    return {(((org.x - lastOrg.x) / zs.x) + zm.x) * coeffW, (((org.y - lastOrg.y) / zs.y) + zm.y) * coeffH};
}

struct ItemInfo {
    const char *name;
    const char *icon;
    const char16_t *tooltip;
};

// RotationZItem's icons (VideoToolbar.cpp:78-80).
const std::array<ItemInfo, 3> &zItems()
{
    static const std::array<ItemInfo, 3> table{{
        {"twoPoints", "two-points",
         u"Set angle from 2 points.\nAfter setting 2 points below text on video\nit calculates angle from it."},
        {"changeAll", "tool-scale-rotation", u"Changing all Z-rotation tags"},
        {"preserveProportions", "resample",
         u"Preserve proportions so that vector drawings and text\nkeep their original positions after rotation."},
    }};
    return table;
}

} // namespace

// ---------------------------------------------------------------- RotationZ

int RotationZTool::toggled() const
{
    // RotationZItem::GetItemToggled (VideoToolbar.h:183-189).
    int result = 0;
    for (int i = 0; i < 3; i++)
        if (m_toggle[static_cast<std::size_t>(i)])
            result |= (1 << i);
    return result;
}

void RotationZTool::setToggled(int bits, VisualHost *host)
{
    for (int i = 0; i < 3; i++)
        m_toggle[static_cast<std::size_t>(i)] = (bits & (1 << i)) != 0;
    if (host)
        changeTool(toggled(), false, *host);
}

std::vector<ToolOption> RotationZTool::options(const VisualHost &host) const
{
    (void)host;
    // RotationZItem::OnMouseEvent: "change all" stays on with "preserve
    // proportions" (VideoToolbar.cpp:833).
    std::vector<ToolOption> out;
    for (int elem = 0; elem < 3; ++elem) {
        const ItemInfo &info = zItems()[static_cast<std::size_t>(elem)];
        ToolOption o;
        o.name = info.name;
        o.iconRole = info.icon;
        o.tooltip = info.tooltip;
        o.checked = m_toggle[static_cast<std::size_t>(elem)];
        o.enabled = !(elem == 1 && m_toggle[2]);
        out.push_back(std::move(o));
    }
    return out;
}

bool RotationZTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // RotationZItem::OnMouseEvent (VideoToolbar.cpp:827-865).
    int elem = -1;
    for (int i = 0; i < 3; ++i)
        if (name == zItems()[static_cast<std::size_t>(i)].name)
            elem = i;
    if (elem < 0)
        return false;
    const auto e = static_cast<std::size_t>(elem);
    const bool grayed = elem == 1 && m_toggle[2];
    if (grayed || m_toggle[e] == (value != 0))
        return false;
    m_toggle[e] = !m_toggle[e];
    if (elem == 2 && m_toggle[e])
        m_toggle[1] = true;
    changeTool(toggled(), false, host);
    return true;
}

void RotationZTool::selected(VisualHost &host)
{
    (void)host;
    const auto keep = m_toggle;
    *this = RotationZTool();
    m_toggle = keep;
}

void RotationZTool::takeView(const VideoView &view)
{
    m_coeffW = view.coeffW();
    m_coeffH = view.coeffH();
    m_zoomMove = view.zoomMove();
    m_zoomScale = view.zoomScale();
}

void RotationZTool::reset(VisualHost &host)
{
    // Visuals::SetVisual(dial, tool) (Visuals.cpp:256-283).
    takeView(host.view());
    const Context ctx = context(host);
    if (ctx.active) {
        m_start = startMs(*ctx.active);
        m_end = endMs(*ctx.active);
    }
    m_currentLineText = ctx.editorText;
    m_find.setSeveralLines(severalLines(host.batchTargets(), host.activeLine()));
    const auto [selFrom, selTo] = host.editorSelection();
    m_find.setSelection(selFrom, selTo);
    changeTool(toggled(), true, host);
    setCurVisual(host);
    host.toolChanged();
}

void RotationZTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // RotationZ::ChangeTool (VisualRotationZ.cpp:422-441): with "change all"
    // switched in the same call the two-point mode is not taken.
    const bool twoPointsTool = (tool & 1) != 0;
    const bool oldChangeAllTags = m_changeAllTags;
    m_changeAllTags = (tool & 2) != 0;
    m_replaceTagsInCursorPosition = !m_changeAllTags;
    m_preserveProportions = (tool & 4) != 0;
    if (oldChangeAllTags != m_changeAllTags) {
        if (!blockSetCurVisual) {
            setCurVisual(host);
            host.toolChanged();
        }
    } else if (twoPointsTool != m_hasTwoPoints) {
        m_hasTwoPoints = twoPointsTool;
        m_visibility = {false, false};
        if (!blockSetCurVisual)
            host.toolChanged();
    }
}

void RotationZTool::setCurVisual(VisualHost &host)
{
    // RotationZ::SetCurVisual (VisualRotationZ.cpp:256-294).
    const Context ctx = context(host);
    PointF linepos = posnScale(ctx, m_find, !m_replaceTagsInCursorPosition, nullptr, nullptr, m_moveValues);
    if (m_moveValues[6] > 3)
        linepos = calcMovePos(ctx, m_moveValues, m_start, m_end);
    m_from = inPos(linepos, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
    const double lastfrz = m_lastmove.y;
    m_lastmove = {0, 0};
    if (m_find.findTag(u"frz?([0-9.-]+)", m_currentLineText.empty() ? ctx.editorText : m_currentLineText,
                       m_changeAllTags ? 1 : 0)) {
        double result = 0.;
        (void)m_find.getDouble(result);
        m_lastmove.y = static_cast<float>(result);
        m_lastmove.x += m_lastmove.y;
    } else {
        const core::StyleValues style = lineStyle(ctx, ctx.active ? ctx.active->style : std::u8string());
        double result = 0.;
        (void)toDouble(core::toUtf16(style.angle), result);
        m_lastmove.y = static_cast<float>(result);
        m_lastmove.x += m_lastmove.y;
    }
    if (m_find.findTag(u"org\\(([^\\)]+)", m_currentLineText.empty() ? ctx.editorText : m_currentLineText)) {
        double orx = 0, ory = 0;
        if (m_find.getTwoValueDouble(orx, ory))
            m_org = inPos({static_cast<float>(orx), static_cast<float>(ory)}, m_coeffW, m_coeffH, m_zoomMove,
                          m_zoomScale);
        else
            m_org = m_from;
    } else {
        m_org = m_from;
    }
    m_to = m_org;
    if (m_hasTwoPoints && std::fabs(lastfrz - m_lastmove.y) > 0.01)
        m_visibility = {false, false};
}

bool RotationZTool::beginEdit(VisualHost &host)
{
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    auto g = host.beginGesture(targets, std::string(familyInfo(Family::RotationZ).history));
    if (!g)
        return false;
    const bool several = severalLines(targets, host.activeLine());
    m_find.setSeveralLines(several);
    m_editing.reset();
    if (!several) {
        const Context ctx = context(host);
        m_editing = targets.front();
        m_editorText = ctx.editorText;
        m_editorIsTranslation = ctx.editorIsTranslation;
        const auto [selFrom, selTo] = host.editorSelection();
        m_find.setSelection(selFrom, selTo);
    }
    return true;
}

bool RotationZTool::finishEdit(VisualHost &host)
{
    // A refused commit (a stale revision, a draft that cannot commit) wrote
    // nothing: the tool reads the unchanged text again, as after Esc.
    const bool committed = !host.gesture() || host.commitGesture().has_value();
    m_editing.reset();
    if (!committed)
        reset(host);
    return committed;
}

void RotationZTool::setVisual(bool dummy, VisualHost &host)
{
    // Visuals::SetVisual(dummy) (Visuals.cpp:726-832), as ScaleTool's.
    Gesture *g = host.gesture();
    if (!g)
        return;
    if (!m_editing) {
        const Context ctx = context(host);
        for (const auto &id : g->targets()) {
            const core::LineRecord &line = g->before(id);
            std::u16string txt = lineText(line);
            changeVisualLine(txt, line, ctx);
            g->stage(id, u8(txt), editsTranslation(line));
        }
        if (!dummy)
            (void)finishEdit(host);
    } else if (dummy) {
        std::u16string txt = m_replaceTagsInCursorPosition ? m_editorText : m_currentLineText;
        const auto pos = changeVisualEditor(txt);
        m_editorText = txt;
        m_find.setSelection(pos.first, pos.first);
        g->stage(*m_editing, u8(txt), m_editorIsTranslation);
    } else if (const std::u16string written = m_editorText; finishEdit(host)) {
        // The caret goes to the tag only in the text that was written.
        m_currentLineText = written;
        const auto [selFrom, selTo] = m_find.selection();
        host.setEditorSelection(selFrom, selTo);
    }
    host.toolChanged();
}

float RotationZTool::currentAngle() const
{
    // VisualRotationZ.cpp:307-315.
    float angle;
    if (m_hasTwoPoints) {
        angle = std::atan2((m_twoPoints[0].y - m_twoPoints[1].y), (m_twoPoints[0].x - m_twoPoints[1].x)) * kDegrees;
        angle = -angle + 180;
    } else {
        angle = m_lastmove.x - std::atan2((m_org.y - m_to.y), (m_org.x - m_to.x)) * kDegrees;
    }
    return std::fmod(angle + 360.f, 360.f);
}

void RotationZTool::changeVisualLine(std::u16string &text, const core::LineRecord &dial, const Context &ctx)
{
    // RotationZ::ChangeVisual(txt, dial, numOfSelections) (VisualRotationZ.cpp:296-370).
    if (m_hasTwoPoints && (!m_visibility[0] || !m_visibility[1]))
        return;
    if (m_isOrg) {
        const PointF d = orgOffset(m_org, m_lastOrg, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        changeOrg(ctx, m_find, text, dial, d.x, d.y);
        return;
    }
    const float angle = currentAngle();
    if (m_changeAllTags) {
        if (m_preserveProportions) {
            const float posRotationAngle = (m_lastAngle - angle);
            const LinePosition lp = linePosition(ctx, dial);
            PointF pos = lp.pos;
            PointF orgpivot = outPos(m_org, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
            // FindTag without a text reads the editor's (TagFindReplace.cpp:37-38).
            if (m_find.findTag(u"org\\(([^\\)]+)", ctx.editorText)) {
                double orx = 0, ory = 0;
                if (m_find.getTwoValueDouble(orx, ory))
                    orgpivot = {static_cast<float>(orx), static_cast<float>(ory)};
            }
            const float s = std::sin(posRotationAngle * kRad);
            const float c = std::cos(posRotationAngle * kRad);
            pos = rotateZ(pos, s, c, orgpivot);
            std::u16string posstr = u"\\pos(" + getfloat(pos.x) + u"," + getfloat(pos.y) + u")";
            if (m_moveValues[6] > 2 && ctx.active)
                posstr = moveText(pos, m_moveValues, startMs(*ctx.active));
            if (lp.putInBracket)
                posstr = u"{" + posstr + u"}";
            const auto at = std::min<std::size_t>(static_cast<std::size_t>(lp.textX), text.size());
            text.replace(at, static_cast<std::size_t>(lp.textY), posstr);
            changeClipRotationZ(m_find, text, orgpivot, s, c);
        }
        m_find.replaceAll(u"frz([0-9.-]+)", u"frz", text,
                          [&](const FindData &data, std::u16string &result) {
                              float newangle = angle;
                              const float oldangle =
                                  data.finding.empty() ? 0 : static_cast<float>(atof(data.finding));
                              newangle = oldangle - (m_lastAngle - angle);
                              newangle = std::fmod(newangle + 360.f, 360.f);
                              if (m_isfirst) {
                                  m_lastmove.y = newangle;
                                  m_isfirst = false;
                              }
                              result = getfloat(newangle);
                          },
                          true);
    } else {
        m_lastmove.y = angle;
        const std::u16string tag = u"\\frz" + getfloat(angle);
        m_find.findTag(u"frz?([0-9.-]+)", text, 1);
        m_find.replace(tag, text);
    }
}

std::pair<long, long> RotationZTool::changeVisualEditor(std::u16string &text)
{
    // RotationZ::ChangeVisual(txt) (VisualRotationZ.cpp:372-420).
    if (m_hasTwoPoints && (!m_visibility[0] || !m_visibility[1]))
        return {0, 0};
    if (m_isOrg) {
        m_find.findTag(u"org\\(([^\\)]+)\\)", text, 1);
        const PointF o = outPos(m_org, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        m_find.replace(u"\\org(" + getfloat(o.x) + u"," + getfloat(o.y) + u")", text);
        return m_find.positionInText();
    }
    const float angle = currentAngle();
    if (m_changeAllTags) {
        m_find.replaceAll(u"frz([0-9.-]+)", u"frz", text,
                          [&](const FindData &data, std::u16string &result) {
                              float newangle = angle;
                              if (!data.finding.empty()) {
                                  const float oldangle = static_cast<float>(atof(data.finding));
                                  newangle = oldangle - (m_lastAngle - angle);
                                  newangle = std::fmod(newangle + 360.f, 360.f);
                              }
                              if (m_isfirst) {
                                  m_lastmove.y = newangle;
                                  m_isfirst = false;
                              }
                              result = getfloat(newangle);
                          },
                          true);
        m_find.findTag(u"frz?([0-9.-]+)", text);
    } else {
        m_lastmove.y = angle;
        const std::u16string tag = u"\\frz" + getfloat(angle);
        m_find.findTag(u"frz?([0-9.-]+)", text);
        m_find.replace(tag, text);
    }
    return m_find.positionInText();
}

void RotationZTool::pointer(const Pointer &event, VisualHost &host)
{
    // RotationZ::OnMouseEvent (VisualRotationZ.cpp:139-254).
    if (event.kind == Pointer::Kind::Wheel)
        return;
    takeView(host.view());
    const Mouse e = mouse(event);
    const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
    const float pointCatch = 5;
    if (e.click)
        (void)beginEdit(host);

    if (e.buttonUp) {
        setVisual(false, host);
        m_to = m_org;
        if (m_isOrg) {
            m_lastmove.x = std::atan2((m_org.y - y), (m_org.x - x)) * kDegrees;
            m_lastmove.x += m_lastmove.y;
        }
        m_isOrg = false;
        m_grabbed = -1;
        host.toolChanged();
    }
    if (m_hasTwoPoints && e.moving) {
        if (std::fabs(m_twoPoints[0].x - x) < pointCatch && std::fabs(m_twoPoints[0].y - y) < pointCatch) {
            m_hover[0] = true;
            host.toolChanged();
        } else if (std::fabs(m_twoPoints[1].x - x) < pointCatch && std::fabs(m_twoPoints[1].y - y) < pointCatch) {
            m_hover[1] = true;
            host.toolChanged();
        } else if (m_hover[0] || m_hover[1]) {
            m_hover = {false, false};
            host.toolChanged();
        }
    }
    if (e.click) {
        if (m_hasTwoPoints) {
            if (!m_visibility[0]) {
                m_visibility[0] = true;
                m_twoPoints[0] = {x, y};
                host.toolChanged();
                return;
            } else if (std::fabs(m_twoPoints[0].x - x) < pointCatch && std::fabs(m_twoPoints[0].y - y) < pointCatch) {
                m_diffs = {m_twoPoints[0].x - x, m_twoPoints[0].y - y};
                m_grabbed = 0;
            } else if (!m_visibility[1]) {
                m_visibility[1] = true;
                m_twoPoints[1] = {x, y};
            } else if (std::fabs(m_twoPoints[1].x - x) < pointCatch && std::fabs(m_twoPoints[1].y - y) < pointCatch) {
                m_diffs = {m_twoPoints[1].x - x, m_twoPoints[1].y - y};
                m_grabbed = 1;
            } else {
                return;
            }
            if (m_changeAllTags)
                m_lastAngle = m_lastmove.y;
            m_isfirst = true;
            setVisual(true, host);
        } else {
            if (std::fabs(m_org.x - x) < pointCatch && std::fabs(m_org.y - y) < pointCatch) {
                m_isOrg = true;
                m_lastOrg = m_org;
                m_diffs = {m_org.x - x, m_org.y - y};
                return;
            }
            m_lastmove.x = std::atan2((m_org.y - y), (m_org.x - x)) * kDegrees;
            m_lastmove.x += m_lastmove.y;
            if (m_changeAllTags)
                m_lastAngle = m_lastmove.y;
        }
    } else if (e.holding) {
        m_isfirst = true;
        if (m_hasTwoPoints) {
            if (m_grabbed != -1) {
                m_twoPoints[static_cast<std::size_t>(m_grabbed)] = {x + m_diffs.x, y + m_diffs.y};
                setVisual(true, host);
            }
        } else {
            if (m_isOrg) {
                m_org = {x + m_diffs.x, y + m_diffs.y};
                setVisual(true, host);
                return;
            }
            m_to = {x, y};
            setVisual(true, host);
        }
    }
}

bool RotationZTool::hasPending() const
{
    return m_hasTwoPoints && m_visibility[0] && !m_visibility[1];
}

bool RotationZTool::cancelPending(VisualHost &host)
{
    // The card's Esc (T3, #178): the first of the two points, placed and
    // waiting for the second, is dropped. Legacy had no key for it
    // (RotationZ::OnKeyPress is empty, VisualRotationZ.cpp:443-453).
    if (!hasPending())
        return false;
    m_visibility[0] = false;
    m_hover = {false, false};
    host.toolChanged();
    return true;
}

Overlay RotationZTool::overlay(const VisualHost &host) const
{
    // RotationZ::DrawVisual (VisualRotationZ.cpp:34-137).
    Overlay out;
    if (m_hasTwoPoints) {
        if (m_visibility[1]) {
            out.lines.push_back({m_twoPoints[0], m_twoPoints[1], 2.f, kHandleBorder});
            drawRect(out, m_twoPoints[1], m_hover[1], 4.f);
        }
        if (m_visibility[0])
            drawRect(out, m_twoPoints[0], m_hover[0], 4.f);
        return out;
    }
    const int time = static_cast<int>(host.videoTimeMs());
    if (time != m_oldtime && m_moveValues[6] > 3) {
        const bool noOrg = (m_org == m_from);
        const Context ctx = context(host);
        PointF f = calcMovePos(ctx, m_moveValues, m_start, m_end);
        f = inPos(f, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        m_from = f;
        m_to = m_from;
        if (noOrg)
            m_org = m_from;
        else
            m_to = m_org;
    }
    m_oldtime = time;
    const PointF org = m_org, from = m_from;
    const float radius = std::sqrt(std::pow(std::fabs(org.x - from.x), 2.f) + std::pow(std::fabs(org.y - from.y), 2.f)) + 40;
    // The ring: a triangle strip between the circles of radius and radius +
    // 10 (0xAA121150), the two circles in 0xAAFF0000.
    OverlayPolygon ring;
    ring.fill = 0xAA121150;
    ring.border = 0;
    std::vector<PointF> outer, inner;
    for (int j = 0; j < 181; j++) {
        const float a = static_cast<float>(j * 2) * kRad;
        outer.push_back({org.x + ((radius + 10.f) * std::sin(a)), org.y + ((radius + 10.f) * std::cos(a))});
        inner.push_back({org.x + (radius * std::sin(a)), org.y + (radius * std::cos(a))});
    }
    ring.points = outer;
    ring.points.insert(ring.points.end(), inner.rbegin(), inner.rend());
    out.polygons.push_back(std::move(ring));
    for (int j = 0; j < 180; j++) {
        out.lines.push_back({outer[static_cast<std::size_t>(j)], outer[static_cast<std::size_t>(j) + 1], 1.f, 0xAAFF0000});
        out.lines.push_back({inner[static_cast<std::size_t>(j)], inner[static_cast<std::size_t>(j) + 1], 1.f, 0xAAFF0000});
    }
    if (radius) {
        const float xx1 = org.x + ((radius - 40) * std::sin(m_lastmove.y * kRad));
        const float yy1 = org.y + ((radius - 40) * std::cos(m_lastmove.y * kRad));
        const float xx2 = xx1 + (radius * std::sin((m_lastmove.y + 90) * kRad));
        const float yy2 = yy1 + (radius * std::cos((m_lastmove.y + 90) * kRad));
        const float xx3 = xx1 + (radius * std::sin((m_lastmove.y - 90) * kRad));
        const float yy3 = yy1 + (radius * std::cos((m_lastmove.y - 90) * kRad));
        out.lines.push_back({{xx1 - 5.0f, yy1}, {xx1 + 5.0f, yy1}, 10.f, 0xAAFF0000});
        out.lines.push_back({org, {xx1, yy1}, 2.f, kHandleBorder});
        out.lines.push_back({{xx2, yy2}, {xx3, yy3}, 2.f, kHandleBorder});
    }
    out.lines.push_back({{org.x - 10.0f, org.y}, {org.x + 10.0f, org.y}, 2.f, kHandleBorder});
    out.lines.push_back({{org.x, org.y - 10.0f}, {org.x, org.y + 10.0f}, 2.f, kHandleBorder});
    out.lines.push_back({org, m_to, 2.f, kHandleBorder});
    return out;
}

std::vector<ToolValue> RotationZTool::values(const VisualHost &host) const
{
    (void)host;
    const PointF o = outPos(m_org, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
    return {{"frz", u"Z rotation", getfloat(m_lastmove.y), false},
            {"org", u"Origin", getfloat(o.x) + u"," + getfloat(o.y), false}};
}

// --------------------------------------------------------------- RotationXY

void RotationXYTool::setToggled(int toggled, VisualHost *host)
{
    m_toggled = toggled;
    if (host)
        changeTool(m_toggled, false, *host);
}

std::vector<ToolOption> RotationXYTool::options(const VisualHost &host) const
{
    (void)host;
    // RotationXYItem (VideoToolbar.cpp:100, 1197-1262).
    ToolOption o;
    o.name = "changeAll";
    o.iconRole = "tool-scale-rotation";
    o.tooltip = u"Changing all X/Y-rotation tags";
    o.checked = m_toggled == 0;
    return {o};
}

bool RotationXYTool::setOption(const std::string &name, int value, VisualHost &host)
{
    if (name != "changeAll" || (m_toggled == 0) == (value != 0))
        return false;
    m_toggled = (m_toggled == 0) ? -1 : 0;
    changeTool(m_toggled, false, host);
    return true;
}

void RotationXYTool::selected(VisualHost &host)
{
    (void)host;
    const int keep = m_toggled;
    *this = RotationXYTool();
    m_toggled = keep;
}

void RotationXYTool::takeView(const VideoView &view)
{
    m_coeffW = view.coeffW();
    m_coeffH = view.coeffH();
    m_zoomMove = view.zoomMove();
    m_zoomScale = view.zoomScale();
}

void RotationXYTool::reset(VisualHost &host)
{
    takeView(host.view());
    const Context ctx = context(host);
    if (ctx.active) {
        m_start = startMs(*ctx.active);
        m_end = endMs(*ctx.active);
    }
    m_currentLineText = ctx.editorText;
    m_find.setSeveralLines(severalLines(host.batchTargets(), host.activeLine()));
    const auto [selFrom, selTo] = host.editorSelection();
    m_find.setSelection(selFrom, selTo);
    changeTool(m_toggled, true, host);
    setCurVisual(host);
    host.toolChanged();
}

void RotationXYTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // RotationXY::ChangeTool (VisualRotationXY.cpp:379-390).
    const bool oldChangeAllTags = m_changeAllTags;
    m_changeAllTags = tool == 0;
    m_replaceTagsInCursorPosition = !m_changeAllTags;
    if (oldChangeAllTags != m_changeAllTags) {
        if (!blockSetCurVisual)
            setCurVisual(host);
        host.toolChanged();
    }
}

void RotationXYTool::setCurVisual(VisualHost &host)
{
    // RotationXY::SetCurVisual (VisualRotationXY.cpp:232-263).
    const Context ctx = context(host);
    PointF linepos = posnScale(ctx, m_find, !m_replaceTagsInCursorPosition, nullptr, &m_an, m_moveValues);
    if (m_moveValues[6] > 3)
        linepos = calcMovePos(ctx, m_moveValues, m_start, m_end);
    m_from = m_to = inPos(linepos, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
    m_oldAngle = {0, 0};
    const std::u16string &text = m_currentLineText.empty() ? ctx.editorText : m_currentLineText;
    if (m_find.findTag(u"frx([0-9.-]+)", text, m_changeAllTags ? 1 : 0)) {
        double result = 0;
        (void)m_find.getDouble(result);
        m_oldAngle.y = static_cast<float>(result);
    }
    if (m_find.findTag(u"fry([0-9.-]+)", text, m_changeAllTags ? 1 : 0)) {
        double result = 0;
        (void)m_find.getDouble(result);
        m_oldAngle.x = static_cast<float>(result);
    }
    if (m_find.findTag(u"org\\(([^\\)]+)", text)) {
        double orx = 0, ory = 0;
        if (m_find.getTwoValueDouble(orx, ory))
            m_org = inPos({static_cast<float>(orx), static_cast<float>(ory)}, m_coeffW, m_coeffH, m_zoomMove,
                          m_zoomScale);
        else
            m_org = m_from;
    } else {
        m_org = m_from;
    }
    m_firstmove = m_to;
    m_angle = m_oldAngle;
    m_lastmove = m_org;
}

bool RotationXYTool::beginEdit(VisualHost &host)
{
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    auto g = host.beginGesture(targets, std::string(familyInfo(Family::RotationXY).history));
    if (!g)
        return false;
    const bool several = severalLines(targets, host.activeLine());
    m_find.setSeveralLines(several);
    m_editing.reset();
    if (!several) {
        const Context ctx = context(host);
        m_editing = targets.front();
        m_editorText = ctx.editorText;
        m_editorIsTranslation = ctx.editorIsTranslation;
        const auto [selFrom, selTo] = host.editorSelection();
        m_find.setSelection(selFrom, selTo);
    }
    return true;
}

bool RotationXYTool::finishEdit(VisualHost &host)
{
    // A refused commit (a stale revision, a draft that cannot commit) wrote
    // nothing: the tool reads the unchanged text again, as after Esc.
    const bool committed = !host.gesture() || host.commitGesture().has_value();
    m_editing.reset();
    if (!committed)
        reset(host);
    return committed;
}

void RotationXYTool::setVisual(bool dummy, VisualHost &host)
{
    Gesture *g = host.gesture();
    if (!g)
        return;
    if (!m_editing) {
        const Context ctx = context(host);
        for (const auto &id : g->targets()) {
            const core::LineRecord &line = g->before(id);
            std::u16string txt = lineText(line);
            changeVisualLine(txt, line, ctx);
            g->stage(id, u8(txt), editsTranslation(line));
        }
        if (!dummy)
            (void)finishEdit(host);
    } else if (dummy) {
        std::u16string txt = m_replaceTagsInCursorPosition ? m_editorText : m_currentLineText;
        const auto pos = changeVisualEditor(txt);
        m_editorText = txt;
        m_find.setSelection(pos.first, pos.first);
        g->stage(*m_editing, u8(txt), m_editorIsTranslation);
    } else if (const std::u16string written = m_editorText; finishEdit(host)) {
        // The caret goes to the tag only in the text that was written.
        m_currentLineText = written;
        const auto [selFrom, selTo] = m_find.selection();
        host.setEditorSelection(selFrom, selTo);
    }
    host.toolChanged();
}

void RotationXYTool::changeVisualLine(std::u16string &text, const core::LineRecord &dial, const Context &ctx)
{
    // RotationXY::ChangeVisual(txt, dial, numOfSelections) (VisualRotationXY.cpp:265-313).
    if (m_isOrg) {
        const PointF d = orgOffset(m_org, m_lastOrg, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        changeOrg(ctx, m_find, text, dial, d.x, d.y);
        return;
    }
    if (m_type != 1) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fry([0-9.-]+)", u"fry", text,
                              [&](const FindData &data, std::u16string &result) {
                                  const float oldangle =
                                      data.finding.empty() ? m_oldAngle.x : static_cast<float>(atof(data.finding));
                                  m_angle.x = (m_to.x - m_firstmove.x) + oldangle;
                                  m_angle.x = std::fmod(m_angle.x + 360.f, 360.f);
                                  result = getfloat(m_angle.x);
                              },
                              true);
        } else {
            m_angle.x = (m_to.x - m_firstmove.x) + m_oldAngle.x;
            m_angle.x = std::fmod(m_angle.x + 360.f, 360.f);
            const std::u16string tag = u"\\fry" + getfloat(m_angle.x);
            m_find.findTag(u"fry([0-9.-]+)", text, 1);
            m_find.replace(tag, text);
        }
    }
    if (m_type != 0) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"frx([0-9.-]+)", u"frx", text,
                              [&](const FindData &data, std::u16string &result) {
                                  const float oldangle =
                                      data.finding.empty() ? m_oldAngle.y : static_cast<float>(atof(data.finding));
                                  const float angy = (m_to.y - m_firstmove.y) - oldangle;
                                  m_angle.y = std::fmod((-angy) + 360.f, 360.f);
                                  result = getfloat(m_angle.y);
                              },
                              true);
        } else {
            const float angy = (m_to.y - m_firstmove.y) - m_oldAngle.y;
            m_angle.y = std::fmod((-angy) + 360.f, 360.f);
            const std::u16string tag = u"\\frx" + getfloat(m_angle.y);
            m_find.findTag(u"frx([0-9.-]+)", text, 1);
            m_find.replace(tag, text);
        }
    }
}

std::pair<long, long> RotationXYTool::changeVisualEditor(std::u16string &text)
{
    // RotationXY::ChangeVisual(txt) (VisualRotationXY.cpp:315-371).
    if (m_isOrg) {
        m_find.findTag(u"org\\(([^\\)]+)\\)", text, 1);
        const PointF o = outPos(m_org, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        m_find.replace(u"\\org(" + getfloat(o.x) + u"," + getfloat(o.y) + u")", text);
        return m_find.positionInText();
    }
    if (m_type != 1) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fry([0-9.-]+)", u"fry", text,
                              [&](const FindData &data, std::u16string &result) {
                                  const float oldangle =
                                      data.finding.empty() ? m_oldAngle.x : static_cast<float>(atof(data.finding));
                                  m_angle.x = (m_to.x - m_firstmove.x) + oldangle;
                                  m_angle.x = std::fmod(m_angle.x + 360.f, 360.f);
                                  result = getfloat(m_angle.x);
                              },
                              true);
            m_find.findTag(u"frz?([0-9.-]+)", text);
        } else {
            m_angle.x = (m_to.x - m_firstmove.x) + m_oldAngle.x;
            m_angle.x = std::fmod(m_angle.x + 360.f, 360.f);
            const std::u16string tag = u"\\fry" + getfloat(m_angle.x);
            m_find.findTag(u"fry([0-9.-]+)", text);
            m_find.replace(tag, text);
        }
    }
    if (m_type != 0) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"frx([0-9.-]+)", u"frx", text,
                              [&](const FindData &data, std::u16string &result) {
                                  const float oldangle =
                                      data.finding.empty() ? m_oldAngle.y : static_cast<float>(atof(data.finding));
                                  const float angy = (m_to.y - m_firstmove.y) - oldangle;
                                  m_angle.y = std::fmod((-angy) + 360.f, 360.f);
                                  result = getfloat(m_angle.y);
                              },
                              true);
            m_find.findTag(u"frz?([0-9.-]+)", text);
        } else {
            const float angy = (m_to.y - m_firstmove.y) - m_oldAngle.y;
            m_angle.y = std::fmod((-angy) + 360.f, 360.f);
            const std::u16string tag = u"\\frx" + getfloat(m_angle.y);
            m_find.findTag(u"frx([0-9.-]+)", text);
            m_find.replace(tag, text);
        }
    }
    return m_find.positionInText();
}

void RotationXYTool::pointer(const Pointer &event, VisualHost &host)
{
    // RotationXY::OnMouseEvent (VisualRotationXY.cpp:183-230).
    if (event.kind == Pointer::Kind::Wheel)
        return;
    takeView(host.view());
    const Mouse e = mouse(event);
    const int x = e.x, y = e.y;
    if (e.click)
        (void)beginEdit(host);
    if (e.buttonUp) {
        setVisual(false, host);
        m_oldAngle = m_angle;
        m_isOrg = false;
        host.toolChanged();
    }
    if (e.click) {
        if (e.leftc)
            m_type = 0; // fry
        if (e.rightc)
            m_type = 1; // frx
        if (e.middlec)
            m_type = 2; // frx + fry
        if (std::abs(m_org.x - x) < 8 && std::abs(m_org.y - y) < 8) {
            m_isOrg = true;
            m_lastOrg = m_org;
            m_diffsX = static_cast<int>(m_org.x - x);
            m_diffsY = static_cast<int>(m_org.y - y);
        }
        m_firstmove = {static_cast<float>(x), static_cast<float>(y)};
    } else if (e.holding) {
        if (m_isOrg) {
            m_org = {static_cast<float>(x + m_diffsX), static_cast<float>(y + m_diffsY)};
            setVisual(true, host);
            return;
        }
        m_to = {static_cast<float>(x), static_cast<float>(y)};
        setVisual(true, host);
    }
}

Overlay RotationXYTool::overlay(const VisualHost &host) const
{
    // RotationXY::DrawVisual (VisualRotationXY.cpp:37-181).
    Overlay out;
    const VideoView &view = host.view();
    const int time = static_cast<int>(host.videoTimeMs());
    if (time != m_oldtime && m_moveValues[6] > 3) {
        const bool noOrg = (m_org == m_from);
        const Context ctx = context(host);
        PointF f = calcMovePos(ctx, m_moveValues, m_start, m_end);
        f = inPos(f, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
        m_from = f;
        m_to = m_from;
        if (noOrg)
            m_org = m_from;
        else
            m_to = m_org;
    }
    m_oldtime = time;
    // GetWindowSize(withTabPanel = false): the client without the panel.
    XYProjection p;
    p.width = static_cast<float>(view.clientWidth());
    p.height = static_cast<float>(view.clientHeight() - view.panelHeight());
    p.org = m_org;
    p.from = m_from;
    p.angle = m_angle;
    if (p.width > 0 && p.height > 0)
        drawXYGrid(out, p, m_an);
    const PointF org = m_org;
    out.lines.push_back({{org.x - 10.0f, org.y}, {org.x + 10.0f, org.y}, 2.f, kHandleBorder});
    out.lines.push_back({{org.x, org.y - 10.0f}, {org.x, org.y + 10.0f}, 2.f, kHandleBorder});
    return out;
}

std::vector<ToolValue> RotationXYTool::values(const VisualHost &host) const
{
    (void)host;
    const PointF o = outPos(m_org, m_coeffW, m_coeffH, m_zoomMove, m_zoomScale);
    return {{"frx", u"X rotation", getfloat(m_angle.y), false},
            {"fry", u"Y rotation", getfloat(m_angle.x), false},
            {"org", u"Origin", getfloat(o.x) + u"," + getfloat(o.y), false}};
}

// --------------------------------------------------------- the 3D overlay

namespace {

// D3DX's row-vector 4x4 matrices.
struct Mat {
    float m[4][4]{};
    static Mat identity()
    {
        Mat r;
        for (int i = 0; i < 4; ++i)
            r.m[i][i] = 1.f;
        return r;
    }
};

Mat mul(const Mat &a, const Mat &b)
{
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

// D3DXMatrixRotationYawPitchRoll: roll about z, then pitch about x, then yaw
// about y (left-handed, row vectors).
Mat yawPitchRoll(float yaw, float pitch, float roll)
{
    Mat rz = Mat::identity(), rx = Mat::identity(), ry = Mat::identity();
    rz.m[0][0] = std::cos(roll);
    rz.m[0][1] = std::sin(roll);
    rz.m[1][0] = -std::sin(roll);
    rz.m[1][1] = std::cos(roll);
    rx.m[1][1] = std::cos(pitch);
    rx.m[1][2] = std::sin(pitch);
    rx.m[2][1] = -std::sin(pitch);
    rx.m[2][2] = std::cos(pitch);
    ry.m[0][0] = std::cos(yaw);
    ry.m[0][2] = -std::sin(yaw);
    ry.m[2][0] = std::sin(yaw);
    ry.m[2][2] = std::cos(yaw);
    return mul(mul(rz, rx), ry);
}

Mat translation(float x, float y, float z)
{
    Mat r = Mat::identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

struct Clip {
    float x, y, z, w;
};

// The world, view (D3DXMatrixLookAtLH from (0, 0, -17.2) at the origin, y up)
// and projection (D3DXMatrixPerspectiveFovLH, 120 degrees, the window's
// ratio, near 1, far 10000, then translated by the \org in clip space).
struct Pipeline {
    Mat world, projection;
    float width = 0, height = 0;
    explicit Pipeline(const XYProjection &p)
    {
        width = p.width;
        height = p.height;
        const float ratio = p.width / p.height;
        const float xxx = ((p.org.x / p.width) * 2) - 1;
        const float yyy = ((p.org.y / p.height) * 2) - 1;
        Mat rotate = yawPitchRoll(-p.angle.x * 0.017453292519943295769f, p.angle.y * 0.017453292519943295769f, 0);
        if (!(p.from == p.org)) {
            const float txx = ((p.from.x / p.width) * 60) - 30;
            const float tyy = ((p.from.y / p.height) * 60) - 30;
            rotate = mul(translation(txx - (xxx * 30), -(tyy - (yyy * 30)), 0.0f), rotate);
        }
        const Mat view = translation(0, 0, 17.2f);
        world = mul(rotate, view);
        const float zn = 1.0f, zf = 10000.0f;
        const float yScale = 1.f / std::tan(120.f * 0.017453292519943295769f / 2.f);
        const float xScale = yScale / ratio;
        Mat proj;
        proj.m[0][0] = xScale;
        proj.m[1][1] = yScale;
        proj.m[2][2] = zf / (zf - zn);
        proj.m[2][3] = 1.f;
        proj.m[3][2] = -zn * zf / (zf - zn);
        projection = mul(proj, translation(xxx, -yyy, 0.0f));
    }
    Clip clip(float x, float y, float z) const
    {
        const float v[4] = {x, y, z, 1.f};
        float wv[4] = {0, 0, 0, 0};
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                wv[j] += v[k] * world.m[k][j];
        float c[4] = {0, 0, 0, 0};
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                c[j] += wv[k] * projection.m[k][j];
        return {c[0], c[1], c[2], c[3]};
    }
    // The viewport (0, 0, width, height) (RendererVideo::SetProjection).
    PointF screen(const Clip &c) const
    {
        return {(c.x / c.w + 1.f) * width / 2.f, (1.f - c.y / c.w) * height / 2.f};
    }
};

std::uint32_t argb(int a, int r, int g, int b)
{
    return (static_cast<std::uint32_t>(a & 0xFF) << 24) | (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) | static_cast<std::uint32_t>(b & 0xFF);
}

// A line clipped at the near plane (z >= 0 in clip space).
void line3(Overlay &out, const Pipeline &p, const float *a, const float *b, std::uint32_t colour)
{
    Clip ca = p.clip(a[0], a[1], a[2]), cb = p.clip(b[0], b[1], b[2]);
    if (ca.z < 0 && cb.z < 0)
        return;
    if (ca.z < 0 || cb.z < 0) {
        const float t = ca.z / (ca.z - cb.z);
        const Clip m{ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, 0.f, ca.w + (cb.w - ca.w) * t};
        if (ca.z < 0)
            ca = m;
        else
            cb = m;
    }
    if (ca.w <= 0 || cb.w <= 0)
        return;
    out.lines.push_back({p.screen(ca), p.screen(cb), 1.f, colour});
}

void fan(Overlay &out, const Pipeline &p, const std::vector<std::array<float, 3>> &v, std::uint32_t colour)
{
    // D3DPT_TRIANGLEFAN: the first vertex with each next pair.
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
        const Clip c0 = p.clip(v[0][0], v[0][1], v[0][2]);
        const Clip c1 = p.clip(v[i][0], v[i][1], v[i][2]);
        const Clip c2 = p.clip(v[i + 1][0], v[i + 1][1], v[i + 1][2]);
        if (c0.z < 0 || c1.z < 0 || c2.z < 0 || c0.w <= 0 || c1.w <= 0 || c2.w <= 0)
            continue;
        out.polygons.push_back({{p.screen(c0), p.screen(c1), p.screen(c2)}, colour, 0});
    }
}

} // namespace

std::optional<PointF> projectXY(const XYProjection &p, float x, float y, float z)
{
    const Pipeline pipe(p);
    const Clip c = pipe.clip(x, y, z);
    if (c.z < 0 || c.w <= 0)
        return std::nullopt;
    return pipe.screen(c);
}

void drawXYGrid(Overlay &out, const XYProjection &p, int an)
{
    const Pipeline pipe(p);
    // The grid (VisualRotationXY.cpp:94-125): eleven lines each way, 5 units
    // apart, more opaque towards the middle, the middle ones orange.
    const float mm = 60.0f / 12.0f;
    float j = 30 - mm;
    const float gg = 1.0f / 12.f;
    float g = gg;
    bool ster = true;
    for (int i = 0; i < 44; i += 4) {
        int re = 122, gr = 57;
        const int bl = 36;
        if (i == 20) {
            re = 255;
            gr = 155;
        }
        const std::uint32_t colour = argb(static_cast<int>(g * 155), re, gr, bl);
        const float v0[3] = {j, -30.f, 0}, v1[3] = {j, 0.f, 0}, v3[3] = {j, 30.f, 0};
        const float h0[3] = {-30.f, j, 0}, h1[3] = {0.f, j, 0}, h3[3] = {30.f, j, 0};
        line3(out, pipe, v0, v1, colour);
        line3(out, pipe, v1, v3, colour);
        line3(out, pipe, h0, h1, colour);
        line3(out, pipe, h1, h3, colour);
        j -= mm;
        if (g == 1.f)
            ster = false;
        if (ster)
            g += gg;
        else
            g -= gg;
    }
    // The axes and their arrows (VisualRotationXY.cpp:126-156).
    const float addy = (an < 4) ? 9.f : -9.f, addx = (an % 3 == 0) ? -9.f : 9.f;
    const float add1y = (an < 4) ? 10.f : -10.f, add1x = (an % 3 == 0) ? -10.f : 10.f;
    const std::uint32_t colour = kHandleBorder;
    const float ly[3] = {0.f, addy, 0.f}, o[3] = {0.f, 0.f, 0.f}, lx[3] = {addx, 0.f, 0.f}, lz[3] = {0.f, 0.f, 9.f};
    line3(out, pipe, ly, o, colour);
    line3(out, pipe, o, lx, colour);
    line3(out, pipe, o, lz, colour);
    fan(out, pipe,
        {{0.f, add1y, 0.f}, {0.f, addy, -0.6f}, {-0.6f, addy, 0.f}, {0.f, addy, 0.6f}, {0.6f, addy, 0.f}, {0.f, addy, -0.6f}},
        colour);
    fan(out, pipe,
        {{add1x, 0.f, 0.f}, {addx, 0.f, -0.6f}, {addx, -0.6f, 0.f}, {addx, 0.f, 0.6f}, {addx, 0.6f, 0.f}, {addx, 0.f, -0.6f}},
        colour);
    fan(out, pipe,
        {{0.f, 0.f, 10.f}, {-0.6f, 0.f, 9.f}, {0.f, 0.6f, 9.f}, {0.6f, 0.f, 9.f}, {0.f, -0.6f, 9.f}, {-0.6f, 0.f, 9.f}},
        colour);
}

} // namespace hikari::application::visual
