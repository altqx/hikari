#pragma once

// T1: the crosshair (legacy Cross, VisualCross.cpp at 20d647c4), the Video
// panel's default tool, and VIDEO_COPY_COORDS (VideoBox::OnCopyCoords).

#include "hikari/application/visual_tools.h"

#include <array>
#include <string>

namespace hikari::application::visual {

// VideoBox::OnCopyCoords (VideoBox.cpp:1395-1412): the pointer's position in
// the video window scaled to the script resolution by the whole client
// (width - 1, height - panel - 1), letterbox and zoom ignored, truncated:
// "x,y".
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
