#pragma once

#include <optional>

namespace hikari::application::visual {

struct IntRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool contains(int x, int y) const { return !(y < top || x < left || y > bottom || x > right); } // inclusive
    bool operator==(const IntRect &) const = default;
};

// MUST KILL: EdgeRect's width and height are its right and bottom edges
struct EdgeRect {
    float x = 0, y = 0, width = 0, height = 0;
    bool operator==(const EdgeRect &) const = default;
};

struct PointF {
    float x = 0, y = 0;
    bool operator==(const PointF &) const = default;
};

// FFMS2's encoded size of frame 0; SAR 0 when unknown.
struct SourceGeometry {
    int width = 0, height = 0;
    int sarNum = 0, sarDen = 1;
    bool valid() const { return width > 0 && height > 0; }
    bool operator==(const SourceGeometry &) const = default;
};

// Display width (encoded width times SAR, truncated) and height, both divided
// by common factors from 10 down to 2 while one divides.
struct AspectPair {
    int width = 0, height = 0;
    bool operator==(const AspectPair &) const = default;
};
AspectPair sourceAspect(const SourceGeometry &source);

// Height / width of sourceAspect; 0 when either side is 0.
float sourceAspectRatio(const SourceGeometry &source);

// Device pixels in and out; toDevice/toLogical convert Qt's logical coordinates.
class VideoView {
public:
    // An odd frame width is made even; the zoom state carries over from the
    // previous video.
    void open(const SourceGeometry &source);
    void close();
    bool hasVideo() const { return m_open; }

    void setClient(int width, int height, int panelHeight);
    int clientWidth() const { return m_clientWidth; }
    int clientHeight() const { return m_clientHeight; }
    int panelHeight() const { return m_panelHeight; }

    // Height / width.
    void setAspectRatio(float ratio);
    float aspectRatio() const { return m_aspect; }
    float ownAspectRatio() const { return m_sourceAspect; }

    void setScript(int width, int height);
    int scriptWidth() const { return m_scriptWidth; }
    int scriptHeight() const { return m_scriptHeight; }

    // The tools take the zoom at once.
    void zoomAt(float percent, int x, int y);
    // Entering zoom mode unzoomed zooms by zoomPercentOption / 100 (2 outside
    // 1..11); the tools take it at once.
    void toggleZoom(int zoomPercentOption);
    // The whole frame; the zoom mode stays as it was.
    void resetZoom();
    // Zoom mode: zoomDrag moves with the left button held (pan or an edge).
    void zoomPress(int x, int y);
    void zoomDrag(int x, int y);
    void zoomWheel(int steps);
    bool zoomMode() const { return m_hasZoom; }

    void refreshToolTransform();

    int frameWidth() const { return m_width; }   // even
    int frameHeight() const { return m_height; }
    const IntRect &windowRect() const { return m_windowRect; }
    const IntRect &videoRect() const { return m_videoRect; }
    // Right and bottom edges excluded; false without video.
    bool onPicture(int x, int y) const
    {
        return m_open && x >= m_videoRect.left && x < m_videoRect.right && y >= m_videoRect.top &&
               y < m_videoRect.bottom;
    }
    const IntRect &sourceRect() const { return m_sourceRect; }
    const EdgeRect &zoomRect() const { return m_zoomRect; }
    float zoomPercent() const { return m_zoomPercent; }

    // Script units per video-rectangle pixel.
    float coeffW() const;
    float coeffH() const;
    PointF zoomMove() const { return m_zoomMove; }
    PointF zoomScale() const { return m_zoomScale; }
    PointF scriptToView(PointF script) const;
    PointF viewToScript(PointF view) const;

    // A pointer's logical position rounds to the device pixel it came from.
    void setDevicePixelRatio(double ratio) { m_dpr = ratio > 0 ? ratio : 1.0; }
    double devicePixelRatio() const { return m_dpr; }
    int toDevice(double logical) const;
    double toLogical(double device) const { return device / m_dpr; }

private:
    bool updateRects(bool changeZoom = true);
    void sourceFromZoomRect(float screenWidth, float screenHeight);
    void clampZoomRect(const EdgeRect &before);
    void zoom();

    bool m_open = false;
    int m_clientWidth = 0, m_clientHeight = 0, m_panelHeight = 0;
    int m_width = 0, m_height = 0;
    float m_aspect = 0;
    float m_sourceAspect = 0;
    int m_scriptWidth = 0, m_scriptHeight = 0;
    IntRect m_windowRect, m_videoRect, m_sourceRect;
    EdgeRect m_zoomRect;
    float m_zoomPercent = 1.f;
    bool m_hasZoom = false;
    signed char m_grabbed = -1;
    int m_zoomDiffX = 0, m_zoomDiffY = 0;
    PointF m_zoomMove{0, 0};
    PointF m_zoomScale{1, 1};
    double m_dpr = 1.0;
};

} // namespace hikari::application::visual
