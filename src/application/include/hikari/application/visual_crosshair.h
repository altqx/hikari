#pragma once

#include "hikari/application/visual_tools.h"

#include <array>
#include <string>

namespace hikari::application::visual {

// Script units per video-rectangle pixel, and the rectangle's left and top bars.
struct CrossCoefficients {
    float x = 0, y = 0;
    int diffX = 0, diffY = 0;
};
CrossCoefficients crossCoefficients(const VideoView &view);
// Script position of a view point in device pixels, zoom applied.
PointF crossScriptPoint(const VideoView &view, float coeffX, float coeffY, int x, int y);

// "x,y": the script coordinate under the pointer, truncated as the label is.
std::u16string copyCoordinatesText(const VideoView &view, int x, int y);

class CrosshairTool : public VisualTool {
public:
    Family family() const override { return Family::Crosshair; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    std::u16string copyText() const { return m_x.empty() ? std::u16string() : m_x + u"," + m_y; }

    bool shown() const { return m_cross && m_onVideo; }
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
    std::u16string m_x, m_y;
    bool m_cross = false;
    float m_coeffX = 0, m_coeffY = 0;
    int m_diffX = 0, m_diffY = 0;
    bool m_onVideo = true;
};

}
