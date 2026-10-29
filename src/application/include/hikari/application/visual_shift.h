#pragma once

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <optional>
#include <vector>

namespace hikari::application::visual {

// Targets: the batch picker's shown Lines in Document order, else the active Line.
class PositionShifterTool : public VisualTool {
public:
    enum Kind : int { Position = 1, MoveStart = 2, MoveEnd = 4, Clip = 8, Drawing = 16, Origin = 32 };

    Family family() const override { return Family::PositionShifter; }
    void reset(VisualHost &host) override;
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // Bit i for the i-th toggle.
    int toggled() const;
    // With a host the tool takes them as a toolbar click does.
    void setToggled(int bits, VisualHost *host = nullptr);

    // MUST KILL: legacy-state accessors kept for the probe and tests
    struct Element {
        int type = 0;
        PointF elem;
        std::optional<std::vector<transform::ClipPoint>> points;
    };
    const std::vector<Element> &elements() const { return m_elems; }
    int numElem() const { return m_numElem; }
    int selectedTags() const { return m_selectedTags; }
    PointF from() const { return m_from; }
    PointF drawingPos() const { return m_drawingPos; }
    PointF drawingOriginalPos() const { return m_drawingOriginalPos; }
    PointF drawingScale() const { return m_drawingScale; }
    PointF scale() const { return m_scale; }
    PointF beforeMove() const { return m_beforeMove; }
    float vectorClipScale() const { return m_vectorClipScale; }
    const double *moveValues() const { return m_moveValues; }

private:
    void changeTool(int tool, VisualHost &host);
    void setCurVisual(VisualHost &host);
    // Moves every toggled element of the targets by the dragged element's offset.
    void changeInLines(bool all, VisualHost &host);
    bool beginEdit(VisualHost &host);
    void takeView(const VideoView &view);
    float inX(float x) const { return ((x / m_coeffW) - m_zoomMove.x) * m_zoomScale.x; }
    float inY(float y) const { return ((y / m_coeffH) - m_zoomMove.y) * m_zoomScale.y; }
    PointF vectorPoint(const transform::ClipPoint &point, PointF drawingPos) const;
    void drawShape(Overlay &out, const std::vector<transform::ClipPoint> &points, PointF drawingPos) const;

    std::array<bool, 6> m_toggled{true, false, false, false, false, false};

    float m_coeffW = 1, m_coeffH = 1;
    PointF m_zoomMove{0.f, 0.f}, m_zoomScale{1.f, 1.f};
    PointF m_from, m_to, m_lastmove, m_firstmove;
    int m_axis = 0;
    double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    int m_start = 0, m_end = 0;
    transform::TagFind m_find;

    std::vector<Element> m_elems;
    int m_numElem = -1;
    bool m_dragging = false;
    int m_selectedTags = 1;
    // The pointer's offset from the held handle.
    float m_diffsX = 0, m_diffsY = 0;
    PointF m_beforeMove;
    // MUST KILL: the const overlay() moves a \move drawing's position to the video's time
    mutable PointF m_drawingPos{0, 0};
    PointF m_drawingOriginalPos{0, 0};
    PointF m_drawingScale{0, 0};
    PointF m_scale{0, 0};
    float m_vectorClipScale = 1.f;
    float m_vectorDrawScale = 1.f;
};

} // namespace hikari::application::visual
