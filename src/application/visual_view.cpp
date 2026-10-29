#include "hikari/application/visual_view.h"

#include <cmath>
#include <cstdlib>

// Built without floating-point contraction so float steps round as in HikariSub legacy.

namespace hikari::application::visual {

namespace {

float mid(float a, float b, float c)
{
    const float lowest = (b < c) ? b : c;
    return (a > lowest) ? a : lowest;
}

}

AspectPair sourceAspect(const SourceGeometry &source)
{
    int arwidth = (source.sarNum == 0)
                      ? source.width
                      : static_cast<int>(static_cast<float>(source.width) *
                                         (static_cast<float>(source.sarNum) / static_cast<float>(source.sarDen)));
    int arheight = source.height;
    // Both sides 0 would divide forever.
    if (arwidth == 0 && arheight == 0)
        return {};
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
    return {arwidth, arheight};
}

float sourceAspectRatio(const SourceGeometry &source)
{
    const AspectPair a = sourceAspect(source);
    if (a.height == 0 || a.width == 0)
        return 0.0f;
    return static_cast<float>(a.height) / static_cast<float>(a.width);
}

void VideoView::open(const SourceGeometry &source)
{
    m_open = true;
    m_width = source.width;
    m_height = source.height;
    if (m_width % 2 != 0)
        m_width++;
    m_aspect = sourceAspectRatio(source);
    m_sourceAspect = m_aspect;
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
        updateRects();
}

void VideoView::setAspectRatio(float ratio)
{
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
    const int rtWidth = m_clientWidth;
    const int rtHeight = m_clientHeight - m_panelHeight;
    if (!rtHeight || !rtWidth)
        return false;
    m_windowRect = {0, 0, rtWidth, rtHeight};
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
        // MUST KILL: scales by the rect's right and bottom edges, not its size
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
    refreshToolTransform();
}

void VideoView::toggleZoom(int zoomPercentOption)
{
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
        refreshToolTransform();
    }
}

void VideoView::resetZoom()
{
    if (!m_open)
        return;
    m_zoomRect = {static_cast<float>(m_videoRect.left), static_cast<float>(m_videoRect.top),
                  static_cast<float>(m_videoRect.right), static_cast<float>(m_videoRect.bottom)};
    const int sizeX = m_videoRect.right - m_videoRect.left;
    m_sourceRect = {0, 0, m_width, m_height};
    m_zoomPercent = sizeX / (m_zoomRect.width - m_zoomRect.x);
    refreshToolTransform();
}

void VideoView::zoomPress(int x, int y)
{
    m_grabbed = -1;
    const double grab = 5 * m_dpr;
    if (std::abs(x - m_zoomRect.x) < grab) {
        m_zoomDiffX = static_cast<int>(m_zoomRect.x - x);
        m_grabbed = 0;
    } else if (std::abs(y - m_zoomRect.y) < grab) {
        m_zoomDiffY = static_cast<int>(m_zoomRect.y - y);
        m_grabbed = 1;
    } else if (std::abs(x - m_zoomRect.width) < grab) {
        m_zoomDiffX = static_cast<int>(m_zoomRect.width - x);
        m_grabbed = 2;
    } else if (std::abs(y - m_zoomRect.height) < grab) {
        m_zoomDiffY = static_cast<int>(m_zoomRect.height - y);
        m_grabbed = 3;
    } else {
        m_zoomDiffX = static_cast<int>(x - m_zoomRect.x);
        m_zoomDiffY = static_cast<int>(y - m_zoomRect.y);
    }
}

void VideoView::zoomDrag(int x, int y)
{
    if (!m_open)
        return;
    const int sx = m_videoRect.right, sy = m_videoRect.bottom;
    const int s1x = m_videoRect.right - m_videoRect.left, s1y = m_videoRect.bottom - m_videoRect.top;
    const float ar = static_cast<float>(s1x) / static_cast<float>(s1y);
    const EdgeRect before = m_zoomRect;
    const int minx = m_videoRect.left;
    const int miny = m_videoRect.top;
    if (m_grabbed < 0) {
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
    // Kept inside the video, at least 100x56.
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
    const int s1x = m_videoRect.right - m_videoRect.left, s1y = m_videoRect.bottom - m_videoRect.top;
    m_hasZoom = true;
    sourceFromZoomRect(static_cast<float>(s1x), static_cast<float>(s1y));
    m_zoomPercent = s1x / (m_zoomRect.width - m_zoomRect.x);
    refreshToolTransform();
}

void VideoView::refreshToolTransform()
{
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
    return {((script.x / coeffW()) - m_zoomMove.x) * m_zoomScale.x,
            ((script.y / coeffH()) - m_zoomMove.y) * m_zoomScale.y};
}

PointF VideoView::viewToScript(PointF view) const
{
    return {((view.x / m_zoomScale.x) + m_zoomMove.x) * coeffW(),
            ((view.y / m_zoomScale.y) + m_zoomMove.y) * coeffH()};
}

int VideoView::toDevice(double logical) const
{
    return static_cast<int>(std::lround(logical * m_dpr));
}

}
