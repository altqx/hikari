#pragma once

// T1: the crosshair (legacy Cross, VisualCross.cpp at 20d647c4), the Video
// panel's default tool, and VIDEO_COPY_COORDS (VideoBox::OnCopyCoords).

#include "hikari/application/visual_tools.h"

#include <array>
#include <string>

namespace hikari::application::visual {

// Cross's coefficients for the view (VisualCross.cpp:67-93): script units per
// video-rectangle pixel, and the rectangle's left and top bars.
struct CrossCoefficients {
    float x = 0, y = 0;
    int diffX = 0, diffY = 0;
};
CrossCoefficients crossCoefficients(const VideoView &view);
// Cross's script position of a view point in device pixels: the tools' zoom
// transform, then the given coefficients (VisualCross.cpp:94-97, 112-113).
PointF crossScriptPoint(const VideoView &view, float coeffX, float coeffY, int x, int y);

// VIDEO_COPY_COORDS (VideoBox::OnCopyCoords, VideoBox.cpp:1395-1412) in
// legacy's text form "x,y": the script coordinate under the pointer, the
// crosshair's conversion truncated as its label is. Approved departure
// T1-copy-coords-view: legacy scaled the pointer by the whole client less
// one pixel, ignoring the letterbox, the pillarbox and the zoom.
std::u16string copyCoordinatesText(const VideoView &view, int x, int y);

class CrosshairTool : public VisualTool {
public:
    Family family() const override { return Family::Crosshair; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;

    // Cross's state, as the legacy probe records it.
    bool shown() const { return m_cross && m_onVideo; } // Cross::Draw's condition
    bool crossOn() const { return m_cross; }
    bool onVideo() const { return m_onVideo; }
    const std::u16string &label() const { return m_coords; }
    const IntRect &labelRect() const { return m_crossRect; }
    const std::array<PointF, 4> &lines() const { return m_vectors; }
    PointF coefficients() const { return {m_coeffX, m_coeffY}; }

private:
    void computeCoefficients(const VisualHost &host);
    void drawLines(int x, int y, const VisualHost &host);
    void putPosition(int x, int y, VisualHost &host);

    std::array<PointF, 4> m_vectors{};
    IntRect m_crossRect;
    std::u16string m_coords;
    bool m_cross = false;
    float m_coeffX = 0, m_coeffY = 0;
    int m_diffX = 0, m_diffY = 0;
    bool m_onVideo = true;
};

} // namespace hikari::application::visual
