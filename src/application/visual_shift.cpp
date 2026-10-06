#include "hikari/application/visual_shift.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_script.h"
#include "hikari/application/visual_vector.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

using namespace transform;

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

u16 u16of(const std::u8string &text)
{
    return core::toUtf16(text);
}

std::u8string u8(const u16 &text)
{
    return core::toUtf8(text);
}

u16v mid(u16v text, std::size_t from)
{
    return from >= text.size() ? u16v() : text.substr(from);
}

// wxString::Trim(true).Trim(false): blanks off both ends.
bool isBlank(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}
u16 trim(u16v text)
{
    std::size_t a = 0, b = text.size();
    while (a < b && isBlank(text[a]))
        ++a;
    while (b > a && isBlank(text[b - 1]))
        --b;
    return u16(text.substr(a, b - a));
}

// wxStringTokenizer(text, delimiter, wxTOKEN_STRTOK).
std::vector<u16> strtok(u16v text, char16_t delimiter)
{
    std::vector<u16> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == delimiter)
            ++i;
        if (i >= text.size())
            break;
        const std::size_t start = i;
        while (i < text.size() && text[i] != delimiter)
            ++i;
        out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

// wxString::BeforeFirst(ch, &rest).
u16 beforeFirst(u16v text, char16_t ch, u16 *rest)
{
    const auto at = text.find(ch);
    if (at == u16v::npos) {
        if (rest)
            rest->clear();
        return u16(text);
    }
    if (rest)
        *rest = u16(text.substr(at + 1));
    return u16(text.substr(0, at));
}

// wxString::AfterFirst(ch).
u16 afterFirst(u16v text, char16_t ch)
{
    const auto at = text.find(ch);
    return at == u16v::npos ? u16() : u16(text.substr(at + 1));
}

std::optional<u16> group(const core::LegacyRegex &re, u16v text, std::size_t index)
{
    const auto m = re.match(index);
    if (!m || m->first == std::u16string::npos)
        return std::nullopt;
    return u16(text.substr(m->first, m->second));
}

// The toolbar item's icons in order (VideoToolbar.cpp:70-75).
struct ItemInfo {
    const char *name;
    const char *icon;
    const char16_t *tooltip;
};
const std::array<ItemInfo, 6> &items()
{
    static const std::array<ItemInfo, 6> table{{
        {"positions", "shift-position", u"Move position points"},
        {"moveStarts", "shift-move-start", u"Change \\move starting points"},
        {"moveEnds", "shift-move-end", u"Change \\move ending points"},
        {"clips", "shift-clips", u"Move clips"},
        {"drawings", "shift-drawings",
         u"Move drawings;\nuse only when you want\nto move drawing points.\nDo not combine with the first three options"},
        {"origins", "shift-origins", u"Move \\org points"},
    }};
    return table;
}

// Visuals::CalcDrawingSize (Visuals.cpp:1206-1243).
PointF calcDrawingSize(int alignment, const std::vector<ClipPoint> &points)
{
    if (alignment == 7 || points.empty())
        return {0, 0};
    float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
    for (const ClipPoint &p : points) {
        if (p.x < minx)
            minx = p.x;
        if (p.y < miny)
            miny = p.y;
        if (p.x > maxx)
            maxx = p.x;
        if (p.y > maxy)
            maxy = p.y;
    }
    const PointF sizes{(maxx - minx), (maxy - miny)};
    PointF result{0, 0};
    if (alignment % 3 == 2)
        result.x = static_cast<float>(sizes.x / 2.0);
    else if (alignment % 3 == 0)
        result.x = sizes.x;
    if (alignment < 4)
        result.y = sizes.y;
    else if (alignment < 7)
        result.y = static_cast<float>(sizes.y / 2.0);
    return result;
}

// Visuals::RotateDrawing (Visuals.cpp:1197-1204).
void rotateDrawing(ClipPoint &point, float sinOfAngle, float cosOfAngle, PointF orgpivot)
{
    const float x = point.x + orgpivot.x;
    const float y = point.y + orgpivot.y;
    point.x = (x * cosOfAngle) - (y * sinOfAngle) - orgpivot.x;
    point.y = (x * sinOfAngle) + (y * cosOfAngle) - orgpivot.y;
}

} // namespace

int PositionShifterTool::toggled() const
{
    // MoveAllItem::GetItemToggled (VideoToolbar.h:84-90).
    int result = 0;
    for (int i = 0; i < 6; i++)
        if (m_toggled[static_cast<std::size_t>(i)])
            result |= 1 << i;
    return result;
}

void PositionShifterTool::setToggled(int bits, VisualHost *host)
{
    for (int i = 0; i < 6; i++)
        m_toggled[static_cast<std::size_t>(i)] = (bits & (1 << i)) != 0;
    if (host)
        changeTool(toggled(), *host);
}

std::vector<ToolOption> PositionShifterTool::options(const VisualHost &host) const
{
    // MoveAllItem (VideoToolbar.cpp:354-414): six toggles, none greyed; the
    // drawings stay pushed while the tool leaves them out (ChangeTool).
    (void)host;
    std::vector<ToolOption> out;
    for (std::size_t i = 0; i < 6; ++i) {
        ToolOption o;
        o.name = items()[i].name;
        o.iconRole = items()[i].icon;
        o.tooltip = items()[i].tooltip;
        o.checked = m_toggled[i];
        out.push_back(std::move(o));
    }
    return out;
}

bool PositionShifterTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // MoveAllItem::OnMouseEvent: a click toggles the icon, then the tool
    // takes GetItemToggled (ID_MOVE_TOOLBAR_EVENT, VideoBox.cpp:182-184).
    for (std::size_t i = 0; i < 6; ++i) {
        if (name != items()[i].name)
            continue;
        if (m_toggled[i] == (value != 0))
            return false;
        m_toggled[i] = !m_toggled[i];
        changeTool(toggled(), host);
        return true;
    }
    return false;
}

void PositionShifterTool::selected(VisualHost &host)
{
    // Visuals::Get made a new MoveAll (VisualMoveAll.cpp:38-43): its state
    // starts over (selectedTags 1), the toolbar's toggles stay.
    (void)host;
    const auto keep = m_toggled;
    *this = PositionShifterTool();
    m_toggled = keep;
}

void PositionShifterTool::takeView(const VideoView &view)
{
    m_coeffW = view.coeffW();
    m_coeffH = view.coeffH();
    m_zoomMove = view.zoomMove();
    m_zoomScale = view.zoomScale();
}

void PositionShifterTool::reset(VisualHost &host)
{
    // Visuals::SetVisual(dial, tool) (Visuals.cpp:256-283): the coefficients,
    // the Line's times, ChangeTool(tool, true), SetCurVisual.
    const Context ctx = context(host);
    if (ctx.active) {
        m_start = transform::startMs(*ctx.active);
        m_end = transform::endMs(*ctx.active);
    }
    takeView(host.view());
    if (!host.view().hasVideo() || ctx.width <= 0 || ctx.height <= 0) {
        m_coeffW = m_coeffH = 1.f;
        host.toolChanged();
        return;
    }
    m_find.setSeveralLines(severalLines(host.batchTargets(), host.activeLine()));
    const auto [selFrom, selTo] = host.editorSelection();
    m_find.setSelection(selFrom, selTo);
    // A reset drops a drag (Esc, another active Line: the gesture rule);
    // legacy's only resets came after its own release or key.
    if (m_dragging)
        m_numElem = -1;
    m_dragging = false;
    changeTool(toggled(), host);
    setCurVisual(host);
    host.toolChanged();
}

void PositionShifterTool::changeTool(int tool, VisualHost &host)
{
    // MoveAll::ChangeTool (VisualMoveAll.cpp:547-557): the drawings are left
    // out while a position or \move point is chosen.
    if (m_selectedTags == tool)
        return;
    m_selectedTags = tool;
    if ((tool & Position || tool & MoveStart || tool & MoveEnd) && tool & Drawing)
        m_selectedTags ^= Drawing;
    host.toolChanged();
}

void PositionShifterTool::setCurVisual(VisualHost &host)
{
    // MoveAll::SetCurVisual (VisualMoveAll.cpp:208-388).
    const Context ctx = context(host);
    if (!ctx.active)
        return;
    const u16 &editor = ctx.editorText;
    int alignment = 1;
    m_drawingPos = posnScale(ctx, m_find, true, &m_scale, &alignment, m_moveValues);
    if (m_moveValues[6] > 3)
        m_drawingPos = calcMovePos(ctx, m_moveValues, m_start, m_end);
    m_from = m_to = PointF{inX(m_drawingPos.x), inY(m_drawingPos.y)};
    m_elems.clear();

    double orx = m_drawingPos.x, ory = m_drawingPos.y;
    if (m_find.findTag(u"org\\(([^\\)]+)", editor, 0)) {
        if (m_find.getTwoValueDouble(orx, ory))
            m_elems.push_back({Origin, {inX(static_cast<float>(orx)), inY(static_cast<float>(ory))}, std::nullopt});
    }
    if (m_find.findTag(u"(i?clip[^\\)]+)", editor, 1)) {
        const u16 finding = m_find.result().finding;
        static const core::LegacyRegex vector(u"m ([0-9.-]+) ([0-9.-]+)", core::LegacyRegex::Advanced);
        Element elem;
        m_drawingOriginalPos = {0, 0};
        const auto repl = std::count(finding.begin(), finding.end(), u',');
        if (vector.matches(finding)) {
            elem.elem = {inX(static_cast<float>(atoi(group(vector, finding, 1).value_or(u"")))),
                         inY(static_cast<float>(atoi(group(vector, finding, 2).value_or(u""))))};
            if (repl > 0) {
                u16 clipWithoutScale;
                const u16 vscalestr = beforeFirst(finding, u',', &clipWithoutScale);
                const int vscale = atoi(afterFirst(vscalestr, u'('));
                if (vscale > 1)
                    m_vectorClipScale = static_cast<float>(std::pow(2, (vscale - 1)));
            }
            std::vector<ClipPoint> points = vectorPoints(finding);
            if (!points.empty()) {
                for (ClipPoint &p : points) {
                    // "weird but zoomMove have to be last" (VisualMoveAll.cpp:248-250).
                    p.x = (((p.x / m_coeffW) * m_zoomScale.x) / m_vectorClipScale) - (m_zoomMove.x * m_zoomScale.x);
                    p.y = (((p.y / m_coeffH) * m_zoomScale.y) / m_vectorClipScale) - (m_zoomMove.y * m_zoomScale.y);
                }
                elem.points = std::move(points);
            }
        } else {
            static const core::LegacyRegex rect(u"\\(([0-9.-]+)[, ]*([0-9.-]+)[, ]*([0-9.-]+)[, ]*([0-9.-]+)",
                                                core::LegacyRegex::Advanced);
            if (repl >= 3 && rect.matches(finding)) {
                const float pt1 = static_cast<float>(atof(group(rect, finding, 1).value_or(u"")));
                const float pt2 = static_cast<float>(atof(group(rect, finding, 2).value_or(u"")));
                float pt3 = static_cast<float>(atof(group(rect, finding, 3).value_or(u"")));
                float pt4 = static_cast<float>(atof(group(rect, finding, 4).value_or(u"")));
                elem.elem = {inX(pt1), inY(pt2)};
                pt3 = inX(pt3);
                pt4 = inY(pt4);
                elem.points = std::vector<ClipPoint>{{elem.elem.x, elem.elem.y, u"m", true},
                                                     {pt3, elem.elem.y, u"l", true},
                                                     {pt3, pt4, u"l", true},
                                                     {elem.elem.x, pt4, u"l", true}};
            }
        }
        elem.type = Clip;
        m_elems.push_back(std::move(elem));
        m_drawingPos = {0, 0};
    }
    if (m_find.findTag(u"p([0-9]+)", editor, 1)) {
        // ParseTags on the Line (tab->edit->line): from its second tag, a
        // \p's scale and the first drawing.
        u16 res;
        const auto tags = drawing::parseTags(transform::lineText(*ctx.active), {u"p"});
        if (tags.size() >= 2) {
            for (std::size_t i = 1; i < tags.size(); ++i) {
                if (tags[i].name == u"p") {
                    const int vscale = atoi(tags[i].value);
                    if (vscale > 1)
                        m_vectorDrawScale = static_cast<float>(std::pow(2, (vscale - 1)));
                }
                if (tags[i].name == u"pvector") {
                    res = tags[i].value;
                    break;
                }
            }
        }
        static const core::LegacyRegex first(u"m ([.0-9-]+) ([.0-9-]+)", core::LegacyRegex::Advanced);
        if (first.matches(res)) {
            Element elem;
            std::vector<ClipPoint> points = vectorPoints(res);
            const float firstPointx = static_cast<float>(atof(group(first, res, 1).value_or(u"")));
            const float firstPointy = static_cast<float>(atof(group(first, res, 2).value_or(u"")));
            // The drawing's position and coefficients divided by its scale.
            m_drawingScale = {m_coeffW / m_scale.x, m_coeffH / m_scale.y};
            m_drawingPos.x /= m_scale.x;
            m_drawingPos.y /= m_scale.y;
            if (!points.empty()) {
                m_drawingOriginalPos = {0, 0};
                float frz = 0;
                if (m_find.findTag(u"frz?([0-9.-]+)", editor, 0)) {
                    double frzd = 0.;
                    m_find.getDouble(frzd);
                    frz = static_cast<float>(frzd);
                } else {
                    double result = 0.;
                    toDouble(u16of(lineStyle(ctx, ctx.active->style).angle), result);
                    frz = static_cast<float>(result);
                }
                // The offset of an alignment other than 7, the rotation
                // about \org (VisualMoveAll.cpp:333-349).
                const PointF offsetxy = calcDrawingSize(alignment, points);
                const float rad = 0.01745329251994329576923690768489f;
                const PointF orgpivot{std::fabs((static_cast<float>(orx) / m_scale.x) - m_drawingPos.x),
                                      std::fabs((static_cast<float>(ory) / m_scale.y) - m_drawingPos.y)};
                const float s = std::sin(-frz * rad);
                const float c = std::cos(-frz * rad);
                for (ClipPoint &p : points) {
                    p.x -= offsetxy.x;
                    p.y -= offsetxy.y;
                    if (frz)
                        rotateDrawing(p, s, c, orgpivot);
                    p.x = ((((p.x) / m_drawingScale.x) - m_zoomMove.x) * m_zoomScale.x) / m_vectorClipScale;
                    p.y = ((((p.y) / m_drawingScale.y) - m_zoomMove.y) * m_zoomScale.y) / m_vectorClipScale;
                }
            }
            if (!points.empty())
                elem.points = std::move(points);
            elem.elem = {(((firstPointx + m_drawingPos.x) / m_drawingScale.x) - m_zoomMove.x) * m_zoomScale.x,
                         (((firstPointy + m_drawingPos.y) / m_drawingScale.y) - m_zoomMove.y) * m_zoomScale.y};
            elem.type = Drawing;
            m_elems.push_back(std::move(elem));
            // The zoom's move is already in the points.
            m_drawingPos.x = ((m_drawingPos.x / m_drawingScale.x) * m_zoomScale.x);
            m_drawingPos.y = ((m_drawingPos.y / m_drawingScale.y) * m_zoomScale.y);
        }
    }
    if (m_moveValues[6] == 2)
        m_elems.push_back({Position,
                           {inX(static_cast<float>(m_moveValues[0])), inY(static_cast<float>(m_moveValues[1]))},
                           std::nullopt});
    if (m_moveValues[6] >= 4) {
        m_elems.push_back({MoveStart,
                           {inX(static_cast<float>(m_moveValues[0])), inY(static_cast<float>(m_moveValues[1]))},
                           std::nullopt});
        m_elems.push_back({MoveEnd,
                           {inX(static_cast<float>(m_moveValues[2])), inY(static_cast<float>(m_moveValues[3]))},
                           std::nullopt});
    }
}

bool PositionShifterTool::beginEdit(VisualHost &host)
{
    // The targets: the selected Lines the Grid shows (SubsFile::
    // GetSelections, SubsFile.cpp:503-512), fixed when the gesture begins.
    if (host.gesture())
        return true;
    std::vector<core::LineId> targets;
    for (const core::LineId id : host.batchTargets())
        if (host.lineShown(id))
            targets.push_back(id);
    if (targets.empty())
        return false;
    return host.beginGesture(std::move(targets), std::string(familyInfo(Family::PositionShifter).history)).has_value();
}

void PositionShifterTool::pointer(const Pointer &event, VisualHost &host)
{
    // MoveAll::OnMouseEvent (VisualMoveAll.cpp:107-206). LeftIsDown or
    // RightIsDown hold; a middle button only ends a drag.
    takeView(host.view());
    const bool press = event.kind == Pointer::Kind::Press;
    const bool click = press && event.button == Pointer::Button::Left;
    const bool rightDown = press && event.button == Pointer::Button::Right;
    const bool holding = (event.kind == Pointer::Kind::Press || event.kind == Pointer::Kind::Move)
                             ? (event.leftDown || event.rightDown || click || rightDown)
                             : false;
    const int x = event.x, y = event.y;

    if (event.kind == Pointer::Kind::Release) {
        m_dragging = false;
        m_drawingOriginalPos = {0, 0};
        if (m_numElem >= 0) {
            if (beginEdit(host))
                changeInLines(true, host);
        } else if (host.gesture()) {
            host.cancelGesture();
        }
        m_numElem = -1;
    }

    if (click) {
        for (std::size_t i = 0; i < m_elems.size(); i++) {
            if (!(m_selectedTags & m_elems[i].type))
                continue;
            if ((std::fabs(m_elems[i].elem.x - x) < 8 && std::fabs(m_elems[i].elem.y - y) < 8) ||
                i == m_elems.size() - 1) {
                m_numElem = static_cast<int>(i);
                m_beforeMove = m_lastmove = m_elems[i].elem;
                m_diffsX = static_cast<int>(m_elems[i].elem.x - x);
                m_diffsY = static_cast<int>(m_elems[i].elem.y - y);
            }
        }
        m_firstmove = PointF{static_cast<float>(x), static_cast<float>(y)};
        m_axis = 0;
        m_dragging = m_numElem >= 0;
        if (m_numElem >= 0)
            (void)beginEdit(host);
    } else if (rightDown) {
        for (std::size_t i = 0; i < m_elems.size(); i++) {
            if (!(m_selectedTags & m_elems[i].type))
                continue;
            m_numElem = static_cast<int>(i);
            m_beforeMove = m_lastmove = m_elems[i].elem;
            m_diffsX = static_cast<int>(m_elems[i].elem.x - x);
            m_diffsY = static_cast<int>(m_elems[i].elem.y - y);
            break;
        }
        m_firstmove = PointF{static_cast<float>(x), static_cast<float>(y)};
        m_axis = 0;
        m_dragging = m_numElem >= 0;
        if (m_numElem >= 0)
            (void)beginEdit(host);
    } else if (holding && m_numElem >= 0) {
        Element &held = m_elems[static_cast<std::size_t>(m_numElem)];
        if (event.shift) {
            const int diffx = std::abs(static_cast<int>(m_firstmove.x) - x);
            const int diffy = std::abs(static_cast<int>(m_firstmove.y) - y);
            if (diffx != diffy)
                m_axis = diffx > diffy ? 1 : 2;
            m_lastmove = held.elem;
            if (m_axis == 1) {
                held.elem.x = static_cast<float>(x + m_diffsX);
                held.elem.y = m_beforeMove.y;
            } else if (m_axis == 2) {
                held.elem.y = static_cast<float>(y + m_diffsY);
                held.elem.x = m_beforeMove.x;
            }
            const PointF moving{held.elem.x - m_lastmove.x, held.elem.y - m_lastmove.y};
            for (std::size_t j = 0; j < m_elems.size(); j++) {
                if (static_cast<int>(j) == m_numElem || !(m_selectedTags & m_elems[j].type))
                    continue;
                if (m_axis == 1)
                    m_elems[j].elem.x += moving.x;
                else if (m_axis == 2)
                    m_elems[j].elem.y += moving.y;
            }
        } else {
            m_lastmove = held.elem;
            held.elem.x = static_cast<float>(x + m_diffsX);
            held.elem.y = static_cast<float>(y + m_diffsY);
            const PointF moving{held.elem.x - m_lastmove.x, held.elem.y - m_lastmove.y};
            for (std::size_t j = 0; j < m_elems.size(); j++) {
                if (static_cast<int>(j) == m_numElem || !(m_selectedTags & m_elems[j].type))
                    continue;
                m_elems[j].elem.x += moving.x;
                m_elems[j].elem.y += moving.y;
            }
        }
        changeInLines(false, host);
    }
    host.toolChanged();
}

bool PositionShifterTool::key(const Key &event, VisualHost &host)
{
    // MoveAll::OnKeyPress (VisualMoveAll.cpp:559-595): A/D/W/S move every
    // element of the chosen kinds one script pixel (Shift: a tenth) unless
    // Alt alone is held; each press is its own step.
    if (event.release)
        return false;
    const bool left = event.key == 'A';
    const bool right = event.key == 'D';
    const bool up = event.key == 'W';
    const bool down = event.key == 'S';
    const bool altOnly = event.alt && !event.control && !event.shift;
    if (!((left || right || up || down) && !altOnly))
        return false;
    takeView(host.view());
    float directionX = (left) ? -1 : (right) ? 1 : 0;
    float directionY = (up) ? -1 : (down) ? 1 : 0;
    if (event.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    directionX = (directionX / m_coeffW);
    directionY = (directionY / m_coeffH);
    m_numElem = -1;
    for (std::size_t j = 0; j < m_elems.size(); j++) {
        if (!(m_selectedTags & m_elems[j].type))
            continue;
        if (m_numElem == -1) {
            m_numElem = static_cast<int>(j);
            m_beforeMove = m_elems[j].elem;
        }
        m_elems[j].elem.x += directionX;
        m_elems[j].elem.y += directionY;
    }
    if (m_numElem != -1 && beginEdit(host))
        changeInLines(true, host);
    host.toolChanged();
    return true;
}

void PositionShifterTool::changeInLines(bool all, VisualHost &host)
{
    // MoveAll::ChangeInLines (VisualMoveAll.cpp:390-545): every target's text
    // with the chosen elements moved by the held element's offset; staged
    // while the drag runs (legacy's preview) and committed when `all`.
    Gesture *g = host.gesture();
    if (!g || m_numElem < 0 || m_numElem >= static_cast<int>(m_elems.size()))
        return;
    PointF moving{m_elems[static_cast<std::size_t>(m_numElem)].elem.x - m_beforeMove.x,
                  m_elems[static_cast<std::size_t>(m_numElem)].elem.y - m_beforeMove.y};
    m_drawingOriginalPos = moving;
    const bool tlMode = host.session() && translationMode(*host.session());
    for (const core::LineId id : g->targets()) {
        const core::LineRecord &line = g->before(id);
        const bool istexttl = tlMode && !line.translation.empty();
        u16 txt = u16of(istexttl ? line.translation : line.text);
        for (int k = 0; k < 6; k++) {
            const int type = m_selectedTags & (1 << k);
            if (!type)
                continue;
            const bool vector = type == Clip || type == Drawing;
            const float newcoeffW = type == Drawing ? m_coeffW / m_scale.x : m_coeffW;
            const float newcoeffH = type == Drawing ? m_coeffH / m_scale.y : m_coeffH;
            float vectorScale = 1.f;
            char16_t delimiter = vector ? u' ' : u',';
            static const core::LegacyRegex pos(u"pos\\(([^\\)]+)", core::LegacyRegex::Advanced);
            static const core::LegacyRegex org(u"org\\(([^\\)]+)", core::LegacyRegex::Advanced);
            static const core::LegacyRegex clip(u"i?clip\\(([^\\)]+)", core::LegacyRegex::Advanced);
            static const core::LegacyRegex draw(u"p[0-9-]+[^}]*} ?m ([^{]+)", core::LegacyRegex::Advanced);
            static const core::LegacyRegex move(u"move\\(([^\\)]+)", core::LegacyRegex::Advanced);
            const core::LegacyRegex &re = type == Position ? pos
                                          : type == Origin ? org
                                          : type == Clip   ? clip
                                          : type == Drawing ? draw
                                                            : move;
            std::size_t startMatch = 0, lenMatch = 0;
            std::size_t textPosition = 0;
            while (re.matches(mid(txt, textPosition))) {
                // The vector scale goes back for each match.
                moving = m_drawingOriginalPos;
                u16 visual;
                const auto m = re.match(1);
                if (m && m->first != std::u16string::npos) {
                    startMatch = m->first;
                    lenMatch = m->second;
                    u16 tmp = txt.substr(startMatch + textPosition, lenMatch);
                    if (type == Clip) {
                        const auto replacements = std::count(tmp.begin(), tmp.end(), u',');
                        if (replacements == 1) {
                            const u16 vectorclip = tmp;
                            const u16 clipScale = beforeFirst(vectorclip, u',', &tmp);
                            visual = clipScale + u",";
                            const int cscale = atoi(clipScale);
                            vectorScale = static_cast<float>(std::pow(2, (cscale - 1)));
                            moving = PointF{m_drawingOriginalPos.x * vectorScale, m_drawingOriginalPos.y * vectorScale};
                        } else if (replacements > 1) {
                            delimiter = u',';
                        }
                    }
                    const u16 delimiterText(1, delimiter);
                    int count = 0;
                    for (const u16 &raw : strtok(tmp, delimiter)) {
                        const u16 token = trim(raw);
                        double val = 0;
                        if (toDouble(token, val)) {
                            if (count % 2 == 0)
                                val += (((moving.x / m_zoomScale.x)) * newcoeffW);
                            else
                                val += (((moving.y / m_zoomScale.y)) * newcoeffH);
                            if (type == MoveStart && count > 1) {
                                visual += token + delimiterText;
                                continue;
                            } else if (type == MoveEnd && count != 2 && count != 3) {
                                visual += token + delimiterText;
                                count++;
                                continue;
                            }
                            if (vector)
                                visual += getfloat(static_cast<float>(val), type == Clip ? "6.0f" : "6.2f") + delimiterText;
                            else
                                visual += getfloat(static_cast<float>(val)) + delimiterText;
                            count++;
                        } else {
                            visual += token + delimiterText;
                            if (!vector)
                                count++;
                        }
                    }
                    if (!visual.empty())
                        visual.pop_back(); // RemoveLast
                    if (lenMatch)
                        txt.erase(startMatch + textPosition, lenMatch);
                    txt.insert(startMatch + textPosition, visual);
                } else {
                    // A match without the group would loop for ever.
                    textPosition++;
                }
                textPosition += startMatch + lenMatch;
            }
        }
        // CopyDialogue(...)->SetText: the translation when the Line has one.
        g->stage(id, u8(txt), transform::editsTranslation(line));
    }
    if (all) {
        // A release that changes no Line records nothing (legacy's
        // CopyDialogue made an undo step of it).
        if (!g->changesAnyLine()) {
            host.cancelGesture();
            return;
        }
        (void)host.commitGesture();
        // SetModified(VISUAL_POSITION_SHIFTER): the edit reaches the video
        // and SetVisual reads the Lines again (the host's reset).
    }
}

PointF PositionShifterTool::vectorPoint(const ClipPoint &point, PointF drawingPos) const
{
    // MoveAll::GetVector (VisualMoveAll.cpp:702-705).
    return {point.x + drawingPos.x + m_drawingOriginalPos.x, point.y + drawingPos.y + m_drawingOriginalPos.y};
}

void PositionShifterTool::drawShape(Overlay &out, const std::vector<ClipPoint> &shape, PointF drawingPos) const
{
    // MoveAll::DrawVisual's shape (VisualMoveAll.cpp:59-98) with DrawLine,
    // DrawCurve and Curve (597-722), in the 2-pixel D3DX line.
    std::vector<ClipPoint> points = shape;
    const auto line = [&out](PointF a, PointF b, std::uint32_t colour) {
        out.lines.push_back({a, b, 2, colour});
    };
    const auto polyline = [&line](const std::vector<PointF> &p, std::uint32_t colour) {
        for (std::size_t i = 0; i + 1 < p.size(); ++i)
            line(p[i], p[i + 1], colour);
    };
    const auto curve = [&](int pos, bool bspline, int nBsplinePoints, int currentPoint) {
        PointF control[4];
        for (int g = 0; g < 4; g++) {
            if (currentPoint > (nBsplinePoints - 1))
                currentPoint = 0;
            control[g] = vectorPoint(points[static_cast<std::size_t>(pos + currentPoint)], drawingPos);
            currentPoint++;
        }
        return flattenCurve(control, bspline);
    };
    const std::size_t size = points.size();
    // "l" or "b" cannot stand in for a missing "m" at the start.
    if (points[0].type != u"m")
        points[0].type = u"m";
    std::size_t g = (size < 2) ? 0 : 1;
    std::size_t lastM = 0;
    while (g < size) {
        if (points[g].type == u"l") {
            // DrawLine.
            std::size_t diff = 1;
            if (points[g - 1].type == u"s") {
                int j = static_cast<int>(g) - 2;
                while (j >= 0) {
                    if (points[static_cast<std::size_t>(j)].type != u"s")
                        break;
                    j--;
                }
                diff = static_cast<std::size_t>((static_cast<int>(g) - j) - 2);
            }
            line(vectorPoint(points[g - diff], drawingPos), vectorPoint(points[g], drawingPos), 0xFFBB0000);
            g++;
        } else if (points[g].type == u"b" || points[g].type == u"s") {
            // DrawCurve.
            const int i = static_cast<int>(g);
            const bool bspline = points[g].type == u"s";
            std::vector<PointF> v4;
            int pts = 3;
            if (bspline) {
                const int acpos = i - 1;
                int bssize = 1;
                int spos = i + 1;
                while (spos < static_cast<int>(size)) {
                    if (points[static_cast<std::size_t>(spos)].start)
                        break;
                    bssize++;
                    spos++;
                }
                pts = bssize;
                bssize++;
                for (int k = 0; k < bssize; k++) {
                    const auto part = curve(acpos, true, bssize, k);
                    v4.insert(v4.end(), part.begin(), part.end());
                }
                std::vector<PointF> v2(static_cast<std::size_t>(pts + 2));
                for (int j = 0, q = i - 1; j < bssize; j++, q++)
                    v2[static_cast<std::size_t>(j)] = vectorPoint(points[static_cast<std::size_t>(q)], drawingPos);
                v2[static_cast<std::size_t>(bssize)] = vectorPoint(points[static_cast<std::size_t>(i - 1)], drawingPos);
                polyline(v2, 0xFFAA33AA);
                const int iplus1 = (i + bssize - 2 < static_cast<int>(size) - 1) ? i + 1 : 0;
                if (i - 1 != 0 || iplus1 != 0) {
                    polyline({vectorPoint(points[static_cast<std::size_t>(i - 1)], drawingPos), v4[0],
                              vectorPoint(points[static_cast<std::size_t>(iplus1)], drawingPos)},
                             0xFFBB0000);
                }
            } else {
                const ClipPoint tmp = points[g - 1];
                if (tmp.type == u"s") {
                    int j = i - 2;
                    while (j >= 0) {
                        if (points[static_cast<std::size_t>(j)].type != u"s")
                            break;
                        j--;
                    }
                    const int diff = (i - j) - 2;
                    points[g - 1] = points[static_cast<std::size_t>(i - diff)];
                }
                v4 = curve(i - 1, false, 4, 0);
                points[g - 1] = tmp;
            }
            polyline(v4, 0xFFBB0000);
            g += static_cast<std::size_t>(pts);
        } else if (points[g].type != u"m") {
            g++;
        }
        if (g >= size || points[g].type == u"m") {
            if (g > 1)
                line(vectorPoint(points[g - 1], drawingPos), vectorPoint(points[lastM], drawingPos), 0xFFBB0000);
            lastM = g;
            g++;
        }
    }
}

Overlay PositionShifterTool::overlay(const VisualHost &host) const
{
    // MoveAll::DrawVisual (VisualMoveAll.cpp:45-105): position and \move
    // start squares, \move end circles, \org crosses (orange), and the clips
    // (crosses in blue) and drawings (magenta) with their shapes.
    Overlay out;
    PointF &drawingPos = m_drawingPos;
    for (const Element &e : m_elems) {
        const int type = e.type;
        if (!(m_selectedTags & type))
            continue;
        if (type == Position || type == MoveStart) {
            drawRect(out, e.elem);
        } else if (type == MoveEnd) {
            drawCircle(out, e.elem);
        } else if (type == Origin) {
            drawCross(out, e.elem, 0xFF8800FF);
        } else {
            if (e.points && !e.points->empty()) {
                if (type == Drawing && m_moveValues[6] > 2) {
                    // A \move drawing follows the video's time.
                    double table[7];
                    std::copy(std::begin(m_moveValues), std::end(m_moveValues), table);
                    Context ctx;
                    ctx.timeMs = static_cast<int>(host.videoTimeMs());
                    const PointF movePos = calcMovePos(ctx, table, m_start, m_end);
                    drawingPos.x = (((movePos.x / m_drawingScale.x)) * m_zoomScale.x);
                    drawingPos.y = (((movePos.y / m_drawingScale.y)) * m_zoomScale.y);
                }
                drawShape(out, *e.points, drawingPos);
            }
            drawCross(out, e.elem, type == Clip ? 0xFF0000FF : 0xFFFF00FF);
        }
    }
    return out;
}

} // namespace hikari::application::visual
