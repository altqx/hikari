#pragma once

// T3: the rotation families (legacy RotationZ and RotationXY,
// VisualRotationZ.cpp and VisualRotationXY.cpp at 20d647c4, and their
// toolbar items RotationZItem and RotationXYItem, VideoToolbar.cpp:827-897,
// 1197-1262).
//
// Z rotation: dragging around the \org circle writes \frz; dragging the
// \org cross moves it (\org written, with a \pos put in first on a Line
// without one); "Set angle from 2 points" takes the angle of two clicked
// points (the first click only places a point, Esc drops it); "Changing all
// Z-rotation tags" turns every \frz by the same amount and "Preserve
// proportions" also turns the several targets' positions and clips about
// \org.
//
// X/Y rotation: left drag writes \fry (the horizontal distance in degrees),
// right drag \frx (the vertical one), middle drag both; the \org cross moves
// as for Z; "Changing all X/Y-rotation tags" adds to every tag. The overlay is
// legacy's Direct3D grid, turned and projected as RotationXY::DrawVisual set
// up its matrices.

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"

#include <array>
#include <optional>

namespace hikari::application::visual {

class RotationZTool : public VisualTool {
public:
    Family family() const override { return Family::RotationZ; }
    void reset(VisualHost &host) override;
    bool keepsStateAfterCommit() const override { return true; }
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    bool cancelPending(VisualHost &host) override;
    bool hasPending() const override;

    // RotationZItem::GetItemToggled: bit 1 two points, 2 change all tags,
    // 4 preserve proportions.
    int toggled() const;
    void setToggled(int bits, VisualHost *host = nullptr);

    PointF from() const { return m_from; }
    PointF to() const { return m_to; }
    PointF org() const { return m_org; }
    float lastAngle() const { return m_lastAngle; }
    bool hasTwoPoints() const { return m_hasTwoPoints; }
    PointF lastmove() const { return m_lastmove; }
    const std::array<PointF, 2> &twoPoints() const { return m_twoPoints; }
    const std::array<bool, 2> &visibility() const { return m_visibility; }
    std::pair<long, long> editorCaret() const { return m_find.selection(); } // as ScaleTool's

private:
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    void setCurVisual(VisualHost &host);
    void setVisual(bool dummy, VisualHost &host);
    void changeVisualLine(std::u16string &text, const core::LineRecord &line, const transform::Context &context);
    std::pair<long, long> changeVisualEditor(std::u16string &text);
    float currentAngle() const;
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);
    void takeView(const VideoView &view);

    std::array<bool, 3> m_toggle{false, false, false};

    float m_coeffW = 1, m_coeffH = 1;
    PointF m_zoomMove{0.f, 0.f}, m_zoomScale{1.f, 1.f};
    mutable PointF m_from, m_to;
    PointF m_lastmove{0.f, 0.f};
    mutable double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    mutable int m_oldtime = 0;
    int m_start = 0, m_end = 0;
    bool m_replaceTagsInCursorPosition = true;
    std::u16string m_currentLineText;
    transform::TagFind m_find;
    std::u16string m_editorText;
    bool m_editorIsTranslation = false;
    std::optional<core::LineId> m_editing;

    // RotationZ members (Visuals.h:406-433).
    bool m_isOrg = false;
    mutable PointF m_org{0.f, 0.f};
    PointF m_lastOrg{0.f, 0.f};
    std::array<PointF, 2> m_twoPoints{};
    PointF m_diffs{0.f, 0.f};
    float m_lastAngle = 0.f;
    bool m_hasTwoPoints = false;
    std::array<bool, 2> m_hover{false, false};
    std::array<bool, 2> m_visibility{false, false};
    bool m_preserveProportions = false;
    bool m_changeAllTags = false;
    bool m_isfirst = true;
    int m_grabbed = -1;
};

class RotationXYTool : public VisualTool {
public:
    Family family() const override { return Family::RotationXY; }
    void reset(VisualHost &host) override;
    bool keepsStateAfterCommit() const override { return true; }
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // RotationXYItem::GetItemToggled: 0 with "Changing all X/Y-rotation
    // tags", else -1.
    int toggled() const { return m_toggled; }
    void setToggled(int toggled, VisualHost *host = nullptr);

    PointF from() const { return m_from; }
    PointF to() const { return m_to; }
    PointF firstmove() const { return m_firstmove; }
    int type() const { return m_type; }
    int an() const { return m_an; }
    PointF org() const { return m_org; }
    PointF angle() const { return m_angle; }
    PointF oldAngle() const { return m_oldAngle; }
    std::pair<long, long> editorCaret() const { return m_find.selection(); } // as ScaleTool's

private:
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    void setCurVisual(VisualHost &host);
    void setVisual(bool dummy, VisualHost &host);
    void changeVisualLine(std::u16string &text, const core::LineRecord &line, const transform::Context &context);
    std::pair<long, long> changeVisualEditor(std::u16string &text);
    bool beginEdit(VisualHost &host);
    bool finishEdit(VisualHost &host);
    void takeView(const VideoView &view);

    int m_toggled = -1;

    float m_coeffW = 1, m_coeffH = 1;
    PointF m_zoomMove{0.f, 0.f}, m_zoomScale{1.f, 1.f};
    mutable PointF m_from, m_to;
    PointF m_firstmove{0.f, 0.f};
    PointF m_lastmove{0.f, 0.f};
    mutable double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    mutable int m_oldtime = 0;
    int m_start = 0, m_end = 0;
    bool m_replaceTagsInCursorPosition = true;
    std::u16string m_currentLineText;
    transform::TagFind m_find;
    std::u16string m_editorText;
    bool m_editorIsTranslation = false;
    std::optional<core::LineId> m_editing;

    // RotationXY members (Visuals.h:435-456).
    bool m_isOrg = false;
    PointF m_angle{0.f, 0.f};
    PointF m_oldAngle{0.f, 0.f};
    mutable PointF m_org{0.f, 0.f};
    PointF m_lastOrg{0.f, 0.f};
    int m_type = 0;
    int m_an = 2;
    int m_diffsX = 0, m_diffsY = 0; // wxPoint
    bool m_changeAllTags = false;
};

// RotationXY::DrawVisual's projection (VisualRotationXY.cpp:37-181): a
// point of the tool's 3D model in the video window's device pixels, for the
// window `width` x `height` (GetWindowSize without the panel), the \org at
// `org`, the Line's position at `from` and the angles (`angle`.x the Y
// rotation, .y the X rotation); nullopt behind the near plane.
struct XYProjection {
    float width = 0, height = 0;
    PointF org, from, angle;
};
std::optional<PointF> projectXY(const XYProjection &p, float x, float y, float z);
// The grid, axes and arrows as legacy drew them (44 grid lines, the axis
// lines and the three arrow cones), clipped at the near plane.
void drawXYGrid(Overlay &out, const XYProjection &p, int an);

} // namespace hikari::application::visual
