#include "hikari/application/visual_position.h"

#include "hikari/application/grid_split.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

// Built without floating-point contraction, as visual_view.cpp: legacy's
// float steps must round as its build did.

namespace hikari::application::visual {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

// Visuals.h:56-68's families' rail options use these icons (K1's set).
constexpr std::string_view kTwoPoints = "two-points";
constexpr std::string_view kFrameToScale = "frame-to-scale";
constexpr std::string_view kScaleX = "scale-x";
constexpr std::string_view kScaleY = "scale-y";

// HitTest's results (VisualPosition.cpp:31-38).
enum { LEFT = 1, RIGHT, TOP = 4, BOTTOM = 8, INSIDE = 16, OUTSIDE = 32 };

u16 ft(float value)
{
    return core::toUtf16(legacy::floatText(value));
}

u16 number(int value)
{
    const std::string s = std::to_string(value);
    return u16(s.begin(), s.end());
}

int atoi16(const u16 &text)
{
    const std::u8string u = core::toUtf8(text);
    return std::atoi(std::string(u.begin(), u.end()).c_str());
}

PointF sub(PointF a, PointF b)
{
    return {a.x - b.x, a.y - b.y};
}

// Visuals::IsInPos (Visuals.h:117-119) on wxPoints.
bool isInPos(int x, int y, int x2, int y2, int diff)
{
    return std::abs(x - x2) < diff && std::abs(y - y2) < diff;
}

// The key a nudge reads (Position/Move::OnKeyPress): any modifiers but Alt
// alone (evt.GetModifiers() != wxMOD_ALT).
bool altAlone(const Key &key)
{
    return key.alt && !key.control && !key.shift;
}

bool parseNumber(const std::u16string &text, double &out)
{
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        s += static_cast<char>(c);
    }
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ')
        ++a;
    while (b > a && s[b - 1] == ' ')
        --b;
    if (a < b && s[a] == '+')
        ++a;
    if (a >= b)
        return false;
    const auto [end, ec] = std::from_chars(s.data() + a, s.data() + b, out);
    return ec == std::errc() && end == s.data() + b && std::isfinite(out);
}

// The record a target has for the gesture: its text as the gesture began, or
// as staged (legacy committed each step and read the Line back).
core::LineRecord stagedRecord(const Gesture &gesture, core::LineId id)
{
    core::LineRecord rec = gesture.before(id);
    const bool tl = editsTranslation(rec);
    if (const auto staged = gesture.staged(id, tl))
        (tl ? rec.translation : rec.text) = *staged;
    return rec;
}

} // namespace

const std::array<std::u16string_view, 21> &positionAlignments()
{
    static const std::array<std::u16string_view, 21> list{
        u"Bottom-left",  u"Bottom-center",  u"Bottom-right",  u"Middle-left",  u"Center",       u"Middle-right",
        u"Top-left",     u"Top-center",     u"Top-right",     u"Left-below",   u"Center-below", u"Right-below",
        u"Left-above",   u"Center-above",   u"Right-above",   u"Before-top",   u"Before-center", u"Before-bottom",
        u"After-top",    u"After-center",   u"After-bottom"};
    return list;
}

// ---------------------------------------------------------------- Position

int PositionTool::toolValue() const
{
    // PositionItem::GetItemToggled (VideoToolbar.h:229-236).
    int result = m_an + 1;
    for (int i = 0; i < 3; i++)
        if (m_toggled[static_cast<std::size_t>(i)])
            result |= (32 << i);
    return result;
}

void PositionTool::selected(VisualHost &host)
{
    // Visuals::Get made a new Position (VisualPosition.cpp:57-60, Visuals.h:
    // 264-300's initial values) and SetVisual handed it the toolbar's state
    // (ChangeTool(tool, true), Visuals.cpp:281).
    m_data.clear();
    m_helperX = m_helperY = 0;
    m_hasHelperLine = m_movingHelperLine = false;
    m_firstmove = {0, 0};
    m_axis = 0;
    m_rectangle = {};
    m_textSize = {};
    m_border = {};
    m_extlead = {};
    m_drawingPosition = {};
    m_diffs = {};
    m_curLinePosition = {};
    m_hasRectangle = m_hasX = m_hasY = m_rectangleVisible = false;
    m_grabbed = -1;
    m_alignment = 1;
    m_curLineAlignment = static_cast<unsigned char>(-1);
    std::fill(std::begin(m_moveValues), std::end(m_moveValues), 0.0);
    m_inGesture = m_cancelled = m_keyNudge = false;
    m_dataFromStaged = m_dummyCommit = m_skipReset = false;
    changeTool(toolValue(), true, host);
}

void PositionTool::reset(VisualHost &host)
{
    // Visuals::SetVisual(dial, tool) (Visuals.cpp:256-283): ChangeTool with
    // the toolbar's state, then SetCurVisual. A gesture the host dropped
    // (Esc) leaves the rest of its drag ignored.
    if (m_inGesture && !host.gesture()) {
        m_cancelled = true;
        m_keyNudge = false;
    }
    m_inGesture = host.gesture() != nullptr && m_inGesture;
    if (m_dummyCommit)
        return;
    if (m_skipReset && host.session() && host.session()->revision() == m_committedRevision) {
        m_skipReset = false;
        return;
    }
    m_skipReset = false;
    changeTool(toolValue(), true, host);
    setCurVisual(host, false);
}

void PositionTool::setCurVisual(VisualHost &host, bool fromGesture)
{
    // Position::SetCurVisual (VisualPosition.cpp:414-453).
    const ScriptState state = scriptState(host);
    const int oldalignment = m_curLineAlignment;
    const Gesture *gesture = fromGesture ? host.gesture() : nullptr;
    m_dataFromStaged = gesture != nullptr;
    if (state.active) {
        core::LineRecord edit = *state.active;
        if (gesture && std::ranges::count(gesture->targets(), edit.id))
            edit = stagedRecord(*gesture, edit.id);
        int an = 0;
        (void)posnScale(state, Family::Position, edit, lineText(edit), &an, m_moveValues);
        m_curLineAlignment = static_cast<unsigned char>(an);
    }
    m_data.clear();
    const std::vector<core::LineId> targets = gesture ? gesture->targets() : host.batchTargets();
    for (const auto id : targets) {
        // "fix to work with editbox changes": the active Line as the editor
        // has it (its pending draft), the others as committed.
        std::optional<core::LineRecord> rec;
        if (gesture)
            rec = stagedRecord(*gesture, id);
        else if (const auto *line = scriptLine(state, id))
            rec = *line;
        if (!rec || rec->comment)
            continue;
        const LinePosition lp = linePosition(state, *rec, true);
        Data d;
        d.line = id;
        d.pos = host.view().scriptToView(lp.pos);
        d.lastpos = d.pos;
        d.textStart = lp.textStart;
        d.textLength = lp.textLength;
        d.putInBracket = lp.putInBracket;
        if (lp.move && lp.move->count != 0) {
            std::array<double, 4> table = lp.move->values;
            const PointF end = host.view().scriptToView(
                {static_cast<float>(table[0]), static_cast<float>(table[1])});
            // GetCalculatedInPosX/Y take floats; the table keeps doubles.
            table[0] = end.x;
            table[1] = end.y;
            d.move = table;
        }
        m_data.push_back(d);
    }
    if (m_hasRectangle) {
        getPositioningData(host);
        // `oldalignment != -1` compares the byte with -1, so it always holds.
        if (m_rectangleVisible && oldalignment != m_curLineAlignment && oldalignment != -1) {
            sortPoints();
            setPosition(host);
            // ChangeMultiline(true, true): SetModified's dummy commit, after
            // which legacy did not set the tool again (no ShowEditOnVideo),
            // so its next drag replaced the old tag's span in the new text
            // (`pos-rect-other-line`). T2-rect-reread: the Lines are read
            // back here; the alignment now matches, so this does not repeat.
            if (!host.gesture()) {
                m_dummyCommit = true;
                const bool committed = commitAll(host);
                m_dummyCommit = false;
                if (committed)
                    setCurVisual(host, false);
                if (host.session()) {
                    m_skipReset = true;
                    m_committedRevision = host.session()->revision();
                }
            }
        }
    }
    host.toolChanged();
}

void PositionTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // Position::ChangeTool (VisualPosition.cpp:188-210).
    if (!m_hasRectangle && (tool & 32)) {
        if (!blockSetCurVisual)
            getPositioningData(host);
        m_rectangleVisible = false;
    }
    m_hasRectangle = tool & 32;
    m_hasX = tool & 64;
    m_hasY = tool & 128;
    const int reset = 32 | 64 | 128;
    int newalignment = tool | reset;
    newalignment ^= reset;
    if (newalignment != m_alignment) {
        m_alignment = static_cast<unsigned char>(newalignment);
        sortPoints();
        setPosition(host);
        (void)commitAll(host); // ChangeMultiline(true)
    } else {
        host.toolChanged();
    }
}

bool PositionTool::ensureGesture(VisualHost &host)
{
    if (host.gesture())
        return m_inGesture;
    m_skipReset = false;
    auto begun = host.beginGesture(host.batchTargets(), std::string(familyInfo(Family::Position).history));
    m_inGesture = begun.has_value();
    return m_inGesture;
}

bool PositionTool::stageAll(VisualHost &host)
{
    // Position::ChangeMultiline (VisualPosition.cpp:455-521) for the Lines
    // the gesture targets: each Line's \pos(x,y) at getfloat's precision in
    // the tag's place (TextPos), in a new block when the Line had none.
    if (m_data.empty() || !ensureGesture(host))
        return false;
    Gesture *gesture = host.gesture();
    const auto &targets = gesture->targets();
    for (const Data &d : m_data) {
        if (!std::ranges::count(targets, d.line))
            continue;
        // The text the positions were read from: as the gesture began, or as
        // a nudge staged it (legacy had committed it and read it back).
        const core::LineRecord base = m_dataFromStaged ? stagedRecord(*gesture, d.line) : gesture->before(d.line);
        const PointF out = host.view().viewToScript(d.pos);
        u16 visual = u"\\pos(" + ft(out.x) + u"," + ft(out.y) + u")";
        u16 txt = lineText(base);
        if (d.putInBracket)
            visual = u"{" + visual + u"}";
        const std::size_t at = std::min(d.textStart, txt.size());
        txt.replace(at, std::min(d.textLength, txt.size() - at), visual);
        gesture->stage(d.line, core::toUtf8(txt), editsTranslation(base));
    }
    host.toolChanged();
    return true;
}

bool PositionTool::commitAll(VisualHost &host)
{
    if (!stageAll(host))
        return false;
    m_inGesture = false;
    m_keyNudge = false;
    return host.commitGesture().has_value();
}

void PositionTool::setMovePosition(Data &data, int time) const
{
    // Position::SetMovePosition (VisualPosition.cpp:762-771).
    if (!data.move)
        return;
    data.pos = calcMovePosition(data.pos, data.move->data(), time);
    data.lastpos = calcMovePosition(data.lastpos, data.move->data(), time);
    data.move.reset();
}

void PositionTool::savePosition(VisualHost &host)
{
    // Position::SavePosition (VisualPosition.cpp:717-727): commit, then the
    // positions become the start of the next drag.
    (void)commitAll(host);
    const int time = static_cast<int>(host.videoTimeMs());
    for (Data &d : m_data) {
        setMovePosition(d, time);
        d.lastpos = d.pos;
    }
    m_movingHelperLine = false;
}

void PositionTool::setPointFor(core::LineId active, PointF point)
{
    // The active Line's position, the others moved by the same distance, all
    // \move tables dropped (VisualPosition.cpp:331-349, 690-710).
    for (std::size_t i = 0; i < m_data.size(); i++) {
        Data &pos = m_data[i];
        if (pos.line != active)
            continue;
        pos.pos = point;
        const PointF diff = sub(pos.pos, pos.lastpos);
        pos.lastpos = pos.pos;
        pos.move.reset();
        for (std::size_t j = 0; j < m_data.size(); j++) {
            if (j == i)
                continue;
            Data &posj = m_data[j];
            posj.pos = {posj.pos.x + diff.x, posj.pos.y + diff.y};
            posj.lastpos = posj.pos;
            posj.move.reset();
        }
        break;
    }
}

bool PositionTool::cancelledByHost(VisualHost &host)
{
    if (m_inGesture && !host.gesture()) {
        m_inGesture = false;
        m_cancelled = true;
        m_keyNudge = false;
    }
    return m_cancelled;
}

void PositionTool::pointer(const Pointer &evt, VisualHost &host)
{
    // Position::OnMouseEvent (VisualPosition.cpp:212-404). The host blocks
    // events while nothing is shown (blockevents).
    if (evt.kind == Pointer::Kind::Press || evt.kind == Pointer::Kind::DoubleClick)
        m_cancelled = false;
    else if (cancelledByHost(host))
        return;
    const bool click = evt.kind == Pointer::Kind::Press && evt.button == Pointer::Button::Left;
    const bool dclick = evt.kind == Pointer::Kind::DoubleClick && evt.button == Pointer::Button::Left;
    const bool holding = evt.leftDown;
    const int x = evt.x, y = evt.y;

    if (m_hasRectangle) {
        rectanglePointer(evt, host);
        return;
    }
    const bool rightDown = evt.kind == Pointer::Kind::Press && evt.button == Pointer::Button::Right;
    if (rightDown || dclick) {
        // A right click puts the active Line at the pointer and commits; a
        // double click shows it and the button's release commits.
        if (const auto active = host.activeLine()) {
            setPointFor(*active, {static_cast<float>(x), static_cast<float>(y)});
            if (rightDown)
                (void)commitAll(host);
            else
                (void)stageAll(host);
        }
        host.toolChanged();
        return;
    }
    if (evt.kind == Pointer::Kind::Release && evt.button == Pointer::Button::Left)
        savePosition(host);
    if (m_movingHelperLine) {
        m_helperX = x;
        m_helperY = y;
        host.toolChanged();
        return;
    }
    if (click) {
        if (isInPos(x, y, m_helperX, m_helperY, 4)) {
            m_movingHelperLine = true;
            return;
        }
        if (host.batchTargets().size() != m_data.size())
            setCurVisual(host, false);
        m_firstmove = {static_cast<float>(x), static_cast<float>(y)};
        m_axis = 0;
        (void)ensureGesture(host);
    } else if (holding && evt.kind != Pointer::Kind::Release) {
        if (ensureGesture(host)) {
            const int time = static_cast<int>(host.videoTimeMs());
            for (Data &d : m_data) {
                setMovePosition(d, time);
                d.pos.x = d.lastpos.x - (m_firstmove.x - x);
                d.pos.y = d.lastpos.y - (m_firstmove.y - y);
                if (evt.shift) {
                    const int diffx = static_cast<int>(std::abs(m_firstmove.x - x));
                    const int diffy = static_cast<int>(std::abs(m_firstmove.y - y));
                    if (diffx != diffy)
                        m_axis = diffx > diffy ? 2 : 1;
                    if (m_axis == 1)
                        d.pos.x = d.lastpos.x;
                    else if (m_axis == 2)
                        d.pos.y = d.lastpos.y;
                }
            }
            (void)stageAll(host);
        }
    }
    if (evt.kind == Pointer::Kind::Press && evt.button == Pointer::Button::Middle) {
        m_hasHelperLine = (m_hasHelperLine && isInPos(x, y, m_helperX, m_helperY, 4)) ? false : true;
        m_helperX = x;
        m_helperY = y;
    }
    host.toolChanged();
}

void PositionTool::rectanglePointer(const Pointer &evt, VisualHost &host)
{
    // Position::OnMouseEvent's "Set position by rectangle" (VisualPosition.
    // cpp:222-328). The pointer's shape over the edges is the panel's.
    const bool click = evt.kind == Pointer::Kind::Press && evt.button == Pointer::Button::Left;
    const bool dclick = evt.kind == Pointer::Kind::DoubleClick && evt.button == Pointer::Button::Left;
    const bool holding = evt.leftDown;
    int x = evt.x, y = evt.y;
    const VideoView &view = host.view();
    const float coeffW = view.coeffW(), coeffH = view.coeffH();
    const PointF zoomScale = view.zoomScale(), zoomMove = view.zoomMove();
    if (evt.kind == Pointer::Kind::Release) {
        if (m_rectangleVisible) {
            if (m_rectangle[1].y == m_rectangle[0].y || m_rectangle[1].x == m_rectangle[0].x)
                m_rectangleVisible = false;
            sortPoints();
        }
        if (m_rectangleVisible) {
            setPosition(host);
            (void)commitAll(host);
        } else if (m_inGesture && host.gesture()) {
            m_inGesture = false;
            host.cancelGesture(); // nothing placed: nothing to write
        }
    }
    if (click || dclick) {
        m_grabbed = OUTSIDE;
        const float pointx = ((x / zoomScale.x) + zoomMove.x) * coeffW;
        const float pointy = ((y / zoomScale.y) + zoomMove.y) * coeffH;
        if (m_rectangleVisible) {
            m_grabbed = hitTest({static_cast<float>(x), static_cast<float>(y)}, true, host);
            if (m_grabbed == INSIDE) {
                if (m_rectangle[0].x <= pointx && m_rectangle[1].x >= pointx && m_rectangle[0].y <= pointy &&
                    m_rectangle[1].y >= pointy) {
                    m_diffs.x = static_cast<float>(x);
                    m_diffs.y = static_cast<float>(y);
                }
            }
        }
        if (!m_rectangleVisible || m_grabbed == OUTSIDE) {
            m_rectangle[0].x = m_rectangle[1].x = pointx;
            m_rectangle[0].y = m_rectangle[1].y = pointy;
            m_grabbed = OUTSIDE;
            m_rectangleVisible = true;
        }
        if (click)
            (void)ensureGesture(host);
    } else if (holding && m_grabbed != -1 && evt.kind != Pointer::Kind::Release) {
        const IntRect video = view.videoRect();
        if (m_grabbed < INSIDE) {
            if ((m_grabbed & LEFT) || (m_grabbed & RIGHT)) {
                x = std::max(video.left, std::min(x, video.right)); // MID(VideoSize.x, x, VideoSize.width)
                const int posInTable = (m_grabbed & RIGHT) ? 1 : 0;
                m_rectangle[static_cast<std::size_t>(posInTable)].x =
                    ((((x + m_diffs.x) / zoomScale.x) + zoomMove.x) * coeffW);
                if ((m_grabbed & LEFT) && m_rectangle[0].x > m_rectangle[1].x)
                    m_rectangle[0].x = m_rectangle[1].x;
                if ((m_grabbed & RIGHT) && m_rectangle[1].x < m_rectangle[0].x)
                    m_rectangle[1].x = m_rectangle[0].x;
            }
            if ((m_grabbed & TOP) || (m_grabbed & BOTTOM)) {
                y = std::max(video.top, std::min(y, video.bottom));
                const int posInTable = (m_grabbed & BOTTOM) ? 1 : 0;
                m_rectangle[static_cast<std::size_t>(posInTable)].y =
                    ((((y + m_diffs.y) / zoomScale.y) + zoomMove.y) * coeffH);
                if ((m_grabbed & TOP) && m_rectangle[0].y > m_rectangle[1].y)
                    m_rectangle[0].y = m_rectangle[1].y;
                if ((m_grabbed & BOTTOM) && m_rectangle[1].y < m_rectangle[0].y)
                    m_rectangle[1].y = m_rectangle[0].y;
            }
        } else if (m_grabbed == INSIDE) {
            const float movex = (((x - m_diffs.x) / zoomScale.x) * coeffW);
            const float movey = (((y - m_diffs.y) / zoomScale.y) * coeffH);
            m_rectangle[0].x += movex;
            m_rectangle[0].y += movey;
            m_rectangle[1].x += movex;
            m_rectangle[1].y += movey;
            m_diffs.x = static_cast<float>(x);
            m_diffs.y = static_cast<float>(y);
        } else if (m_grabbed == OUTSIDE) {
            m_rectangle[1].x = ((x / zoomScale.x) + zoomMove.x) * coeffW;
            m_rectangle[1].y = ((y / zoomScale.y) + zoomMove.y) * coeffH;
        }
        setPosition(host);
        if (m_rectangleVisible)
            (void)stageAll(host);
    }
    host.toolChanged();
}

int PositionTool::hitTest(PointF pos, bool diff, const VisualHost &host)
{
    // Position::HitTest (VisualPosition.cpp:560-600), its int truncations
    // and its use of the first corner's x in the y test kept.
    int resultX = 0, resultY = 0, resultInside = 0, resultFinal = 0, oldpointx = 0, oldpointy = 0;
    for (int i = 0; i < 2; i++) {
        const PointF point = host.view().scriptToView(m_rectangle[static_cast<std::size_t>(i)]);
        const float pointx = point.x, pointy = point.y;
        if (std::abs(pos.x - pointx) < 5) {
            if (diff)
                m_diffs.x = pointx - pos.x;
            resultX |= (i + 1);
        }
        if (std::abs(pos.y - pointy) < 5) {
            if (diff)
                m_diffs.y = pointy - pos.y;
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
            oldpointx = static_cast<int>(pointx);
            oldpointy = static_cast<int>(pointy);
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

void PositionTool::sortPoints()
{
    // Position::SortPoints (VisualPosition.cpp:602-614).
    if (m_rectangle[1].y < m_rectangle[0].y)
        std::swap(m_rectangle[0].y, m_rectangle[1].y);
    if (m_rectangle[1].x < m_rectangle[0].x)
        std::swap(m_rectangle[0].x, m_rectangle[1].x);
}

void PositionTool::setPosition(VisualHost &host)
{
    // Position::SetPosition (VisualPosition.cpp:616-715): the text placed in
    // the rectangle for the chosen alignment, then the active Line put there
    // and the other Lines moved by the same distance.
    if (!(m_hasRectangle && m_rectangleVisible))
        return;
    const int an = m_alignment;
    const float bordery = m_border[0].y;
    const float borderx = m_border[0].x;
    const float bordery1 = m_border[1].y;
    const float borderx1 = m_border[1].x;
    const float rectx = m_rectangle[0].x < m_rectangle[1].x ? m_rectangle[0].x : m_rectangle[1].x;
    const float recty = m_rectangle[0].y < m_rectangle[1].y ? m_rectangle[0].y : m_rectangle[1].y;
    const float rectx1 = m_rectangle[0].x > m_rectangle[1].x ? m_rectangle[0].x : m_rectangle[1].x;
    const float recty1 = m_rectangle[0].y > m_rectangle[1].y ? m_rectangle[0].y : m_rectangle[1].y;
    float x = rectx, y = recty;
    if (an <= 15) {
        if (an % 3 == 0)
            x = rectx1 - (m_textSize.x + borderx1 - 2);
        else if (an % 3 == 1)
            x += borderx + 2;
        else if (an % 3 == 2)
            x += (rectx1 - (rectx + m_textSize.x) + borderx) / 2;
    } else if (an <= 18) {
        x = rectx - (m_textSize.x + borderx1 - 2);
    } else if (an <= 21) {
        x = rectx1 + borderx;
    }
    if (an <= 3) {
        y = recty1 - bordery;
    } else if (an <= 6) {
        y += (((recty1 - recty) + m_textSize.y + m_extlead.y) / 2);
    } else if (an <= 9) {
        y += m_textSize.y + bordery1 - (m_extlead.x - m_extlead.y);
    } else if (an <= 12) {
        y = recty1 + m_textSize.y + bordery;
    } else if (an <= 15) {
        y = recty;
    } else if (an <= 21) {
        if (an % 3 == 0)
            y = recty1 - bordery;
        else if (an % 3 == 2)
            y += (((recty1 - recty) + m_textSize.y + m_extlead.y) / 2);
        else if (an % 3 == 1)
            y += m_textSize.y + bordery1 - (m_extlead.x - m_extlead.y);
    }
    const auto active = host.activeLine();
    for (std::size_t i = 0; i < m_data.size(); i++) {
        Data &pos = m_data[i];
        if (!active || pos.line != *active)
            continue;
        if (m_hasX)
            pos.pos.x = x + m_curLinePosition.x;
        if (m_hasY)
            pos.pos.y = y + m_curLinePosition.y;
        pos.pos = positionToVideo(pos.pos, m_hasX, m_hasY, host);
        const PointF diff = sub(pos.pos, pos.lastpos);
        pos.lastpos = pos.pos;
        pos.move.reset();
        for (std::size_t j = 0; j < m_data.size(); j++) {
            if (j == i)
                continue;
            Data &posj = m_data[j];
            posj.pos = {posj.pos.x + diff.x, posj.pos.y + diff.y};
            posj.lastpos = posj.pos;
            posj.move.reset();
        }
        break;
    }
}

PointF PositionTool::positionToVideo(PointF point, bool changeX, bool changeY, const VisualHost &host) const
{
    // Position::PositionToVideo (VisualPosition.cpp:729-734).
    const PointF in = host.view().scriptToView(point);
    return {changeX ? in.x : point.x, changeY ? in.y : point.y};
}

void PositionTool::getPositioningData(VisualHost &host)
{
    // Position::GetPositioningData (VisualPosition.cpp:736-760). The byte
    // curLineAlingment is never -1, so it is not read again here.
    const ScriptState state = scriptState(host);
    if (!state.active)
        return;
    const TextSize size = textSize(state, *state.active, nullptr, true);
    m_textSize = size.size;
    m_extlead = size.extraLead;
    m_drawingPosition = size.drawingPosition;
    m_border = size.bordShad;
    m_curLinePosition = m_drawingPosition;
    if (m_curLineAlignment % 3 == 0)
        m_curLinePosition.x += m_textSize.x;
    else if (m_curLineAlignment % 3 == 2)
        m_curLinePosition.x += (m_textSize.x) / 2;
    if (m_curLineAlignment <= 3) {
    } else if (m_curLineAlignment <= 6) {
        m_curLinePosition.y -= (m_textSize.y / 2);
    } else if (m_curLineAlignment <= 9) {
        m_curLinePosition.y -= m_textSize.y;
    }
}

bool PositionTool::key(const Key &evt, VisualHost &host)
{
    // Position::OnKeyPress (VisualPosition.cpp:523-558): A/D/W/S nudge every
    // Line one script pixel (a tenth with Shift). Legacy committed each
    // press; here the presses while the key is held make one gesture
    // committed on its release (the transaction rule, #55).
    const bool left = evt.key == 'A';
    const bool right = evt.key == 'D';
    const bool up = evt.key == 'W';
    const bool down = evt.key == 'S';
    if (!(left || right || up || down))
        return false;
    if (evt.release) {
        if (evt.autoRepeat || !m_keyNudge)
            return m_keyNudge;
        (void)commitAll(host);
        return true;
    }
    if (altAlone(evt))
        return false;
    float directionX = left ? -1.f : right ? 1.f : 0.f;
    float directionY = up ? -1.f : down ? 1.f : 0.f;
    if (evt.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    const VideoView &view = host.view();
    directionX = (directionX / view.coeffW()) * view.zoomScale().x;
    directionY = (directionY / view.coeffH()) * view.zoomScale().y;
    if (!ensureGesture(host))
        return true;
    if (!m_keyNudge)
        setCurVisual(host, true);
    const int time = static_cast<int>(host.videoTimeMs());
    for (Data &d : m_data) {
        setMovePosition(d, time);
        d.pos.x = d.lastpos.x + directionX;
        d.pos.y = d.lastpos.y + directionY;
    }
    (void)stageAll(host);
    m_keyNudge = true;
    // Legacy's commit read the Lines back (SetCurVisual), so the next press
    // starts from the written text.
    setCurVisual(host, true);
    return true;
}

Overlay PositionTool::overlay(const VisualHost &host) const
{
    // Position::Draw (VisualPosition.cpp:63-134).
    Overlay out;
    const EditSession *session = host.session();
    if (!session)
        return out;
    const int time = static_cast<int>(host.videoTimeMs());
    bool nothingToShow = true;
    for (const Data &pos : m_data) {
        const core::LineRecord *dial = nullptr;
        for (const auto *l : session->document().lines())
            if (l->id == pos.line)
                dial = l;
        if (!dial)
            continue;
        if (time >= startMs(*dial) && time < endMs(*dial)) {
            PointF conv = pos.pos;
            if (pos.move)
                conv = calcMovePosition(conv, pos.move->data(), time);
            drawCross(out, conv);
            drawRect(out, conv);
            nothingToShow = false;
        }
    }
    if (m_hasRectangle && !nothingToShow && m_rectangleVisible) {
        const PointF p1 = host.view().scriptToView(m_rectangle[0]);
        const PointF p2 = host.view().scriptToView(m_rectangle[1]);
        const PointF v[5] = {p1, {p2.x, p1.y}, p2, {p1.x, p2.y}, p1};
        for (int i = 0; i < 4; i++)
            out.lines.push_back({v[i], v[i + 1], 2, kHandleBorder});
    }
    if (!nothingToShow && m_hasHelperLine)
        drawHelperLine(out, m_helperX, m_helperY, host.view());
    return out;
}

std::optional<LineWarning> PositionTool::warning(const VisualHost &host) const
{
    // Position::Draw's nothintoshow: the warning (and blocked events) only
    // while none of its Lines is visible; the comment text when the active
    // Line is a comment (notDialogue).
    const EditSession *session = host.session();
    if (!session)
        return LineWarning::None;
    const int time = static_cast<int>(host.videoTimeMs());
    for (const Data &pos : m_data)
        for (const auto *l : session->document().lines())
            if (l->id == pos.line && time >= startMs(*l) && time < endMs(*l))
                return LineWarning::None;
    bool comment = false;
    if (const auto active = host.activeLine()) {
        const auto draft = session->draftRecord();
        if (draft && draft->id == *active)
            comment = draft->comment;
        else
            for (const auto *l : session->document().lines())
                if (l->id == *active)
                    comment = l->comment;
    }
    return comment ? LineWarning::Comment : LineWarning::NotVisible;
}

void PositionTool::blocked(VisualHost &host)
{
    // Position::Draw's nothintoshow ends a helper-cross drag
    // (VisualPosition.cpp:107-113, DrawWx 162-168: movingHelperLine =
    // false), so the next event after the Line shows again does not move it.
    (void)host;
    m_movingHelperLine = false;
}

std::vector<ToolValue> PositionTool::values(const VisualHost &host) const
{
    // The numeric alternative (media.md): the active Line's position in
    // script coordinates, as GetVisual writes it.
    const auto active = host.activeLine();
    for (const Data &d : m_data)
        if (active && d.line == *active) {
            const PointF out = host.view().viewToScript(d.pos);
            return {{"x", u"X", ft(out.x), true}, {"y", u"Y", ft(out.y), true}};
        }
    return {};
}

bool PositionTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host)
{
    // The active Line's \pos at the typed point, the other Lines moved with
    // it, as a right click there would (VisualPosition.cpp:330-352).
    double value = 0;
    const auto active = host.activeLine();
    if ((name != "x" && name != "y") || !active || !parseNumber(text, value) || host.gesture())
        return false;
    for (const Data &d : m_data) {
        if (d.line != *active)
            continue;
        PointF script = host.view().viewToScript(d.pos);
        (name == "x" ? script.x : script.y) = static_cast<float>(value);
        setPointFor(*active, host.view().scriptToView(script));
        return commitAll(host);
    }
    return false;
}

std::vector<ToolOption> PositionTool::options(const VisualHost &host) const
{
    // PositionItem (VideoToolbar.cpp:995-1129; help texts 82-86, 1113).
    (void)host;
    std::vector<ToolOption> out;
    ToolOption rect;
    rect.name = "byRectangle";
    rect.iconRole = std::string(kFrameToScale);
    rect.tooltip = u"Set position by rectangle.\nAfter drawing a rectangle, the line will be\npositioned on one or "
                   u"both axes\nfor the selected alignment.";
    rect.checked = m_toggled[0];
    out.push_back(rect);
    ToolOption x;
    x.name = "x";
    x.iconRole = std::string(kScaleX);
    x.tooltip = u"Positioning in X axis";
    x.checked = m_toggled[1];
    x.enabled = m_toggled[0];
    out.push_back(x);
    ToolOption y;
    y.name = "y";
    y.iconRole = std::string(kScaleY);
    y.tooltip = u"Positioning in Y axis";
    y.checked = m_toggled[2];
    y.enabled = m_toggled[0];
    out.push_back(y);
    ToolOption an;
    an.name = "alignment";
    an.kind = ToolOption::Kind::Choice;
    an.tooltip = u"Text placing works similar like in styles";
    for (const auto a : positionAlignments())
        an.choices.emplace_back(a);
    an.index = m_an;
    out.push_back(an);
    return out;
}

bool PositionTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // PositionItem::OnMouseEvent / the alignment choice (VideoToolbar.cpp:
    // 995-1042, 1104-1110): X and Y are greyed without the rectangle, one of
    // them stays on, and the toolbar hands the tool its state (ChangeTool).
    if (host.gesture())
        return false;
    if (name == "alignment") {
        if (value < 0 || value >= static_cast<int>(positionAlignments().size()))
            return false;
        m_an = value;
    } else {
        const int elem = name == "byRectangle" ? 0 : name == "x" ? 1 : name == "y" ? 2 : -1;
        if (elem < 0 || (elem > 0 && !m_toggled[0]))
            return false;
        if (m_toggled[static_cast<std::size_t>(elem)] == (value != 0))
            return true;
        m_toggled[static_cast<std::size_t>(elem)] = !m_toggled[static_cast<std::size_t>(elem)];
        if (elem == 1 || elem == 2) {
            if (!m_toggled[1] && !m_toggled[2])
                m_toggled[elem == 1 ? 2 : 1] = true;
        }
    }
    applyToolValue(toolValue(), host);
    return true;
}

void PositionTool::applyToolValue(int value, VisualHost &host)
{
    m_an = std::max(0, std::min((value & 31) - 1, static_cast<int>(positionAlignments().size()) - 1));
    for (int i = 0; i < 3; i++)
        m_toggled[static_cast<std::size_t>(i)] = (value & (32 << i)) != 0;
    changeTool(value, false, host);
    host.toolChanged();
}

// -------------------------------------------------------------------- Move

void MoveTool::selected(VisualHost &host)
{
    // Visuals::Get made a new Move (VisualMove.cpp:27-34, Visuals.h:302-339).
    m_from = m_to = m_lastFrom = m_lastTo = m_firstmove = m_lastmove = m_moveDistance = {0, 0};
    m_moveStart = m_moveEnd = 0;
    m_type = 0;
    m_grabbed = -1;
    m_diffsX = m_diffsY = 0;
    m_axis = 0;
    std::fill(std::begin(m_moveValues), std::end(m_moveValues), 0.0);
    m_helperX = m_helperY = 0;
    m_hasHelperLine = m_movingHelperLine = false;
    m_lineToMoveStart = m_lineToMoveEnd = {0, 0};
    m_lineToMoveVisibility[0] = m_lineToMoveVisibility[1] = false;
    m_lastVideoTime = m_lineStartTime = -1;
    m_inGesture = m_cancelled = m_keyNudge = false;
    m_skipReset = m_committing = false;
    (void)host;
}

void MoveTool::reset(VisualHost &host)
{
    if (m_inGesture && !host.gesture()) {
        m_cancelled = true;
        m_keyNudge = false;
    }
    m_inGesture = host.gesture() != nullptr && m_inGesture;
    // Legacy's commit did not set the tool again (EditBox::Send and the
    // batch's SetModified pass visualdummy/dummy: no ShowEditOnVideo,
    // SubsGridBase.cpp:154, Visuals.cpp:792-797), so the refresh that follows
    // this tool's own commit leaves its state as the drag left it.
    if (m_committing)
        return;
    if (m_skipReset && host.session() && host.session()->revision() == m_committedRevision &&
        host.activeLine() == m_committedActive) {
        m_skipReset = false;
        return;
    }
    m_skipReset = false;
    setCurVisual(host);
}

void MoveTool::commit(VisualHost &host)
{
    m_inGesture = false;
    m_keyNudge = false;
    m_skipReset = false;
    m_committing = true;
    const bool done = host.commitGesture().has_value();
    m_committing = false;
    if (done && host.session()) {
        m_skipReset = true;
        m_committedRevision = host.session()->revision();
        m_committedActive = host.activeLine();
    }
}

void MoveTool::setCurVisual(VisualHost &host)
{
    // Move::SetCurVisual (VisualMove.cpp:355-371) on the Line editor's text:
    // the staged text while a nudge's gesture is open (legacy had committed
    // and read it back).
    const ScriptState state = scriptState(host);
    if (!state.active)
        return;
    core::LineRecord edit = *state.active;
    if (const Gesture *g = host.gesture(); g && std::ranges::count(g->targets(), edit.id))
        edit = stagedRecord(*g, edit.id);
    const PointF linepos = posnScale(state, Family::Move, edit, lineText(edit), nullptr, m_moveValues);
    const VideoView &view = host.view();
    m_from = m_to = view.scriptToView(linepos);
    if (m_moveValues[6] > 3)
        m_to = view.scriptToView({static_cast<float>(m_moveValues[2]), static_cast<float>(m_moveValues[3])});
    m_moveDistance = sub(m_to, m_from);
    int startIter = 4, endIter = 5;
    if (m_moveValues[4] > m_moveValues[5]) {
        startIter = 5;
        endIter = 4;
    }
    m_moveStart = static_cast<int>(m_moveValues[startIter]);
    m_moveEnd = static_cast<int>(m_moveValues[endIter]);
    host.toolChanged();
}

std::u16string MoveTool::changeVisualBatch(const std::u16string &text, const core::LineRecord &line,
                                           const ScriptState &state, const core::LineRecord &editLine,
                                           const VisualHost &host) const
{
    // Move::ChangeVisual(txt, dial, n) (VisualMove.cpp:373-403): each Line's
    // own start moved by the active Line's drag, its end by the same \move
    // distance, its own times when its \move has six values, else the active
    // Line's (GetMoveTimes).
    u16 txt = text;
    const LinePosition lp = linePosition(state, line, false);
    const PointF moveFrom = sub(m_lastFrom, m_from);
    const PointF moveTo = sub(m_lastTo, m_to);
    int moveStartTime = 0, moveEndTime = 0;
    u16 tagBefore;
    if (!lp.putInBracket && lp.textStart < txt.size())
        tagBefore = txt.substr(lp.textStart, lp.textLength);
    std::vector<u16> values;
    {
        std::size_t pos = 0;
        while (pos < tagBefore.size()) {
            const std::size_t next = tagBefore.find(u',', pos);
            const u16 token = tagBefore.substr(pos, next == u16::npos ? u16::npos : next - pos);
            if (!token.empty())
                values.push_back(token);
            if (next == u16::npos)
                break;
            pos = next + 1;
        }
    }
    if (lp.putInBracket || values.size() < 6) {
        const auto times = moveTimes(state, editLine);
        moveStartTime = times.first;
        moveEndTime = times.second;
    } else {
        u16 t2 = values[5];
        const u16 &t1 = values[4];
        std::erase(t2, u')');
        moveStartTime = atoi16(t1); // wxAtoi
        moveEndTime = atoi16(t2);
    }
    const VideoView &view = host.view();
    const float coeffW = view.coeffW(), coeffH = view.coeffH();
    const PointF zoomScale = view.zoomScale();
    const u16 tag = u"\\move(" + ft(lp.pos.x - (moveFrom.x / zoomScale.x) * coeffW) + u"," +
                    ft(lp.pos.y - (moveFrom.y / zoomScale.y) * coeffH) + u"," +
                    ft(lp.pos.x + ((m_moveDistance.x - moveTo.x) / zoomScale.x) * coeffW) + u"," +
                    ft(lp.pos.y + ((m_moveDistance.y - moveTo.y) / zoomScale.y) * coeffH) + u"," +
                    number(moveStartTime) + u"," + number(moveEndTime) + u")";
    const long x = static_cast<long>(lp.textStart);
    const long y = static_cast<long>(lp.textLength) + x - 1;
    (void)changeText(txt, tag, !lp.putInBracket, x, y);
    return txt;
}

void MoveTool::setVisual(bool dummy, VisualHost &host)
{
    // Visuals::SetVisual(dummy) (Visuals.cpp:725-832): with several Lines
    // (EditBox::IsCursorOnStart: more than one selected; here picked) each
    // Line's own text, else the Line editor's text of the one target.
    if (!host.gesture()) {
        auto begun = host.beginGesture(host.batchTargets(), std::string(familyInfo(Family::Move).history));
        m_inGesture = begun.has_value();
        m_skipReset = false;
        if (!m_inGesture)
            return;
    } else if (!m_inGesture) {
        return;
    }
    Gesture *gesture = host.gesture();
    const ScriptState state = scriptState(host);
    const auto &targets = gesture->targets();
    if (targets.size() > 1) {
        const core::LineRecord editLine = state.active ? *state.active : gesture->before(targets.front());
        for (const auto id : targets) {
            const core::LineRecord &before = gesture->before(id);
            const u16 txt = changeVisualBatch(lineText(before), before, state, editLine, host);
            gesture->stage(id, core::toUtf8(txt), editsTranslation(before));
        }
    } else if (!targets.empty() && !dummy) {
        // The editor path's commit sends the Line editor's text as the last
        // preview left it (EditBox::Send after SetModified, Visuals.cpp:821-
        // 830): unchanged when nothing was shown, still one step.
        const core::LineRecord &before = gesture->before(targets.front());
        if (!gesture->staged(before.id, editsTranslation(before)))
            gesture->stage(before.id, editsTranslation(before) ? before.translation : before.text,
                           editsTranslation(before));
    } else if (!targets.empty()) {
        const core::LineRecord &before = gesture->before(targets.front());
        const core::LineRecord editLine =
            (state.active && state.active->id == before.id) ? before : (state.active ? *state.active : before);
        u16 txt = lineText(before);
        core::legacy::TagEditor editor({txt, 0, 0});
        (void)editor.findTag(u"(move|pos).+", 1, false);
        const int startTime = zeroit(startMs(editLine));
        const VideoView &view = host.view();
        const PointF from = view.viewToScript(m_from);
        const PointF to = view.viewToScript(m_to);
        const u16 visual = u"\\move(" + ft(from.x) + u"," + ft(from.y) + u"," + ft(to.x) + u"," + ft(to.y) + u"," +
                           ft(static_cast<float>(m_moveValues[4] - startTime)) + u"," +
                           ft(static_cast<float>(m_moveValues[5] - startTime)) + u")";
        const auto [px, py] = editor.position();
        (void)replaceTag(txt, visual, editor.inBracket(), px, py);
        gesture->stage(before.id, core::toUtf8(txt), editsTranslation(before));
    }
    if (!dummy)
        commit(host);
    host.toolChanged();
}

bool MoveTool::setMove(VisualHost &host)
{
    // Move::SetMove (VisualMove.cpp:460-479): the end point extrapolated from
    // the two points over the \move's time. lastVideoTime is kept from the
    // first time it is set, as legacy did.
    const int time = static_cast<int>(host.videoTimeMs());
    if (!m_lineToMoveVisibility[1] || time == m_lineStartTime || m_lineStartTime == -1) {
        if (time == m_lineStartTime)
            host.log(u"Video must be set at least one frame after the line's start time");
        return false;
    }
    if (m_lastVideoTime == -1)
        m_lastVideoTime = time;
    const float t1 = static_cast<float>(m_lastVideoTime - m_moveStart);
    const float t2 = static_cast<float>(m_moveEnd - m_moveStart);
    const float currentTime = t2 / t1;
    const PointF d = sub(m_lineToMoveEnd, m_lineToMoveStart);
    m_to = {m_from.x + (d.x * currentTime), m_from.y + (d.y * currentTime)};
    return true;
}

bool MoveTool::cancelledByHost(VisualHost &host)
{
    if (m_inGesture && !host.gesture()) {
        m_inGesture = false;
        m_cancelled = true;
        m_keyNudge = false;
    }
    return m_cancelled;
}

void MoveTool::pointer(const Pointer &evt, VisualHost &host)
{
    // Move::OnMouseEvent (VisualMove.cpp:144-353).
    if (evt.kind == Pointer::Kind::Press || evt.kind == Pointer::Kind::DoubleClick)
        m_cancelled = false;
    else if (cancelledByHost(host))
        return;
    const bool press = evt.kind == Pointer::Kind::Press;
    const bool leftc = press && evt.button == Pointer::Button::Left;
    const bool rightc = press && evt.button == Pointer::Button::Right;
    const bool click = leftc || rightc;
    const bool holding = (evt.leftDown || evt.rightDown) && evt.kind != Pointer::Kind::Release;
    const bool shift = evt.shift;
    const bool buttonUp = evt.kind == Pointer::Kind::Release;
    const int x = evt.x, y = evt.y;
    const auto near8 = [](PointF p, int px, int py) { return std::abs(p.x - px) < 8 && std::abs(p.y - py) < 8; };

    if (m_hasLineToMove) {
        if (buttonUp) {
            const int time = static_cast<int>(host.videoTimeMs());
            if (!m_lineToMoveVisibility[1] || time == m_lineStartTime || m_lineStartTime == -1) {
                if (m_inGesture && host.gesture()) {
                    m_inGesture = false;
                    host.cancelGesture();
                }
            } else {
                setVisual(false, host);
            }
            m_grabbed = -1;
            m_moveDistance = sub(m_to, m_from);
            m_movingHelperLine = false;
        }
        if (m_movingHelperLine) {
            m_helperX = x;
            m_helperY = y;
            host.toolChanged();
            return;
        }
        if (click) {
            if (isInPos(x, y, m_helperX, m_helperY, 4)) {
                m_movingHelperLine = true;
                return;
            }
            m_type = 0;
            if (!m_lineToMoveVisibility[0]) {
                m_lineToMoveVisibility[0] = true;
                m_lineToMoveStart = {static_cast<float>(x), static_cast<float>(y)};
                // DrawVisual takes the time the first point is drawn at.
                if (m_lineStartTime == -1)
                    m_lineStartTime = static_cast<int>(host.videoTimeMs());
                host.toolChanged();
                return;
            } else if (near8(m_lineToMoveStart, x, y)) {
                m_diffsX = static_cast<int>(m_lineToMoveStart.x - x);
                m_diffsY = static_cast<int>(m_lineToMoveStart.y - y);
                m_grabbed = 0;
            } else if (!m_lineToMoveVisibility[1]) {
                m_lineToMoveVisibility[1] = true;
                m_lineToMoveEnd = {static_cast<float>(x), static_cast<float>(y)};
                if (shift) {
                    if (std::abs(m_lineToMoveStart.x - m_lineToMoveEnd.x) <
                        std::abs(m_lineToMoveStart.y - m_lineToMoveEnd.y))
                        m_lineToMoveEnd.x = m_lineToMoveStart.x;
                    else
                        m_lineToMoveEnd.y = m_lineToMoveStart.y;
                }
                m_type = 1;
            } else if (near8(m_lineToMoveEnd, x, y)) {
                m_diffsX = static_cast<int>(m_lineToMoveEnd.x - x);
                m_diffsY = static_cast<int>(m_lineToMoveEnd.y - y);
                m_grabbed = 1;
                m_type = 1;
            } else {
                return;
            }
            m_lastmove = m_lastTo = m_to;
            m_firstmove = m_lastFrom = m_from;
            if (setMove(host))
                setVisual(true, host);
            else
                host.toolChanged();
        }
        if (holding && m_grabbed != -1) {
            int shiftType = 0;
            if (shift && m_lineToMoveVisibility[1])
                shiftType = (std::abs(m_lineToMoveStart.x - m_lineToMoveEnd.x) <
                             std::abs(m_lineToMoveStart.y - m_lineToMoveEnd.y))
                                ? 1
                                : 2;
            if (m_grabbed == 0) {
                m_lineToMoveStart.x = (shiftType == 1) ? m_lineToMoveEnd.x : static_cast<float>(x + m_diffsX);
                m_lineToMoveStart.y = (shiftType == 2) ? m_lineToMoveEnd.y : static_cast<float>(y + m_diffsY);
            } else {
                m_lineToMoveEnd.x = (shiftType == 1) ? m_lineToMoveStart.x : static_cast<float>(x + m_diffsX);
                m_lineToMoveEnd.y = (shiftType == 2) ? m_lineToMoveStart.y : static_cast<float>(y + m_diffsY);
            }
            if (setMove(host))
                setVisual(true, host);
            else
                host.toolChanged();
        }
        return;
    }

    if (buttonUp) {
        setVisual(false, host);
        m_grabbed = -1;
        m_moveDistance = sub(m_to, m_from);
        m_movingHelperLine = false;
    }
    if (m_movingHelperLine) {
        m_helperX = x;
        m_helperY = y;
        host.toolChanged();
        return;
    }
    if (click) {
        if (isInPos(x, y, m_helperX, m_helperY, 4)) {
            m_movingHelperLine = true;
            return;
        }
        if (leftc)
            m_type = 0;
        if (rightc)
            m_type = 1;
        if (near8(m_from, x, y) && leftc) {
            m_grabbed = 0;
            m_type = 0;
            m_diffsX = static_cast<int>(m_from.x - x);
            m_diffsY = static_cast<int>(m_from.y - y);
        } else if (near8(m_to, x, y)) {
            m_grabbed = 1;
            m_type = 1;
            m_diffsX = static_cast<int>(m_to.x - x);
            m_diffsY = static_cast<int>(m_to.y - y);
        } else if (!shift) {
            m_grabbed = -1;
            if (m_type == 1)
                m_to = {static_cast<float>(x), static_cast<float>(y)};
            else
                m_from = {static_cast<float>(x), static_cast<float>(y)};
            m_diffsX = m_diffsY = 0;
        }
        m_lastmove = m_lastTo = m_to;
        m_firstmove = m_lastFrom = m_from;
        setVisual(true, host);
        m_axis = 0;
    }
    if (holding) {
        if (m_type == 0)
            m_from = {static_cast<float>(x + m_diffsX), static_cast<float>(y + m_diffsY)};
        else
            m_to = {static_cast<float>(x + m_diffsX), static_cast<float>(y + m_diffsY)};
        if (shift) {
            const int diffx = static_cast<int>(std::abs((m_type == 0) ? m_firstmove.x - x : m_lastmove.x - x));
            const int diffy = static_cast<int>(std::abs((m_type == 0) ? m_firstmove.y - y : m_lastmove.y - y));
            if (diffx != diffy)
                m_axis = diffx > diffy ? 2 : 1;
            if (m_type == 0) {
                if (m_axis == 1)
                    m_from.x = m_firstmove.x;
                if (m_axis == 2)
                    m_from.y = m_firstmove.y;
            } else {
                if (m_axis == 1)
                    m_to.x = m_lastmove.x;
                if (m_axis == 2)
                    m_to.y = m_lastmove.y;
            }
        }
        setVisual(true, host);
    }
    if (press && evt.button == Pointer::Button::Middle) {
        m_hasHelperLine = (m_hasHelperLine && isInPos(x, y, m_helperX, m_helperY, 4)) ? false : true;
        m_helperX = x;
        m_helperY = y;
    }
    host.toolChanged();
}

bool MoveTool::key(const Key &evt, VisualHost &host)
{
    // Move::OnKeyPress (VisualMove.cpp:421-458): A/D/W/S move the start,
    // J/L/I/K the end, one script pixel unzoomed (a tenth with Shift). Legacy
    // committed each press and read the Line back; here the presses while the
    // key is held make one gesture committed on its release (#55).
    const bool left = evt.key == 'A', right = evt.key == 'D', up = evt.key == 'W', down = evt.key == 'S';
    const bool sleft = evt.key == 'J', sright = evt.key == 'L', sup = evt.key == 'I', sdown = evt.key == 'K';
    const bool secondShortcut = sleft || sright || sup || sdown;
    if (!(left || right || up || down || secondShortcut))
        return false;
    if (evt.release) {
        if (evt.autoRepeat || !m_keyNudge)
            return m_keyNudge;
        if (host.gesture() && m_inGesture)
            commit(host);
        m_keyNudge = false;
        return true;
    }
    if (altAlone(evt))
        return false;
    float directionX = (left || sleft) ? -1.f : (right || sright) ? 1.f : 0.f;
    float directionY = (up || sup) ? -1.f : (down || sdown) ? 1.f : 0.f;
    if (evt.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    directionX = (directionX / host.view().coeffW());
    directionY = (directionY / host.view().coeffH());
    if (secondShortcut) {
        m_to.x += directionX;
        m_to.y += directionY;
    } else {
        m_from.x += directionX;
        m_from.y += directionY;
    }
    // Legacy staged and committed on each press (SetVisual(true), then
    // SetVisual(false)); the commit is the key's release here (#55).
    setVisual(true, host);
    if (host.gesture())
        m_keyNudge = true;
    return true;
}

Overlay MoveTool::overlay(const VisualHost &host) const
{
    // Move::DrawVisual (VisualMove.cpp:36-93); the cross along the \move
    // stays at its start when the \move has no duration (DrawWx's guard,
    // VisualMove.cpp:128; the Direct3D path divided by zero).
    Overlay out;
    const int time = static_cast<int>(host.videoTimeMs());
    if (m_hasLineToMove && m_lineToMoveVisibility[0]) {
        drawRect(out, m_lineToMoveStart);
        if (m_lineToMoveVisibility[1]) {
            const PointF end = drawArrow(out, m_lineToMoveStart, m_lineToMoveEnd, 6);
            out.lines.push_back({m_lineToMoveStart, end, 2, kHandleBorder});
            drawCircle(out, m_lineToMoveEnd);
        }
    }
    if (m_hasHelperLine)
        drawHelperLine(out, m_helperX, m_helperY, host.view());
    const PointF end = drawArrow(out, m_from, m_to, 6);
    const float tmpt = static_cast<float>(time - m_moveStart);
    const float tmpt1 = static_cast<float>(m_moveEnd - m_moveStart);
    const float actime = tmpt1 == 0 ? 0 : tmpt / tmpt1;
    PointF dist;
    if (time < m_moveStart)
        dist = m_from;
    else if (time > m_moveEnd)
        dist = m_to;
    else
        dist = {m_from.x - ((m_from.x - m_to.x) * actime), m_from.y - ((m_from.y - m_to.y) * actime)};
    out.lines.push_back({m_from, end, 2, kHandleBorder});
    drawCross(out, dist, kHandleBorder);
    drawRect(out, m_from);
    drawCircle(out, m_to);
    return out;
}

std::vector<ToolValue> MoveTool::values(const VisualHost &host) const
{
    // The numeric alternative: the \move's points in script coordinates and
    // its times from the Line's start, as the tool writes them.
    const PointF from = host.view().viewToScript(m_from);
    const PointF to = host.view().viewToScript(m_to);
    const auto *session = host.session();
    int startTime = 0;
    if (session && host.activeLine())
        for (const auto *l : session->document().lines())
            if (l->id == *host.activeLine())
                startTime = zeroit(startMs(*l));
    return {{"x1", u"X1", ft(from.x), true},
            {"y1", u"Y1", ft(from.y), true},
            {"x2", u"X2", ft(to.x), true},
            {"y2", u"Y2", ft(to.y), true},
            {"t1", u"T1", ft(static_cast<float>(m_moveValues[4] - startTime)), false},
            {"t2", u"T2", ft(static_cast<float>(m_moveValues[5] - startTime)), false}};
}

bool MoveTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host)
{
    // A typed point written as a key's nudge writes it (SetVisual).
    double value = 0;
    if ((name != "x1" && name != "y1" && name != "x2" && name != "y2") || !parseNumber(text, value) ||
        host.gesture() || !host.activeLine())
        return false;
    const bool start = name[1] == '1';
    PointF script = host.view().viewToScript(start ? m_from : m_to);
    (name[0] == 'x' ? script.x : script.y) = static_cast<float>(value);
    (start ? m_from : m_to) = host.view().scriptToView(script);
    const std::uint64_t before = host.session() ? host.session()->revision() : 0;
    setVisual(true, host);
    setVisual(false, host);
    return host.session() && host.session()->revision() != before;
}

std::vector<ToolOption> MoveTool::options(const VisualHost &host) const
{
    // MoveItem (VideoToolbar.cpp:1131-1197; help text 88).
    (void)host;
    ToolOption two;
    two.name = "twoPoints";
    two.iconRole = std::string(kTwoPoints);
    two.tooltip = u"Set movement using 2 points and the video position.\nAfter placing the first point before the "
                  u"moving text,\nadvance the video by enough frames\nso that the same video element\nfor which the "
                  u"start point was set remains visible.\nMovement is generated by placing the second point.";
    two.checked = m_hasLineToMove;
    return {two};
}

bool MoveTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // MoveItem::OnMouseEvent then Move::ChangeTool (VisualMove.cpp:481-484):
    // only whether the two points are used; the points stay.
    if (name != "twoPoints" || host.gesture())
        return false;
    m_hasLineToMove = value != 0;
    host.toolChanged();
    return true;
}

} // namespace hikari::application::visual
