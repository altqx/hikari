#pragma once

// T6: the Position shifter (legacy MoveAll, HikariSub/VisualMoveAll.cpp at
// 20d647c4, and its toolbar item MoveAllItem, VideoToolbar.cpp:354-414). The
// active Line's \pos, \move points, clip, drawing and \org are shown as
// handles; dragging one (or A/D/W/S, Shift a tenth) moves every element of
// the toggled kinds in every target Line by the same amount, one history
// step ("Position shifter") a release or a key. The toolbar's six toggles
// choose the kinds: position points, \move starting points, \move ending
// points, clips, drawings (dropped while one of the first three is on, as
// legacy's ChangeTool did) and \org points.
//
// The targets are the batch picker's Lines the Grid shows, in Document
// order (legacy SubsFile::GetSelections skips hidden Lines), else the
// active Line; the preview shows them while the drag runs.

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <optional>
#include <vector>

namespace hikari::application::visual {

class PositionShifterTool : public VisualTool {
public:
    // The element kinds (VisualMoveAll.cpp:29-36).
    enum Kind : int { Position = 1, MoveStart = 2, MoveEnd = 4, Clip = 8, Drawing = 16, Origin = 32 };

    Family family() const override { return Family::PositionShifter; }
    void reset(VisualHost &host) override;
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // MoveAllItem::GetItemToggled: bit i for the i-th toggle (default 1).
    int toggled() const;
    // The toolbar's toggles as they are (tests and the probe's replay); with
    // a host the tool takes them as a toolbar click does (ChangeTool).
    void setToggled(int bits, VisualHost *host = nullptr);

    // Legacy state, as the probe records it.
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
    // MoveAll::ChangeInLines: the targets' texts with every toggled
    // element moved by the dragged element's offset.
    void changeInLines(bool all, VisualHost &host);
    bool beginEdit(VisualHost &host);
    void takeView(const VideoView &view);
    float inX(float x) const { return ((x / m_coeffW) - m_zoomMove.x) * m_zoomScale.x; }
    float inY(float y) const { return ((y / m_coeffH) - m_zoomMove.y) * m_zoomScale.y; }
    PointF vectorPoint(const transform::ClipPoint &point, PointF drawingPos) const; // GetVector
    void drawShape(Overlay &out, const std::vector<transform::ClipPoint> &points, PointF drawingPos) const;

    std::array<bool, 6> m_toggled{true, false, false, false, false, false};

    // Visuals members.
    float m_coeffW = 1, m_coeffH = 1;
    PointF m_zoomMove{0.f, 0.f}, m_zoomScale{1.f, 1.f};
    PointF m_from, m_to, m_lastmove, m_firstmove;
    int m_axis = 0;
    double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    int m_start = 0, m_end = 0;
    transform::TagFind m_find;

    // MoveAll members (Visuals.h:328-360).
    std::vector<Element> m_elems;
    int m_numElem = -1;
    bool m_dragging = false; // a button holds a handle
    int m_selectedTags = 1;
    // The pointer's offset from the held handle (T6-drag-offset: legacy's
    // wxPoint dropped the fraction).
    float m_diffsX = 0, m_diffsY = 0;
    PointF m_beforeMove;
    // DrawVisual moves a \move drawing's position to the video's time.
    mutable PointF m_drawingPos{0, 0};
    PointF m_drawingOriginalPos{0, 0};
    PointF m_drawingScale{0, 0};
    PointF m_scale{0, 0};
    float m_vectorClipScale = 1.f;
    float m_vectorDrawScale = 1.f;
};

} // namespace hikari::application::visual
