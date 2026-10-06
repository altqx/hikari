#pragma once

// T1: the Video panel's geometry and the shared script-to-source-to-view
// transform (docs/qt/visual-tools.md). Every number follows the legacy
// arithmetic at 20d647c4, its float steps and int truncations included, so
// a pointer maps to the coordinate legacy wrote and a script point is drawn
// where legacy drew it:
//
//   - the video rectangle: RendererVideo::UpdateRects (RendererVideo.cpp:213-280),
//     the frame's aspect from its encoded size and SAR (ProviderFFMS2.cpp:364-380,
//     RendererFFMS2.cpp:460-491) or the AspectRatioDialog override
//     (VideoBox::SetAspectRatio, VideoBox.cpp:1285-1299);
//   - the zoom: RendererVideo::SetZoom, ResetZoom, Zoom and the zoom mode's
//     ZoomMouseHandle (RendererVideo.cpp:709-1033), which V4 (#183) puts on
//     the menus, wheel and keys;
//   - the visual tools' transform: Visuals::SizeChanged (Visuals.cpp:285-304)
//     and RendererVideo::SetVisualZoom (RendererVideo.cpp:818-829), read with
//     Visuals::GetCalculatedInPos / GetCalculatedOutPos (Visuals.h:139-154).
//
// Legacy works in the video window's device pixels. The view's inputs and
// outputs are device pixels too; toDevice/toLogical convert Qt's logical
// item coordinates at the window's device pixel ratio. The checked fixtures
// are the legacy probe's captures (tools/legacy-capture/visual_capture.cpp,
// tests/fixtures/legacy-observations/local-t1-visual-20261005).

#include <optional>

namespace hikari::application::visual {

// A legacy RECT: left, top, right, bottom.
struct IntRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool contains(int x, int y) const { return !(y < top || x < left || y > bottom || x > right); } // inclusive
    bool operator==(const IntRect &) const = default;
};

// The legacy FloatRect (RendererVideo.h:66-77): its width and height are the
// right and bottom edges.
struct EdgeRect {
    float x = 0, y = 0, width = 0, height = 0;
    bool operator==(const EdgeRect &) const = default;
};

struct PointF {
    float x = 0, y = 0;
    bool operator==(const PointF &) const = default;
};

// The opened video's frame: FFMS2's encoded size of frame 0 and the track's
// sample aspect ratio (0 when unknown), as legacy ProviderFFMS2 read them.
struct SourceGeometry {
    int width = 0, height = 0;
    int sarNum = 0, sarDen = 1;
    bool valid() const { return width > 0 && height > 0; }
    bool operator==(const SourceGeometry &) const = default;
};

// Legacy m_arwidth : m_arheight of a source (ProviderFFMS2.cpp:364-380):
// the display width (the encoded width times the SAR, truncated) and the
// height, both divided by common factors from 10 down to 2 while one
// divides. The status bar's aspect ratio field (P10) shows it.
struct AspectPair {
    int width = 0, height = 0;
    bool operator==(const AspectPair &) const = default;
};
AspectPair sourceAspect(const SourceGeometry &source);

// Legacy m_AspectRatio (height / width) of a source: the display width is
// the encoded width times the SAR (truncated), both sides divided by common
// factors from 10 down to 2 while one divides (ProviderFFMS2.cpp:366-380),
// then height / width (RendererFFMS2.cpp:488-489); 0 when either side is 0.
float sourceAspectRatio(const SourceGeometry &source);

class VideoView {
public:
    // RendererFFMS2::OpenFile: the frame size (an odd width made even), the
    // source's aspect, the whole frame shown, then UpdateRects. The zoom
    // state carries over from the previous video, as the renderer's does.
    void open(const SourceGeometry &source);
    void close();
    bool hasVideo() const { return m_open; }

    // The video window's client size and its panel's height, in device
    // pixels (legacy GetClientSize and m_PanelHeight): UpdateRects.
    void setClient(int width, int height, int panelHeight);
    int clientWidth() const { return m_clientWidth; }
    int clientHeight() const { return m_clientHeight; }
    int panelHeight() const { return m_panelHeight; }

    // VideoBox::SetAspectRatio: legacy m_AspectRatio (height / width), then
    // UpdateRects (UpdateVideoWindow).
    void setAspectRatio(float ratio);
    float aspectRatio() const { return m_aspect; }

    // The script resolution (SubsGrid::GetASSRes) the tools convert to.
    void setScript(int width, int height);
    int scriptWidth() const { return m_scriptWidth; }
    int scriptHeight() const { return m_scriptHeight; }

    // RendererVideo::SetZoom(percent, mousePos): the wheel's zoom at a point.
    // Legacy left the tools' transform as it was until its next SetVisual or
    // resize; here the tools take the zoom at once (approved departure
    // T1-wheel-zoom-stale).
    void zoomAt(float percent, int x, int y);
    // RendererVideo::SetZoom(): GLOBAL_VIDEO_ZOOM and Return in zoom mode
    // toggle the zoom mode; entering it unzoomed zooms by VIDEO_ZOOM_PERCENT
    // / 100 (2 outside 1..11, the default when unset), and the tools take
    // that zoom at once too (T1-wheel-zoom-stale).
    void toggleZoom(int zoomPercentOption);
    // RendererVideo::ResetZoom (GLOBAL_RESET_VIDEO_ZOOM, Ctrl+Shift+Z): the
    // whole frame; the zoom mode stays as it was.
    void resetZoom();
    // The zoom mode's mouse (ZoomMouseHandle): a left press, a move with the
    // left button held (pan, or an edge dragged), the wheel (wheel steps).
    void zoomPress(int x, int y);
    void zoomDrag(int x, int y);
    void zoomWheel(int steps);
    bool zoomMode() const { return m_hasZoom; }

    // RendererVideo::SetVisualZoom: the tools take the current zoom.
    void refreshToolTransform();

    // State, as legacy names it.
    int frameWidth() const { return m_width; }   // m_Width (even)
    int frameHeight() const { return m_height; } // m_Height
    const IntRect &windowRect() const { return m_windowRect; }
    const IntRect &videoRect() const { return m_videoRect; }   // m_BackBufferRect
    const IntRect &sourceRect() const { return m_sourceRect; } // m_MainStreamRect
    const EdgeRect &zoomRect() const { return m_zoomRect; }
    float zoomPercent() const { return m_zoomPercent; }

    // Visuals::coeffW / coeffH (script units per video-rectangle pixel) and
    // the tools' zoomMove / zoomScale.
    float coeffW() const;
    float coeffH() const;
    PointF zoomMove() const { return m_zoomMove; }
    PointF zoomScale() const { return m_zoomScale; }
    // Visuals::GetCalculatedInPos: a script point to the view.
    PointF scriptToView(PointF script) const;
    // Visuals::GetCalculatedOutPos: a view point to the script.
    PointF viewToScript(PointF view) const;

    // Qt's logical coordinates and legacy's device pixels. A pointer's
    // logical position is rounded to the device pixel it came from.
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
    int m_scriptWidth = 0, m_scriptHeight = 0;
    IntRect m_windowRect, m_videoRect, m_sourceRect;
    EdgeRect m_zoomRect;
    float m_zoomPercent = 1.f;
    bool m_hasZoom = false;
    signed char m_grabbed = -1;
    int m_zoomDiffX = 0, m_zoomDiffY = 0; // wxPoint m_ZoomDiff
    PointF m_zoomMove{0, 0};
    PointF m_zoomScale{1, 1};
    double m_dpr = 1.0;
};

} // namespace hikari::application::visual
