#include "hikari/application/visual_view.h"

#include <cmath>
#include <cstdlib>

// Built without floating-point contraction (see CMakeLists.txt): legacy's
// float steps must round as they did in its build.

namespace hikari::application::visual {

namespace {

// config.h:552-560: MID(a,b,c) is MAX((a),MIN((b),(c))), int and float
// mixed as written.
float mid(float a, float b, float c)
{
    const float lowest = (b < c) ? b : c;
    return (a > lowest) ? a : lowest;
}

} // namespace

float sourceAspectRatio(const SourceGeometry &source)
{
    // ProviderFFMS2.cpp:364-380.
    int arwidth = (source.sarNum == 0)
                      ? source.width
                      : static_cast<int>(static_cast<float>(source.width) *
                                         (static_cast<float>(source.sarNum) / static_cast<float>(source.sarDen)));
    int arheight = source.height;
    while (true) {
        bool divided = false;
        for (int i = 10; i > 1; i--) {
            if (arwidth % i == 0 && arheight % i == 0) {
                arwidth /= i;
                arheight /= i;
                divided = true;
                break;
            }
        }
        if (!divided)
            break;
    }
    // RendererFFMS2.cpp:488-489.
    if (arheight == 0 || arwidth == 0)
        return 0.0f;
    return static_cast<float>(arheight) / static_cast<float>(arwidth);
}

void VideoView::open(const SourceGeometry &source)
{
    // RendererFFMS2::OpenFile (RendererFFMS2.cpp:460-491).
    m_open = true;
    m_width = source.width;
    m_height = source.height;
    if (m_width % 2 != 0)
        m_width++;
    m_aspect = sourceAspectRatio(source);
    m_sourceRect = {0, 0, m_width, m_height};
    updateRects();
}

void VideoView::close()
{
    m_open = false;
}

void VideoView::setClient(int width, int height, int panelHeight)
{
    m_clientWidth = width;
    m_clientHeight = height;
    m_panelHeight = panelHeight;
    if (m_open)
        updateRects(); // RendererVideo::UpdateVideoWindow on a resize
}

void VideoView::setAspectRatio(float ratio)
{
    // VideoBox::SetAspectRatio (VideoBox.cpp:1285-1299): nothing without a renderer.
    if (!m_open)
        return;
    m_aspect = ratio;
    updateRects();
}

void VideoView::setScript(int width, int height)
{
    m_scriptWidth = width;
    m_scriptHeight = height;
}

bool VideoView::updateRects(bool changeZoom)
{
    // RendererVideo::UpdateRects (RendererVideo.cpp:213-280), out of fullscreen.
    const int rtWidth = m_clientWidth;
    const int rtHeight = m_clientHeight - m_panelHeight;
    if (!rtHeight || !rtWidth)
        return false;
    m_windowRect = {0, 0, rtWidth, rtHeight};
    // A zero aspect divides by zero in legacy (no source has one).
    if (m_aspect == 0.0f) {
        m_videoRect = m_windowRect;
    } else {
        const int arwidth = static_cast<int>(static_cast<float>(rtHeight) / m_aspect);
        const int arheight = static_cast<int>(static_cast<float>(rtWidth) * m_aspect);
        if (arwidth > rtWidth) {
            const int onebar = (rtHeight - arheight) / 2;
            m_videoRect = {0, onebar, rtWidth, arheight + onebar};
        } else if (arheight > rtHeight) {
            const int onebar = (rtWidth - arwidth) / 2;
            m_videoRect = {onebar, 0, arwidth + onebar, rtHeight};
        } else {
            m_videoRect = m_windowRect;
        }
    }
    if (changeZoom) {
        // The right and bottom edges, not the size, as legacy wrote it.
        const float videoToScreenX = static_cast<float>(m_videoRect.right) / static_cast<float>(m_width);
        const float videoToScreenY = static_cast<float>(m_videoRect.bottom) / static_cast<float>(m_height);
        m_zoomRect.x = (m_sourceRect.left * videoToScreenX) + m_videoRect.left;
        m_zoomRect.y = (m_sourceRect.top * videoToScreenY) + m_videoRect.top;
        m_zoomRect.height = (m_sourceRect.bottom * videoToScreenY);
        m_zoomRect.width = (m_sourceRect.right * videoToScreenX);
        refreshToolTransform();
    }
    return true;
}

void VideoView::sourceFromZoomRect(float screenWidth, float screenHeight)
{
    const float videoToScreenXX = screenWidth / static_cast<float>(m_width);
    const float videoToScreenYY = screenHeight / static_cast<float>(m_height);
    m_sourceRect.left = static_cast<int>((m_zoomRect.x - m_videoRect.left) / videoToScreenXX);
    m_sourceRect.top = static_cast<int>((m_zoomRect.y - m_videoRect.top) / videoToScreenYY);
    m_sourceRect.right = static_cast<int>((m_zoomRect.width - m_videoRect.left) / videoToScreenXX);
    m_sourceRect.bottom = static_cast<int>((m_zoomRect.height - m_videoRect.top) / videoToScreenYY);
}

void VideoView::zoomAt(float percent, int mouseX, int mouseY)
{
    // RendererVideo::SetZoom(percent, mousePos) (RendererVideo.cpp:709-740).
    if (!m_open)
        return;
    m_zoomPercent = percent;
    const float x = static_cast<float>(m_videoRect.right - m_videoRect.left);
    const float y = static_cast<float>(m_videoRect.bottom - m_videoRect.top);
    const float xpar = x / percent;
    const float ypar = y / percent;
    float left = m_videoRect.left + mouseX - (xpar / 2.f);
    float top = m_videoRect.top + mouseY - (ypar / 2.f);
    if (left + xpar > x)
        left = m_videoRect.left + (x - xpar);
    else if (left < m_videoRect.left)
        left = static_cast<float>(m_videoRect.left);
    if (top + ypar > y)
        top = m_videoRect.top + (y - ypar);
    else if (top < m_videoRect.top)
        top = static_cast<float>(m_videoRect.top);
    m_zoomRect = {left, top, xpar + left, ypar + top};
    sourceFromZoomRect(x, y);
    m_zoomPercent = x / (m_zoomRect.width - m_zoomRect.x);
}

void VideoView::toggleZoom(int zoomPercentOption)
{
    // RendererVideo::SetZoom() (RendererVideo.cpp:741-765).
    if (!m_open)
        return;
    m_hasZoom = !m_hasZoom;
    if ((m_zoomPercent == 1.f && m_hasZoom) || m_zoomRect.width < 1) {
        float npercent = zoomPercentOption / 100.f;
        npercent = (npercent < 1.f || npercent > 11.f) ? 2.f : npercent;
        const float x = static_cast<float>(m_videoRect.right - m_videoRect.left);
        const float y = static_cast<float>(m_videoRect.bottom - m_videoRect.top);
        const float xpar = x / npercent;
        const float ypar = y / npercent;
        const float left = m_videoRect.left + ((x - xpar) / 2.f);
        const float top = m_videoRect.top + ((y - ypar) / 2.f);
        m_zoomRect = {left, top, xpar + left, ypar + top};
        sourceFromZoomRect(x, y);
        m_zoomPercent = x / (m_zoomRect.width - m_zoomRect.x);
    }
}

void VideoView::resetZoom()
{
    // RendererVideo::ResetZoom (RendererVideo.cpp:768-793).
    if (!m_open)
        return;
    m_zoomRect = {static_cast<float>(m_videoRect.left), static_cast<float>(m_videoRect.top),
                  static_cast<float>(m_videoRect.right), static_cast<float>(m_videoRect.bottom)};
    const int sizeX = m_videoRect.right - m_videoRect.left;
    const int sizeY = m_videoRect.bottom - m_videoRect.top;
    sourceFromZoomRect(static_cast<float>(sizeX), static_cast<float>(sizeY));
    m_zoomPercent = sizeX / (m_zoomRect.width - m_zoomRect.x);
    refreshToolTransform();
}

void VideoView::zoomPress(int x, int y)
{
    // ZoomMouseHandle's LeftDown (RendererVideo.cpp:916-938); m_ZoomDiff is a
    // wxPoint, so each difference is truncated.
    m_grabbed = -1;
    if (std::abs(x - m_zoomRect.x) < 5) {
        m_zoomDiffX = static_cast<int>(m_zoomRect.x - x);
        m_grabbed = 0;
    } else if (std::abs(y - m_zoomRect.y) < 5) {
        m_zoomDiffY = static_cast<int>(m_zoomRect.y - y);
        m_grabbed = 1;
    } else if (std::abs(x - m_zoomRect.width) < 5) {
        m_zoomDiffX = static_cast<int>(m_zoomRect.width - x);
        m_grabbed = 2;
    } else if (std::abs(y - m_zoomRect.height) < 5) {
        m_zoomDiffY = static_cast<int>(m_zoomRect.height - y);
        m_grabbed = 3;
    } else {
        m_zoomDiffX = static_cast<int>(x - m_zoomRect.x);
        m_zoomDiffY = static_cast<int>(y - m_zoomRect.y);
    }
}

void VideoView::zoomDrag(int x, int y)
{
    // ZoomMouseHandle with the left button held (RendererVideo.cpp:940-1032).
    if (!m_open)
        return;
    const int sx = m_videoRect.right, sy = m_videoRect.bottom;
    const int s1x = m_videoRect.right - m_videoRect.left, s1y = m_videoRect.bottom - m_videoRect.top;
    const float ar = static_cast<float>(s1x) / static_cast<float>(s1y);
    const EdgeRect before = m_zoomRect;
    const int minx = m_videoRect.left;
    const int miny = m_videoRect.top;
    if (m_grabbed < 0) {
        // The pan: no clamps after it, Zoom only when it moved.
        const float oldzx = m_zoomRect.x;
        const float oldzy = m_zoomRect.y;
        if ((m_zoomRect.x >= minx && m_zoomRect.width < sx) ||
            (m_zoomRect.width == sx && m_zoomRect.x > x - m_zoomDiffX)) {
            const float zoomwidth = m_zoomRect.width - m_zoomRect.x;
            m_zoomRect.x = static_cast<float>(x - m_zoomDiffX);
            m_zoomRect.x = mid(static_cast<float>(minx), m_zoomRect.x, sx - zoomwidth);
            m_zoomRect.width += (m_zoomRect.x - oldzx);
        }
        if ((m_zoomRect.y >= miny && m_zoomRect.height < sy) ||
            (m_zoomRect.height == sy && m_zoomRect.y > y - m_zoomDiffY)) {
            const float zoomheight = m_zoomRect.height - m_zoomRect.y;
            m_zoomRect.y = static_cast<float>(y - m_zoomDiffY);
            m_zoomRect.y = mid(static_cast<float>(miny), m_zoomRect.y, sy - zoomheight);
            m_zoomRect.height += (m_zoomRect.y - oldzy);
        }
        if (m_zoomRect.x != oldzx || m_zoomRect.y != oldzy)
            zoom();
        return;
    }
    if (m_grabbed < 2) {
        if (m_grabbed == 0) {
            const float oldzx = m_zoomRect.x;
            m_zoomRect.x = static_cast<float>(x - m_zoomDiffX);
            m_zoomRect.y += (m_zoomRect.x - oldzx) / ar;
            m_zoomRect.width -= (m_zoomRect.x - oldzx);
            m_zoomRect.height -= (m_zoomRect.x - oldzx) / ar;
        } else {
            const float oldzy = m_zoomRect.y;
            m_zoomRect.y = static_cast<float>(y - m_zoomDiffY);
            m_zoomRect.x += (m_zoomRect.y - oldzy) * ar;
            m_zoomRect.height -= (m_zoomRect.y - oldzy);
            m_zoomRect.width -= (m_zoomRect.y - oldzy) * ar;
        }
    } else if (m_grabbed == 2) {
        const float oldzw = m_zoomRect.width;
        m_zoomRect.width = static_cast<float>(x - m_zoomDiffX);
        m_zoomRect.height += (m_zoomRect.width - oldzw) / ar;
    } else {
        const float oldzh = m_zoomRect.height;
        m_zoomRect.height = static_cast<float>(y - m_zoomDiffY);
        m_zoomRect.width += (m_zoomRect.height - oldzh) * ar;
    }
    clampZoomRect(before);
}

void VideoView::zoomWheel(int steps)
{
    // ZoomMouseHandle's wheel (RendererVideo.cpp:947-952): five pixels a step.
    if (!m_open || steps == 0)
        return;
    const int s1x = m_videoRect.right - m_videoRect.left, s1y = m_videoRect.bottom - m_videoRect.top;
    const float ar = static_cast<float>(s1x) / static_cast<float>(s1y);
    const EdgeRect before = m_zoomRect;
    const int step = 5 * steps;
    m_zoomRect.x -= step;
    m_zoomRect.y -= step / ar;
    m_zoomRect.width += step;
    m_zoomRect.height += step / ar;
    clampZoomRect(before);
}

void VideoView::clampZoomRect(const EdgeRect &before)
{
    // RendererVideo.cpp:1009-1031: kept inside the video, at least 100x56,
    // then Zoom(s1).
    const int sx = m_videoRect.right, sy = m_videoRect.bottom;
    const int minx = m_videoRect.left;
    const int miny = m_videoRect.top;
    if (m_zoomRect.width > sx) {
        m_zoomRect.x -= m_zoomRect.width - sx;
        m_zoomRect.width = static_cast<float>(sx);
    }
    if (m_zoomRect.height > sy) {
        m_zoomRect.y -= m_zoomRect.height - sy;
        m_zoomRect.height = static_cast<float>(sy);
    }
    if (m_zoomRect.x < minx) {
        m_zoomRect.width -= (m_zoomRect.x - minx);
        m_zoomRect.x = static_cast<float>(minx);
    }
    if (m_zoomRect.y < miny) {
        m_zoomRect.height -= (m_zoomRect.y - miny);
        m_zoomRect.y = static_cast<float>(miny);
    }
    m_zoomRect.width = (m_zoomRect.width < sx) ? m_zoomRect.width : static_cast<float>(sx);
    m_zoomRect.height = (m_zoomRect.height < sy) ? m_zoomRect.height : static_cast<float>(sy);
    m_zoomRect.x = mid(static_cast<float>(minx), m_zoomRect.x, static_cast<float>(sx));
    m_zoomRect.y = mid(static_cast<float>(miny), m_zoomRect.y, static_cast<float>(sy));
    if (m_zoomRect.width - m_zoomRect.x < 100 || m_zoomRect.height - m_zoomRect.y < 56)
        m_zoomRect = before;
    zoom();
}

void VideoView::zoom()
{
    // RendererVideo::Zoom(s1) (RendererVideo.cpp:795-816).
    const int s1x = m_videoRect.right - m_videoRect.left, s1y = m_videoRect.bottom - m_videoRect.top;
    m_hasZoom = true;
    sourceFromZoomRect(static_cast<float>(s1x), static_cast<float>(s1y));
    m_zoomPercent = s1x / (m_zoomRect.width - m_zoomRect.x);
    refreshToolTransform();
}

void VideoView::refreshToolTransform()
{
    // RendererVideo::SetVisualZoom (RendererVideo.cpp:818-829).
    if (!m_open || m_width == 0 || m_height == 0)
        return;
    const float videoToScreenX =
        static_cast<float>(m_videoRect.right - m_videoRect.left) / static_cast<float>(m_width);
    const float videoToScreenY =
        static_cast<float>(m_videoRect.bottom - m_videoRect.top) / static_cast<float>(m_height);
    const float zoomX = m_sourceRect.left * videoToScreenX;
    const float zoomY = m_sourceRect.top * videoToScreenY;
    m_zoomScale = {static_cast<float>(m_width) / static_cast<float>(m_sourceRect.right - m_sourceRect.left),
                   static_cast<float>(m_height) / static_cast<float>(m_sourceRect.bottom - m_sourceRect.top)};
    m_zoomMove = {zoomX - (m_videoRect.left / m_zoomScale.x), zoomY - (m_videoRect.top / m_zoomScale.y)};
}

float VideoView::coeffW() const
{
    // Visuals::SizeChanged (Visuals.cpp:285-300) on the video rectangle.
    const int videoWidth = m_videoRect.right - m_videoRect.left;
    const int videoHeight = m_videoRect.bottom - m_videoRect.top;
    if (videoWidth <= 0 || videoHeight <= 0 || m_scriptWidth <= 0 || m_scriptHeight <= 0)
        return 1.f;
    return static_cast<float>(m_scriptWidth) / static_cast<float>(videoWidth);
}

float VideoView::coeffH() const
{
    const int videoWidth = m_videoRect.right - m_videoRect.left;
    const int videoHeight = m_videoRect.bottom - m_videoRect.top;
    if (videoWidth <= 0 || videoHeight <= 0 || m_scriptWidth <= 0 || m_scriptHeight <= 0)
        return 1.f;
    return static_cast<float>(m_scriptHeight) / static_cast<float>(videoHeight);
}

PointF VideoView::scriptToView(PointF script) const
{
    // Visuals::GetCalculatedInPosX/Y (Visuals.h:139-146).
    return {((script.x / coeffW()) - m_zoomMove.x) * m_zoomScale.x,
            ((script.y / coeffH()) - m_zoomMove.y) * m_zoomScale.y};
}

PointF VideoView::viewToScript(PointF view) const
{
    // Visuals::GetCalculatedOutPosX/Y (Visuals.h:147-154).
    return {((view.x / m_zoomScale.x) + m_zoomMove.x) * coeffW(),
            ((view.y / m_zoomScale.y) + m_zoomMove.y) * coeffH()};
}

int VideoView::toDevice(double logical) const
{
    return static_cast<int>(std::lround(logical * m_dpr));
}

} // namespace hikari::application::visual
