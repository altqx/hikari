#include "hikari/application/visual_crosshair.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/grid_split.h"
#include "hikari/core/legacy_regex.h"
#include "hikari/core/text_projection.h"

// Built without floating-point contraction, as visual_view.cpp.

namespace hikari::application::visual {

namespace {

std::u16string number(int value)
{
    const std::string s = std::to_string(value);
    return std::u16string(s.begin(), s.end());
}

std::u16string toU16(const std::u8string &text)
{
    return core::toUtf16(text);
}

std::u8string toU8(const std::u16string &text)
{
    return core::toUtf8(text);
}

} // namespace

std::u16string copyCoordinatesText(const VideoView &view, int x, int y)
{
    // VideoBox::OnCopyCoords (VideoBox.cpp:1395-1412).
    const int w = view.clientWidth(), h = view.clientHeight();
    const int nx = view.scriptWidth(), ny = view.scriptHeight();
    const float coeffX = static_cast<float>(nx) / static_cast<float>(w - 1);
    const float coeffY = static_cast<float>(ny) / static_cast<float>(h - view.panelHeight() - 1);
    const int posx = static_cast<int>(static_cast<float>(x) * coeffX);
    const int posy = static_cast<int>(static_cast<float>(y) * coeffY);
    return number(posx) + u"," + number(posy);
}

void CrosshairTool::computeCoefficients(const VisualHost &host)
{
    // Cross::SetCurVisual / OnMouseEvent's Entering (VisualCross.cpp:67-93,
    // 260-281): a side without a bar divides by one pixel less.
    const VideoView &view = host.view();
    int w = 0, h = 0;
    int diffW = 1, diffH = 1;
    if (view.hasVideo()) {
        const IntRect videoRect = view.videoRect();
        m_diffX = videoRect.left;
        m_diffY = videoRect.top;
        w = videoRect.right - m_diffX;
        h = videoRect.bottom - m_diffY;
        if (m_diffX)
            diffW = 0;
        if (m_diffY)
            diffH = 0;
    } else {
        w = view.clientWidth();
        h = view.clientHeight() - view.panelHeight();
        m_diffX = m_diffY = 0;
    }
    m_coeffX = static_cast<float>(view.scriptWidth()) / static_cast<float>(w - diffW);
    m_coeffY = static_cast<float>(view.scriptHeight()) / static_cast<float>(h - diffH);
}

void CrosshairTool::reset(VisualHost &host)
{
    // Cross::SetCurVisual (VisualCross.cpp:244-282): hidden until the next move.
    m_cross = false;
    computeCoefficients(host);
    host.toolChanged();
}

void CrosshairTool::pointer(const Pointer &event, VisualHost &host)
{
    // Cross::OnMouseEvent (VisualCross.cpp:43-127). A right button release
    // hides it (and opens the context menu, V4).
    if (event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Right) {
        if (m_cross) {
            m_cross = false;
            host.toolChanged();
        }
        return;
    }
    const int x = event.x;
    const int y = event.y;
    if (event.kind == Pointer::Kind::Leave) {
        if (m_cross) {
            m_cross = false;
            host.toolChanged();
        }
        return;
    }
    if (event.kind == Pointer::Kind::Enter) {
        m_cross = true;
        computeCoefficients(host);
    }
    const VideoView &view = host.view();
    const float zx = (x / view.zoomScale().x) + view.zoomMove().x;
    const float zy = (y / view.zoomScale().y) + view.zoomMove().y;
    const int posx = static_cast<int>(zx * m_coeffX);
    const int posy = static_cast<int>(zy * m_coeffY);
    m_coords = number(posx) + u", " + number(posy);
    drawLines(x, y, host);
    host.toolChanged();

    if (event.kind == Pointer::Kind::Press &&
        (event.button == Pointer::Button::Middle || (event.button == Pointer::Button::Left && event.control)))
        putPosition(x, y, host);
}

void CrosshairTool::drawLines(int x, int y, const VisualHost &host)
{
    // Cross::DrawLines (VisualCross.cpp:188-242).
    const VideoView &view = host.view();
    if (!view.hasVideo())
        return;
    const IntRect videoRect = view.videoRect();
    if (!videoRect.contains(x, y)) {
        m_onVideo = false;
        return;
    }
    m_onVideo = true;
    // GetWindowSize(withTabPanel = true): the whole client, its panel included.
    int w = view.clientWidth(), h = view.clientHeight();
    const auto [fw, fh] = host.measureLabel(m_coords);
    const int margin = static_cast<int>(fh * 0.25f);
    w /= 2;
    h /= 2;
    m_crossRect.top = (h > y) ? y - (margin * 2) - 2 : y - (margin * 2) - 2 - fh;
    m_crossRect.bottom = (h > y) ? y + fh : y - margin;
    m_crossRect.left = (w < x) ? x - fw - margin : x + margin;
    m_crossRect.right = (w < x) ? x - margin : x + fw + margin;
    m_vectors[0] = {static_cast<float>(x), static_cast<float>(videoRect.top)};
    m_vectors[1] = {static_cast<float>(x), static_cast<float>(videoRect.bottom)};
    m_vectors[2] = {static_cast<float>(videoRect.left), static_cast<float>(y)};
    m_vectors[3] = {static_cast<float>(videoRect.right), static_cast<float>(y)};
    m_cross = true;
}

void CrosshairTool::putPosition(int x, int y, VisualHost &host)
{
    // Middle click or Ctrl+left click (VisualCross.cpp:102-126): \pos at the
    // pointer into the active Line, its other \pos and \move removed, one
    // history step ("Visual positioning tool", VISUAL_POSITION).
    const EditSession *session = host.session();
    const auto active = host.activeLine();
    if (!session || !active)
        return;
    auto gesture = host.beginGesture({*active}, std::string(familyInfo(Family::Crosshair).history));
    if (!gesture)
        return;
    const core::LineRecord &line = (*gesture)->before(*active);
    // The translation in TLMode, unless it is empty.
    const bool istl = translationMode(*session) && !line.translation.empty();
    std::u16string ltext = toU16(istl ? line.translation : line.text);
    static const core::LegacyRegex posmov(u"\\\\(pos|move)([^\\\\}]+)", core::LegacyRegex::Advanced);
    posmov.replaceAll(ltext, u"");

    const VideoView &view = host.view();
    const float zx = (x / view.zoomScale().x) + view.zoomMove().x;
    const float zy = (y / view.zoomScale().y) + view.zoomMove().y;
    const float posx = zx * m_coeffX;
    const float posy = zy * m_coeffY;
    const std::u16string postxt =
        u"\\pos(" + toU16(legacy::floatText(posx)) + u"," + toU16(legacy::floatText(posy)) + u")";
    if (ltext.starts_with(u"{"))
        ltext.insert(1, postxt);
    else
        ltext = u"{" + postxt + u"}" + ltext;
    (*gesture)->stage(*active, toU8(ltext), istl);
    (void)host.commitGesture();
}

Overlay CrosshairTool::overlay(const VisualHost &host) const
{
    // Cross::Draw (VisualCross.cpp:129-160): the label outlined in black,
    // then two black three-pixel lines with one-pixel white lines over them,
    // half a pixel across.
    Overlay out;
    if (!shown() || !host.view().hasVideo())
        return out;
    out.lines.push_back({m_vectors[0], m_vectors[1], 3, 0xFF000000});
    out.lines.push_back({m_vectors[2], m_vectors[3], 3, 0xFF000000});
    out.lines.push_back({{m_vectors[0].x + 0.5f, m_vectors[0].y}, {m_vectors[1].x + 0.5f, m_vectors[1].y}, 1, 0xFFFFFFFF});
    out.lines.push_back({{m_vectors[2].x, m_vectors[2].y + 0.5f}, {m_vectors[3].x, m_vectors[3].y + 0.5f}, 1, 0xFFFFFFFF});
    out.texts.push_back({m_crossRect, m_coords, 0xFFFFFFFF, true, 0});
    return out;
}

std::vector<ToolValue> CrosshairTool::values(const VisualHost &host) const
{
    (void)host;
    return {{"position", u"Position", m_coords, false}};
}

} // namespace hikari::application::visual
