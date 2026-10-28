#pragma once

// T5: the drawing tool (legacy Shapes over DrawingAndClip as VECTORDRAW,
// HikariSub/VisualDrawingShapes.cpp and VisualClips.cpp at 20d647c4;
// docs/qt/visual-tools.md, "Drawing and shape presets"). With the toolbar's
// shape list on "Choose" the tool edits the Line's \p drawing with T4's
// vector point editor (visual_vector.h): the drawing's position, \p scale,
// \fscx / \fscy and \frz place its points. With a shape chosen a drag draws
// a rectangle and the shape preset (shape_presets.h) is written scaled into
// it, replacing the Line's drawing.
//
// A Line's text is read and written as legacy did, through the ported
// TagFindReplace::FindTag (core::legacy::TagEditor), from the start of the
// text (approved T4-clip-read-start: the visual tools do not follow the
// Line editor's caret).

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

namespace drawing {

// Dialogue::ParseTags(tags, n, plainText) (SubsDialogue.cpp:1086-1158): the
// named tags in override blocks (name, value, where the value starts) and,
// with plainText or while a \p other than \p0 is on, the text between
// blocks ("plain" / "pvector").
struct Tag {
    std::u16string name;
    std::u16string value;
    std::size_t start = 0;
};
std::vector<Tag> parseTags(std::u16string_view text, const std::vector<std::u16string_view> &names,
                           bool plainText = false);

// Visuals::CalcDrawingSize (Visuals.cpp:1206-1243): the offset the
// alignment gives a drawing's points (an7: none), or with
// withoutAlignment the points' width and height.
PointF drawingSize(int alignment, const std::vector<VectorPoint> &points, bool withoutAlignment = false);
// Visuals::RotateDrawing (Visuals.cpp:1197-1204).
void rotate(VectorPoint &point, float sinOfAngle, float cosOfAngle, PointF orgpivot);

// What DrawingAndClip::SetCurVisual reads for VECTORDRAW (VisualClips.cpp:
// 182-296) with Visuals::GetPosnScale (Visuals.cpp:624-722), from the
// active Line's text as the Line editor shows it.
struct LineDrawing {
    PointF position;         // GetPosnScale's \pos / \move start, or the default position
    PointF scale{1, 1};      // \fscx, \fscy (or the Style's) / 2^(\p-1)
    int alignment = 2;       // \an or the Style's (legacy byte)
    std::optional<int> vectorScale; // the \p scale read (else the tool keeps its own)
    std::array<double, 7> move{};   // moveValues: x1 y1 x2 y2 t1 t2, the count read
    std::u16string body;     // the drawing: ParseTags' first "pvector" after the first tag
    float frz = 0;           // \frz / \fr, else the Style's Angle
    std::optional<PointF> org; // \org as GetTwoValueDouble reads it (never, legacy)
    int start = 0, end = 0;  // the Line's times in ms (Visuals::start / end)
};
// `editorText`: the text the Line editor shows (GetPosnScale, \frz, \org);
// `lineText`: the Line's text (ParseTags on tab->edit->line). `moveTable`
// is legacy's moveValues member, kept by the tool between calls.
LineDrawing readLine(const core::Document &document, const core::LineRecord &line, std::u16string_view editorText,
                     std::u16string_view lineText, int scriptWidth, int scriptHeight,
                     const std::array<double, 7> &moveTable);

// Visuals::CalcMovePos (Visuals.cpp:547-564): a \move's position at `time`
// (the table's times are made the Line's when it has fewer than six values,
// as legacy did to its member).
PointF movePosition(std::array<double, 7> &move, int start, int end, int time);

// DrawingAndClip::ChangeVectorVisual for VECTORDRAW (VisualClips.cpp:574-649):
// the drawing `body` replaces the Line's first one (a \p1, a \pos at
// `position` and an \an are added when missing, plain text after a block is
// put in a block of its own, and a "{\p0}" follows a drawing that ends the
// text). `start` and `end` are where the body went (ChangeVectorVisual's
// changePos: x the start, y legacy's end count). `setScale` runs with a
// shape chosen (Shapes::SetScale).
struct PutResult {
    std::u16string text;
    std::size_t start = 0;
    int end = 0;
};
using SetScale = std::function<void(std::u16string &text, std::size_t position, int *diff)>;
PutResult putDrawing(std::u16string text, std::u16string_view body, PointF position, int alignment,
                     const SetScale &setScale = {});

} // namespace drawing

class DrawingTool : public VisualTool {
public:
    DrawingTool();
    Family family() const override { return Family::Drawing; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // Legacy's state, for the tests.
    VectorEditor &editor() { return m_editor; }
    const VectorEditor &editor() const { return m_editor; }
    VectorFrame frame(const VisualHost &host) const;
    PointF position() const { return {m_x, m_y}; } // _x, _y
    PointF scale() const { return m_scale; }
    int alignment() const { return m_alignment; }
    float frz() const { return m_frz; }
    PointF org() const { return m_org; }
    int vectorScale() const { return m_vectorScale; }
    const std::array<double, 7> &moveValues() const { return m_move; }
    // The shape list's selection (VectorItem::shapeListSelection; 0
    // "Choose") and Shapes' own state.
    int shapeSelection() const { return m_shapeSelection; }
    int shape() const { return m_shape; } // -1: none
    const std::vector<VectorPoint> &shapePoints() const { return m_shapePoints; }
    PointF shapeSize() const { return m_shapeSize; }
    PointF shapeScale() const { return m_shapeScale; }
    PointF rectangle(int i) const { return m_rect[i]; }
    bool rectangleVisible() const { return m_rectVisible; }
    int shapeGrabbed() const { return m_shapeGrabbed; }
    // GetVisual: the drawing's text as it would be written now.
    std::u16string body() const;

    // ChangeTool's shape part (DrawingAndClip::ChangeTool, VisualClips.cpp:
    // 1477-1497, and Shapes::SetShape): the list's selection; `fromToolbar`
    // false when SetVisual calls it (blockSetCurVisual).
    void changeShape(int listSelection, bool fromToolbar, VisualHost &host);

private:
    void readActive(VisualHost &host); // SetCurVisual
    void apply(VisualHost &host, bool commit); // SetClip(!commit)
    VectorEditor::Callbacks callbacks(VisualHost &host);
    void syncMove(const VisualHost &host) const; // DrawVisual's \move position
    // Shapes (VisualDrawingShapes.cpp:355-776).
    void shapePointer(const Pointer &event, VisualHost &host);
    std::u16string shapeBody() const;
    std::u16string drawingBody() const;
    void setScaleTags(std::u16string &text, std::size_t position, int *diff) const;
    int hitTest(PointF pos, bool diff, const VisualHost &host);
    void sortPoints();
    void setDrawingScale();
    void setSquareShape(bool axisX);
    PointF pointToVideo(PointF point, const VisualHost &host) const;
    PointF pointToSubtitles(float x, float y, const VisualHost &host) const;
    PointF drawingAnchor() const; // CalcDrawingAnchor

    VectorEditor m_editor;
    PointF m_scale{1, 1};
    int m_alignment = 0;
    mutable float m_x = 0, m_y = 0; // _x, _y (DrawVisual moves a \move drawing's)
    float m_frz = 0;
    PointF m_org{0, 0};
    int m_vectorScale = 1;
    mutable std::array<double, 7> m_move{}; // moveValues
    int m_start = 0, m_end = 0;
    int m_shapeSelection = 0;

    int m_shape = -1;
    ShapePreset m_current;
    std::vector<VectorPoint> m_shapePoints;
    PointF m_shapeSize{0, 0};
    PointF m_shapeScale{1, 1};
    PointF m_rect[2]{};
    bool m_rectVisible = false;
    int m_shapeGrabbed = -1;
    int m_diffX = 0, m_diffY = 0; // DrawingAndClip::diffs as Shapes uses it

    // The single-Line preview's place (legacy dummytext / dumplaced): set by
    // the first sample after a reset, then only the drawing is replaced.
    bool m_placed = false;
    std::size_t m_placedStart = 0, m_placedEnd = 0;
    bool m_began = false;
    bool m_inKey = false, m_keyCommit = false;
};

} // namespace hikari::application::visual
