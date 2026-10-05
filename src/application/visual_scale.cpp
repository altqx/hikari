#include "hikari/application/visual_scale.h"

#include "hikari/core/text_projection.h"

#include <cmath>

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

using namespace transform;

namespace {

enum { LEFT = 1, RIGHT, TOP = 4, BOTTOM = 8, INSIDE = 16, OUTSIDE = 32 }; // VisualScale.cpp:28-35

std::u8string u8(const std::u16string &text)
{
    return core::toUtf8(text);
}

// config.h's MID(a, b, c): MAX(a, MIN(b, c)).
int mid(int a, int b, int c)
{
    const int m = b < c ? b : c;
    return a > m ? a : m;
}

// The toolbar item's icons in order (VideoToolbar.cpp:83-89).
struct ItemInfo {
    const char *name;
    const char *icon;
    const char16_t *tooltip;
};
const std::array<ItemInfo, 7> &items()
{
    static const std::array<ItemInfo, 7> table{{
        {"rectangle", "frame-to-scale",
         u"Set scale by rectangle.\nAfter drawing rectangle text is scaled\nin two or one axis."},
        {"scaleX", "scale-x", u"Scale width"},
        {"aspectRatio", "link", u"Preserve aspect ratio"},
        {"scaleY", "scale-y", u"Scale height"},
        {"originalRectangle", "original-frame",
         u"Set a custom rectangle for the current scale.\nIf automatic size calculation does not work correctly,\n"
         u"you can set a rectangle for the original scale."},
        {"changeAll", "tool-scale-rotation", u"Change all scale tags"},
        {"preserveProportions", "resample",
         u"Preserve proportions so that vector drawings and text\nkeep their original positions after scaling."},
    }};
    return table;
}

} // namespace

int ScaleTool::toggled() const
{
    // ScaleItem::GetItemToggled (VideoToolbar.h:208-214).
    int result = 0;
    for (int i = 0; i < 7; i++)
        if (m_toggle[static_cast<std::size_t>(i)])
            result |= (1 << i);
    return result;
}

void ScaleTool::setToggled(int bits, VisualHost *host)
{
    for (int i = 0; i < 7; i++)
        m_toggle[static_cast<std::size_t>(i)] = (bits & (1 << i)) != 0;
    if (host)
        changeTool(toggled(), false, *host);
}

std::vector<ToolOption> ScaleTool::options(const VisualHost &host) const
{
    (void)host;
    // ScaleItem::OnMouseEvent's greying (VideoToolbar.cpp:906-913): the
    // rectangle's four options need the rectangle, the axes stay while the
    // aspect ratio links them, and "change all" stays on with the rectangle
    // or "preserve proportions".
    std::vector<ToolOption> out;
    const auto &t = m_toggle;
    for (int elem = 0; elem < 7; ++elem) {
        const bool grayed = ((elem > 0 && elem < 5) && !t[0]) || ((elem == 1 || elem == 3) && t[2]) ||
                            (elem == 5 && (t[6] || t[0]));
        const ItemInfo &info = items()[static_cast<std::size_t>(elem)];
        ToolOption o;
        o.name = info.name;
        o.iconRole = info.icon;
        o.tooltip = info.tooltip;
        o.checked = t[static_cast<std::size_t>(elem)];
        o.enabled = !grayed;
        out.push_back(std::move(o));
    }
    return out;
}

bool ScaleTool::setOption(const std::string &name, int value, VisualHost &host)
{
    // ScaleItem::OnMouseEvent (VideoToolbar.cpp:900-958): a click toggles
    // the icon unless it is greyed, with the item's own links, then the tool
    // takes GetItemToggled (ID_MOVE_TOOLBAR_EVENT, VideoBox.cpp:179-181).
    int elem = -1;
    for (int i = 0; i < 7; ++i)
        if (name == items()[static_cast<std::size_t>(i)].name)
            elem = i;
    if (elem < 0)
        return false;
    auto &t = m_toggle;
    const bool grayed = ((elem > 0 && elem < 5) && !t[0]) || ((elem == 1 || elem == 3) && t[2]) ||
                        (elem == 5 && (t[6] || t[0]));
    const auto e = static_cast<std::size_t>(elem);
    if (grayed || t[e] == (value != 0))
        return false;
    t[e] = !t[e];
    if (elem == 1 || elem == 3) {
        if (!t[1] && !t[3])
            t[elem == 1 ? 3 : 1] = true;
    }
    if (elem == 2 && t[e])
        t[1] = true;
    if ((elem == 6 && t[e]) || (elem == 0 && t[e]))
        t[5] = true;
    changeTool(toggled(), false, host);
    return true;
}

void ScaleTool::selected(VisualHost &host)
{
    // Visuals::Get made a new Scale (VisualScale.cpp:37-42, Visuals.h:384-402):
    // the tool's state starts over, the toolbar's toggles stay.
    (void)host;
    const auto keep = m_toggle;
    *this = ScaleTool();
    m_toggle = keep;
}

void ScaleTool::takeView(const VideoView &view)
{
    m_coeffW = view.coeffW();
    m_coeffH = view.coeffH();
    m_viewZoomMove = view.zoomMove();
    m_viewZoomScale = view.zoomScale();
}

void ScaleTool::reset(VisualHost &host)
{
    takeView(host.view());
    // Visuals::SetVisual(dial, tool) (Visuals.cpp:256-283): the editor's text
    // kept for the dummy edits, ChangeTool(tool, true), SetCurVisual.
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

void ScaleTool::changeTool(int tool, bool blockSetCurVisual, VisualHost &host)
{
    // Scale::ChangeTool (VisualScale.cpp:393-423).
    m_hasScaleToRectangle = tool & 1;
    m_hasScaleX = tool & 2;
    m_preserveAspectRatio = tool & 4;
    m_hasScaleY = tool & 8;
    const bool oldHasOriginalRectangle = m_hasOriginalRectangle;
    m_hasOriginalRectangle = tool & 16;
    const bool oldChangeAllTags = m_changeAllTags;
    m_changeAllTags = tool & 32;
    m_preserveProportions = tool & 64;
    m_replaceTagsInCursorPosition = !m_changeAllTags;
    if (oldChangeAllTags != m_changeAllTags && !blockSetCurVisual) {
        setCurVisual(host);
    } else if (m_hasScaleToRectangle) {
        const Context ctx = context(host);
        if (ctx.active) {
            const TextSize ts = textSize(ctx, *ctx.active, nullptr, false);
            m_originalSize = ts.size;
            m_border = ts.border;
        }
    }
    if (oldHasOriginalRectangle != m_hasOriginalRectangle || m_hasOriginalRectangle) {
        if (m_hasOriginalRectangle) {
            if (oldChangeAllTags == m_changeAllTags)
                setCurVisual(host);
            setSecondRectScale(host.view());
        } else {
            m_originalRectangleVisible = false;
        }
    }
    host.toolChanged();
}

void ScaleTool::setCurVisual(VisualHost &host)
{
    // Scale::SetCurVisual (VisualScale.cpp:373-391).
    const Context ctx = context(host);
    const VideoView &view = host.view();
    PointF linepos = posnScale(ctx, m_find, !m_replaceTagsInCursorPosition, &m_scale, &m_an, m_moveValues);
    m_originalScale = m_lastScale = m_scale;
    if (m_moveValues[6] > 3)
        linepos = calcMovePos(ctx, m_moveValues, m_start, m_end);
    const float coeffW = view.coeffW(), coeffH = view.coeffH();
    const PointF zm = view.zoomMove(), zs = view.zoomScale();
    m_from = {((linepos.x / coeffW) - zm.x) * zs.x, ((linepos.y / coeffH) - zm.y) * zs.y};
    m_arrowLengths.y = (linepos.y > ctx.height / 2) ? -100.f : 100.f;
    m_arrowLengths.x = (linepos.x > ctx.width / 2) ? -100.f : 100.f;
    m_to.x = m_from.x + (m_scale.x * m_arrowLengths.x);
    m_to.y = m_from.y + (m_scale.y * m_arrowLengths.y);
    if (m_hasScaleToRectangle && ctx.active) {
        const TextSize ts = textSize(ctx, *ctx.active, nullptr, false);
        m_originalSize = ts.size;
        m_border = ts.border;
    }
}

bool ScaleTool::beginEdit(VisualHost &host)
{
    // The gesture's targets are fixed when it begins (edit-transactions.md);
    // legacy's SetVisual read the selection on every sample.
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    auto g = host.beginGesture(targets, std::string(familyInfo(Family::Scale).history));
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

bool ScaleTool::finishEdit(VisualHost &host)
{
    // A refused commit (a stale revision, a draft that cannot commit) wrote
    // nothing: the tool reads the unchanged text again, as after Esc.
    const bool committed = !host.gesture() || host.commitGesture().has_value();
    m_editing.reset();
    if (!committed)
        reset(host);
    return committed;
}

void ScaleTool::setVisual(bool dummy, VisualHost &host)
{
    // Visuals::SetVisual(dummy) (Visuals.cpp:726-832). Several Lines: each
    // target's text through ChangeVisual(txt, dial, n), previewed while
    // dummy and recomputed for the commit. One Line: the editor's text
    // through ChangeVisual(txt), the caret put at the tag; the release
    // commits what the editor holds.
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

void ScaleTool::changeVisualLine(std::u16string &text, const core::LineRecord &dial, const Context &ctx)
{
    // Scale::ChangeVisual(txt, dial, numOfSelections) (VisualScale.cpp:425-516).
    float Scalex = m_scale.x / m_lastScale.x;
    float Scaley = m_scale.y / m_lastScale.y;
    if (Scalex > Scaley)
        Scaley = Scalex;
    else if (Scaley > Scalex)
        Scalex = Scaley;
    if (m_changeAllTags && m_preserveProportions) {
        const LinePosition lp = linePosition(ctx, dial);
        PointF pos = lp.pos;
        const PointF zm = m_viewZoomMove, zs = m_viewZoomScale;
        const PointF activeLinePos{((m_from.x / zs.x) + zm.x) * m_coeffW, ((m_from.y / zs.y) + zm.y) * m_coeffH};
        pos.x = activeLinePos.x + ((pos.x - activeLinePos.x) * Scalex);
        pos.y = activeLinePos.y + ((pos.y - activeLinePos.y) * Scaley);
        std::u16string posstr = u"\\pos(" + getfloat(pos.x) + u"," + getfloat(pos.y) + u")";
        if (m_moveValues[6] > 2 && ctx.active)
            posstr = moveText(pos, m_moveValues, startMs(*ctx.active));
        if (lp.putInBracket)
            posstr = u"{" + posstr + u"}";
        const auto at = std::min<std::size_t>(static_cast<std::size_t>(lp.textX), text.size());
        text.replace(at, static_cast<std::size_t>(lp.textY), posstr);
        changeClipScale(m_find, text, activeLinePos, Scalex, Scaley);
        m_type = 2;
    }
    std::optional<core::StyleValues> style;
    if (m_changeAllTags)
        style = lineStyle(ctx, dial.style);
    if (m_type != 1) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fscx([0-9.-]+)", u"fscx", text,
                              [&](const FindData &data, std::u16string &result) {
                                  float scalex = 1.f * Scalex;
                                  if (!data.finding.empty())
                                      scalex = static_cast<float>((atof(data.finding) / 100.f) * Scalex);
                                  else if (style)
                                      scalex = static_cast<float>((styleNumber(style->scaleX, 100.) / 100.f) * Scalex);
                                  result = getfloat(scalex * 100);
                              },
                              true);
        } else {
            const std::u16string tag = u"\\fscx" + getfloat(m_scale.x * 100);
            m_find.findTag(u"fscx([0-9.-]+)", text, 1);
            m_find.replace(tag, text);
        }
    }
    if (m_type != 0) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fscy([0-9.-]+)", u"fscy", text,
                              [&](const FindData &data, std::u16string &result) {
                                  float scaley = 1.f * Scalex;
                                  if (!data.finding.empty())
                                      scaley = static_cast<float>((atof(data.finding) / 100.f) * Scaley);
                                  else if (style)
                                      scaley = static_cast<float>((styleNumber(style->scaleY, 100.) / 100.f) * Scaley);
                                  result = getfloat(scaley * 100);
                              },
                              true);
        } else {
            const std::u16string tag = u"\\fscy" + getfloat(m_scale.y * 100);
            m_find.findTag(u"fscy([0-9.-]+)", text, 1);
            m_find.replace(tag, text);
        }
    }
}

std::pair<long, long> ScaleTool::changeVisualEditor(std::u16string &text)
{
    // Scale::ChangeVisual(txt) (VisualScale.cpp:518-562).
    if (m_type != 1) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fscx([0-9.-]+)", u"fscx", text,
                              [&](const FindData &data, std::u16string &result) {
                                  float scalex = m_scale.x;
                                  if (!data.finding.empty())
                                      scalex = static_cast<float>((atof(data.finding) / 100.f) *
                                                                  (m_scale.x / m_lastScale.x));
                                  result = getfloat(scalex * 100);
                              },
                              true);
        } else {
            const std::u16string tag = u"\\fscx" + getfloat(m_scale.x * 100);
            m_find.findTag(u"fscx([0-9.-]+)", text);
            m_find.replace(tag, text);
        }
    }
    if (m_type != 0) {
        if (m_changeAllTags) {
            m_find.replaceAll(u"fscy([0-9.-]+)", u"fscy", text,
                              [&](const FindData &data, std::u16string &result) {
                                  float scaley = m_scale.y;
                                  if (!data.finding.empty())
                                      scaley = static_cast<float>((atof(data.finding) / 100.f) *
                                                                  (m_scale.y / m_lastScale.y));
                                  result = getfloat(scaley * 100);
                              },
                              true);
        } else {
            const std::u16string tag = u"\\fscy" + getfloat(m_scale.y * 100);
            m_find.findTag(u"fscy([0-9.-]+)", text);
            m_find.replace(tag, text);
        }
    }
    return m_find.positionInText();
}

void ScaleTool::pointer(const Pointer &event, VisualHost &host)
{
    // Scale::OnMouseEvent (VisualScale.cpp:116-371). The cursor shapes are
    // the overlay's (not drawn here).
    if (event.kind == Pointer::Kind::Wheel)
        return;
    const VideoView &view = host.view();
    takeView(view);
    const PointF zm = m_viewZoomMove, zs = m_viewZoomScale;
    const float coeffW = m_coeffW, coeffH = m_coeffH;
    const Mouse e = mouse(event);
    int x = e.x, y = e.y;
    if (e.click)
        (void)beginEdit(host);

    if (m_hasScaleToRectangle) {
        m_type = ((m_hasScaleX && m_hasScaleY) || m_preserveAspectRatio) ? 2 : m_hasScaleY ? 1 : 0;
        if (e.buttonUp) {
            if (m_rectangleVisible) {
                if (m_sizingRectangle[1].y == m_sizingRectangle[0].y ||
                    m_sizingRectangle[1].x == m_sizingRectangle[0].x)
                    m_rectangleVisible = false;
                if (m_originalRectangleVisible) {
                    if (m_sizingRectangle[3].y == m_sizingRectangle[2].y ||
                        m_sizingRectangle[3].x == m_sizingRectangle[2].x)
                        m_originalRectangleVisible = false;
                }
                sortPoints();
            }
            if (m_rectangleVisible) {
                setScale();
                setVisual(false, host);
            } else {
                (void)finishEdit(host);
            }
        }
        if (e.click) {
            m_rightHolding = e.rightc && m_hasOriginalRectangle;
            m_grabbed = OUTSIDE;
            const float pointx = ((x / zs.x) + zm.x) * coeffW, pointy = ((y / zs.y) + zm.y) * coeffH;
            if (m_rectangleVisible) {
                m_grabbed = hitTest({static_cast<float>(x), static_cast<float>(y)}, false, true);
                if (m_grabbed == INSIDE) {
                    if (m_sizingRectangle[0].x <= pointx && m_sizingRectangle[1].x >= pointx &&
                        m_sizingRectangle[0].y <= pointy && m_sizingRectangle[1].y >= pointy) {
                        m_diffs.x = static_cast<float>(x);
                        m_diffs.y = static_cast<float>(y);
                    }
                } else if (m_grabbed < INSIDE) {
                    m_rightHolding = false;
                }
            }
            if (m_originalRectangleVisible) {
                const int grabbed1 = hitTest({static_cast<float>(x), static_cast<float>(y)}, true, true);
                if (grabbed1 == INSIDE) {
                    if (m_sizingRectangle[2].x <= pointx && m_sizingRectangle[3].x >= pointx &&
                        m_sizingRectangle[2].y <= pointy && m_sizingRectangle[3].y >= pointy) {
                        m_diffs.x = static_cast<float>(x);
                        m_diffs.y = static_cast<float>(y);
                    }
                    m_grabbed = grabbed1;
                } else if (grabbed1 < INSIDE) {
                    m_grabbed = grabbed1;
                    m_rightHolding = true;
                }
            }
            const std::size_t tablediff = m_rightHolding ? 2 : 0;
            const bool visible = m_rightHolding ? m_originalRectangleVisible : m_rectangleVisible;
            if (!visible || m_grabbed == OUTSIDE) {
                m_sizingRectangle[0 + tablediff].x = m_sizingRectangle[1 + tablediff].x = pointx;
                m_sizingRectangle[0 + tablediff].y = m_sizingRectangle[1 + tablediff].y = pointy;
                m_grabbed = OUTSIDE;
                if (m_rightHolding)
                    m_originalRectangleVisible = true;
                else
                    m_rectangleVisible = true;
            }
            m_lastScale = m_scale;
        } else if (e.holding && m_grabbed != -1) {
            const std::size_t tablediff = m_rightHolding ? 2 : 0;
            const IntRect videoSize = view.videoRect();
            if (m_grabbed < INSIDE) {
                if (m_grabbed & LEFT || m_grabbed & RIGHT) {
                    x = mid(videoSize.left, x, videoSize.right);
                    const std::size_t posInTable = (m_grabbed & RIGHT) ? 1 : 0;
                    m_sizingRectangle[posInTable + tablediff].x = ((((x + m_diffs.x) / zs.x) + zm.x) * coeffW);
                    if (m_grabbed & LEFT && m_sizingRectangle[0 + tablediff].x > m_sizingRectangle[1 + tablediff].x)
                        m_sizingRectangle[0 + tablediff].x = m_sizingRectangle[1 + tablediff].x;
                    if (m_grabbed & RIGHT && m_sizingRectangle[1 + tablediff].x < m_sizingRectangle[0 + tablediff].x)
                        m_sizingRectangle[1 + tablediff].x = m_sizingRectangle[0 + tablediff].x;
                }
                if (m_grabbed & TOP || m_grabbed & BOTTOM) {
                    y = mid(videoSize.top, y, videoSize.bottom);
                    const std::size_t posInTable = (m_grabbed & BOTTOM) ? 1 : 0;
                    m_sizingRectangle[posInTable + tablediff].y = ((((y + m_diffs.y) / zs.y) + zm.y) * coeffH);
                    if (m_grabbed & TOP && m_sizingRectangle[0 + tablediff].y > m_sizingRectangle[1 + tablediff].y)
                        m_sizingRectangle[0 + tablediff].y = m_sizingRectangle[1 + tablediff].y;
                    if (m_grabbed & BOTTOM && m_sizingRectangle[1 + tablediff].y < m_sizingRectangle[0 + tablediff].y)
                        m_sizingRectangle[1 + tablediff].y = m_sizingRectangle[0 + tablediff].y;
                }
            } else if (m_grabbed == INSIDE) {
                const float movex = (((x - m_diffs.x) / zs.x) * coeffW), movey = (((y - m_diffs.y) / zs.y) * coeffH);
                m_sizingRectangle[0 + tablediff].x += movex;
                m_sizingRectangle[0 + tablediff].y += movey;
                m_sizingRectangle[1 + tablediff].x += movex;
                m_sizingRectangle[1 + tablediff].y += movey;
                m_diffs.x = static_cast<float>(x);
                m_diffs.y = static_cast<float>(y);
            } else if (m_grabbed == OUTSIDE) {
                const float pointx = ((x / zs.x) + zm.x) * coeffW, pointy = ((y / zs.y) + zm.y) * coeffH;
                m_sizingRectangle[1 + tablediff].x = pointx;
                m_sizingRectangle[1 + tablediff].y = pointy;
            }
            setScale();
            if (m_rectangleVisible)
                setVisual(true, host);
            else
                host.toolChanged();
        }
        return;
    }

    if (e.buttonUp) {
        setVisual(false, host);
        m_wasUsedShift = false;
    }
    if (e.click || m_wasUsedShift != e.shift) {
        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
        if (e.leftc)
            m_type = 0;
        if (e.rightc)
            m_type = 1;
        if (e.middlec || (e.leftc && e.shift) || m_preserveProportions)
            m_type = 2;
        if (std::abs(m_to.x - fx) < 11 && std::abs(m_from.y - fy) < 11) {
            m_grabbed = 0;
            m_type = 0;
        } else if (std::abs(m_to.y - fy) < 11 && std::abs(m_from.x - fx) < 11) {
            m_grabbed = 1;
            m_type = 1;
        } else if (std::abs(m_to.x - fx) < 11 && std::abs(m_to.y - fy) < 11) {
            m_grabbed = 2;
            m_type = 2;
        }
        m_diffs.x = m_to.x - fx;
        m_diffs.y = m_to.y - fy;
        if (((e.leftc || e.leftIsDown) && e.shift) || m_preserveProportions) {
            m_type = 2;
            m_diffs.x = fx;
            m_diffs.y = fy;
            m_wasUsedShift = e.shift;
            return;
        }
        if (m_grabbed == -1) {
            m_diffs.x = (m_from.x - fx) + (m_arrowLengths.x * m_scale.x);
            m_diffs.y = (m_from.y - fy) + (m_arrowLengths.y * m_scale.y);
        }
        m_wasUsedShift = e.shift;
        m_lastScale = m_scale;
    } else if (e.holding) {
        if (e.shift || m_preserveProportions) {
            const int diffx = static_cast<int>(std::abs(x - m_diffs.x));
            const int diffy = static_cast<int>(std::abs(m_diffs.y - y));
            const int move = static_cast<int>((diffx > diffy) ? x - m_diffs.x : m_diffs.y - y);
            const PointF copyto = m_to;
            const PointF copydiffs = m_diffs;
            const bool normalArrowX = m_arrowLengths.x > 0;
            const bool normalArrowY = m_arrowLengths.y > 0;
            if (((!normalArrowX && !normalArrowY) || (normalArrowX && normalArrowY)) && diffy < diffx) {
                m_to.y = m_to.y + move;
                m_to.x = m_to.x + move;
            } else if ((normalArrowX && !normalArrowY) || (!normalArrowX && normalArrowY)) {
                m_to.y = m_to.y - move;
                m_to.x = m_to.x + move;
            } else {
                m_to.y = m_to.y - move;
                m_to.x = m_to.x - move;
            }
            m_diffs.x = static_cast<float>(x);
            m_diffs.y = static_cast<float>(y);
            if ((normalArrowX && (m_to.x - m_from.x) < 1) || (!normalArrowX && (m_to.x - m_from.x) > -1)) {
                m_diffs = copydiffs;
                m_to = copyto;
            }
        } else {
            if (m_type != 1)
                m_to.x = x + m_diffs.x;
            if (m_type != 0)
                m_to.y = y + m_diffs.y;
        }
        m_scale.x = std::abs((m_to.x - m_from.x) / m_arrowLengths.x);
        m_scale.y = std::abs((m_to.y - m_from.y) / m_arrowLengths.y);
        setVisual(true, host);
    }
}

bool ScaleTool::key(const Key &event, VisualHost &host)
{
    // Scale::OnKeyPress (VisualScale.cpp:564-607): W/A/S/D move the arrows'
    // end by one percent (Shift: a tenth) unless Alt alone is held; each
    // press is its own edit.
    if (event.release)
        return false;
    const bool left = event.key == 'A';
    const bool right = event.key == 'D';
    const bool up = event.key == 'W';
    const bool down = event.key == 'S';
    const bool altOnly = event.alt && !event.control && !event.shift;
    if (!((left || right || up || down) && !altOnly))
        return false;
    if (!beginEdit(host))
        return true;
    const float unitx = std::abs(m_arrowLengths.x / 100);
    const float unity = std::abs(m_arrowLengths.y / 100);
    float directionX = (left) ? -unitx : (right) ? unitx : 0;
    float directionY = (up) ? -unity : (down) ? unity : 0;
    m_type = (directionX) ? 0 : 1;
    if (event.shift) {
        directionX /= 10.f;
        directionY /= 10.f;
    }
    m_to.x += directionX;
    m_to.y += directionY;
    if (m_changeAllTags && m_preserveProportions) {
        m_lastScale.x = m_scale.x > m_scale.y ? m_scale.x : m_scale.y;
        m_lastScale.y = m_scale.y > m_scale.x ? m_scale.y : m_scale.x;
        m_scale.x = directionX ? std::abs((m_to.x - m_from.x) / m_arrowLengths.x)
                               : std::abs((m_to.y - m_from.y) / m_arrowLengths.y);
        m_scale.y = directionY ? std::abs((m_to.y - m_from.y) / m_arrowLengths.y)
                               : std::abs((m_to.x - m_from.x) / m_arrowLengths.x);
        if (directionX)
            m_to.y += directionX;
        else
            m_to.x += directionY;
    } else {
        m_scale.x = std::abs((m_to.x - m_from.x) / m_arrowLengths.x);
        m_scale.y = std::abs((m_to.y - m_from.y) / m_arrowLengths.y);
    }
    takeView(host.view());
    setVisual(true, host);
    setVisual(false, host);
    return true;
}

int ScaleTool::hitTest(PointF pos, bool originalRect, bool diff)
{
    // Scale::HitTest (VisualScale.cpp:609-650); the previous corner is kept
    // in ints and the vertical inside test reads the x corners, as legacy.
    int resultX = 0, resultY = 0, resultInside = 0, resultFinal = 0, oldpointx = 0, oldpointy = 0;
    const std::size_t tablediff = originalRect ? 2 : 0;
    for (int i = 0; i < 2; i++) {
        const PointF corner = m_sizingRectangle[static_cast<std::size_t>(i) + tablediff];
        const float pointx = ((corner.x / m_coeffW) - m_viewZoomMove.x) * m_viewZoomScale.x;
        const float pointy = ((corner.y / m_coeffH) - m_viewZoomMove.y) * m_viewZoomScale.y;
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

void ScaleTool::sortPoints()
{
    // Scale::SortPoints (VisualScale.cpp:652-676).
    auto &r = m_sizingRectangle;
    if (r[1].y < r[0].y)
        std::swap(r[0].y, r[1].y);
    if (r[1].x < r[0].x)
        std::swap(r[0].x, r[1].x);
    if (m_originalRectangleVisible) {
        if (r[3].y < r[2].y)
            std::swap(r[2].y, r[3].y);
        if (r[3].x < r[2].x)
            std::swap(r[2].x, r[3].x);
    }
}

void ScaleTool::setScale()
{
    // Scale::SetScale (VisualScale.cpp:678-700).
    const auto &r = m_sizingRectangle;
    if (m_originalRectangleVisible) {
        m_scale.x = m_originalScale.x *
                    ((std::fabs(r[1].x - r[0].x) - m_border.x) / (std::fabs(r[3].x - r[2].x) - m_border.x));
        if (m_preserveAspectRatio)
            m_scale.y = m_scale.x;
        else
            m_scale.y = m_originalScale.y *
                        ((std::fabs(r[1].y - r[0].y) - m_border.y) / (std::fabs(r[3].y - r[2].y) - m_border.y));
    } else {
        m_scale.x = m_originalScale.x * ((std::fabs(r[1].x - r[0].x) - m_border.x) / m_originalSize.x);
        if (m_preserveAspectRatio)
            m_scale.y = m_scale.x;
        else
            m_scale.y = m_originalScale.y * ((std::fabs(r[1].y - r[0].y) - m_border.y) / m_originalSize.y);
    }
    if (m_scale.x < 0.f)
        m_scale.x = 0.f;
    if (m_scale.y < 0.f)
        m_scale.y = 0.f;
}

PointF ScaleTool::scaleToVideo(PointF point, const VideoView &view) const
{
    // Scale::ScaleToVideo (VisualScale.cpp:702-707).
    const PointF zm = view.zoomMove(), zs = view.zoomScale();
    return {((point.x / view.coeffW()) - zm.x) * zs.x, ((point.y / view.coeffH()) - zm.y) * zs.y};
}

void ScaleTool::setSecondRectScale(const VideoView &view)
{
    // Scale::SetSecondRectScale (VisualScale.cpp:709-739).
    const PointF zm = view.zoomMove(), zs = view.zoomScale();
    const PointF activeLinePos{((m_from.x / zs.x) + zm.x) * view.coeffW(), ((m_from.y / zs.y) + zm.y) * view.coeffH()};
    const float borderx = m_border.x / 2;
    const float bordery = m_border.y / 2;
    auto &r = m_sizingRectangle;
    r[2].x = activeLinePos.x - borderx - 1;
    r[2].y = activeLinePos.y - bordery - 1;
    r[3].x = m_originalSize.x + activeLinePos.x + borderx - 1;
    r[3].y = m_originalSize.y + activeLinePos.y + bordery - 1;
    if (m_an % 3 == 0) {
        r[2].x -= m_originalSize.x;
        r[3].x -= m_originalSize.x;
    } else if (m_an % 3 == 2) {
        const float halfsizex = m_originalSize.x / 2;
        r[2].x -= halfsizex;
        r[3].x -= halfsizex;
    }
    if (m_an < 4) {
        r[2].y -= m_originalSize.y;
        r[3].y -= m_originalSize.y;
    } else if (m_an < 7) {
        const float halfsizey = m_originalSize.y / 2;
        r[2].y -= halfsizey;
        r[3].y -= halfsizey;
    }
    m_originalRectangleVisible = true;
}

Overlay ScaleTool::overlay(const VisualHost &host) const
{
    // Scale::DrawVisual (VisualScale.cpp:44-114), lines 2 pixels wide
    // (Visuals::Draw).
    Overlay out;
    const VideoView &view = host.view();
    if (m_hasScaleToRectangle) {
        auto rect = [&](PointF a, PointF b, std::uint32_t colour) {
            const PointF p1 = scaleToVideo(a, view), p2 = scaleToVideo(b, view);
            const PointF v[5] = {p1, {p2.x, p1.y}, p2, {p1.x, p2.y}, p1};
            for (int i = 0; i < 4; ++i)
                out.lines.push_back({v[i], v[i + 1], 2, colour});
        };
        if (m_rectangleVisible)
            rect(m_sizingRectangle[0], m_sizingRectangle[1], 0xFFBB0000);
        if (m_originalRectangleVisible)
            rect(m_sizingRectangle[2], m_sizingRectangle[3], 0xFF0000BB);
        return out;
    }
    const int time = static_cast<int>(host.videoTimeMs());
    if (time != m_oldtime && m_moveValues[6] > 3) {
        // The \move's position at the shown frame.
        const Context ctx = context(host);
        PointF f = calcMovePos(ctx, m_moveValues, m_start, m_end);
        const PointF zm = view.zoomMove(), zs = view.zoomScale();
        f.x = ((f.x / view.coeffW()) - zm.x) * zs.x;
        f.y = ((f.y / view.coeffH()) - zm.y) * zs.y;
        m_from = f;
        m_to.x = m_from.x + (m_scale.x * m_arrowLengths.x);
        m_to.y = m_from.y + (m_scale.y * m_arrowLengths.y);
    }
    m_oldtime = time;
    PointF v4[6];
    v4[0] = m_from;
    v4[1] = {m_to.x, m_from.y};
    v4[2] = m_from;
    v4[3] = m_to;
    v4[4] = m_from;
    v4[5] = {m_from.x, m_to.y};
    for (int i = 1; i < 6; i += 2)
        v4[i] = drawArrow(out, v4[0], v4[i]);
    out.lines.push_back({v4[0], v4[1], 2, kHandleBorder});
    out.lines.push_back({v4[2], v4[3], 2, kHandleBorder});
    out.lines.push_back({v4[4], v4[5], 2, kHandleBorder});
    return out;
}

std::vector<ToolValue> ScaleTool::values(const VisualHost &host) const
{
    (void)host;
    return {{"fscx", u"Scale X", getfloat(m_scale.x * 100), false},
            {"fscy", u"Scale Y", getfloat(m_scale.y * 100), false}};
}

} // namespace hikari::application::visual
