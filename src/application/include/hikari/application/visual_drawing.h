#pragma once

// Line text is searched from its start, not from the Line editor's caret.

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

// Named tags in override blocks; with plainText or inside a drawing, the text between blocks as
// "plain" / "pvector".
struct Tag {
    std::u16string name;
    std::u16string value;
    std::size_t start = 0; // value offset in the text
};
std::vector<Tag> parseTags(std::u16string_view text, const std::vector<std::u16string_view> &names,
                           bool plainText = false);

// The offset the alignment gives the points (\an7: none), or with withoutAlignment their size.
PointF drawingSize(int alignment, const std::vector<VectorPoint> &points, bool withoutAlignment = false);
void rotate(VectorPoint &point, float sinOfAngle, float cosOfAngle, PointF orgpivot);

struct LineDrawing {
    PointF position;         // \pos / \move start, or the default position
    PointF scale{1, 1};      // \fscx, \fscy (or the Style's) / 2^(\p-1)
    int alignment = 2;       // truncated to a byte
    std::optional<int> vectorScale; // nullopt: the tool keeps its own
    std::array<double, 7> move{};   // x1 y1 x2 y2 t1 t2, then the count read
    std::u16string body;
    float frz = 0;           // \frz / \fr, else the Style's Angle
    std::optional<PointF> org;
    int start = 0, end = 0;  // ms
};
// `editorText` gives position, scale, \frz and \org; `lineText` the drawing. `moveTable` is the
// tool's table from the previous call.
LineDrawing readLine(const core::Document &document, const core::LineRecord &line, std::u16string_view editorText,
                     std::u16string_view lineText, int scriptWidth, int scriptHeight,
                     const std::array<double, 7> &moveTable);

// MUST KILL: writes the Line's times into `move` when it has fewer than six values
PointF movePosition(std::array<double, 7> &move, int start, int end, int time);

// `body` replaces the Line's first drawing; a missing \p1, \pos at `position` and \an are added,
// plain text after a block gets its own block, and "{\p0}" closes a drawing that ends the text.
struct PutResult {
    std::u16string text;
    std::size_t start = 0; // where the body went
    // MUST KILL: `end` is the replaced length, not an end position
    int end = 0;
};
// `diff`: the length the scale tags added before the drawing.
using SetScale = std::function<void(std::u16string &text, std::size_t position, int *diff)>;
PutResult putDrawing(std::u16string text, std::u16string_view body, PointF position, int alignment,
                     const SetScale &setScale = {});

}

class DrawingTool : public VisualTool {
public:
    DrawingTool();
    Family family() const override { return Family::Drawing; }
    void reset(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    // Rereads the preset at `listSelection`, even if unchanged. Hidden, the tool reads its Line on reset.
    void presetsChanged(int listSelection, VisualHost &host, bool shown = true);

    // For the tests.
    VectorEditor &editor() { return m_editor; }
    const VectorEditor &editor() const { return m_editor; }
    VectorFrame frame(const VisualHost &host) const;
    PointF position() const { return {m_x, m_y}; }
    PointF scale() const { return m_scale; }
    int alignment() const { return m_alignment; }
    float frz() const { return m_frz; }
    PointF org() const { return m_org; }
    int vectorScale() const { return m_vectorScale; }
    const std::array<double, 7> &moveValues() const { return m_move; }
    int shapeSelection() const { return m_shapeSelection; } // 0: "Choose"
    int shape() const { return m_shape; } // -1: none
    const std::vector<VectorPoint> &shapePoints() const { return m_shapePoints; }
    PointF shapeSize() const { return m_shapeSize; }
    PointF shapeScale() const { return m_shapeScale; }
    PointF rectangle(int i) const { return m_rect[i]; }
    bool rectangleVisible() const { return m_rectVisible; }
    int shapeGrabbed() const { return m_shapeGrabbed; }
    // The drawing as it would be written now.
    std::u16string body() const;

    // Only `fromToolbar` rereads the Line on a return to "Choose".
    void changeShape(int listSelection, bool fromToolbar, VisualHost &host);

private:
    void readActive(VisualHost &host);
    void apply(VisualHost &host, bool commit);
    VectorEditor::Callbacks callbacks(VisualHost &host);
    void syncMove(const VisualHost &host) const;
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
    PointF drawingAnchor() const;

    VectorEditor m_editor;
    PointF m_scale{1, 1};
    int m_alignment = 0;
    // MUST KILL: const overlay() moves these for a \move drawing
    mutable float m_x = 0, m_y = 0;
    float m_frz = 0;
    PointF m_org{0, 0};
    int m_vectorScale = 1;
    mutable std::array<double, 7> m_move{};
    int m_start = 0, m_end = 0;
    int m_shapeSelection = 0;

    int m_shape = -1;
    static constexpr int kNoPresetRead = -2;
    ShapePreset m_current;
    std::vector<VectorPoint> m_shapePoints;
    PointF m_shapeSize{0, 0};
    PointF m_shapeScale{1, 1};
    PointF m_rect[2]{};
    bool m_rectVisible = false;
    int m_shapeGrabbed = -1;
    int m_diffX = 0, m_diffY = 0;

    // Set by the first sample after a reset; later samples replace only the drawing.
    bool m_placed = false;
    std::size_t m_placedStart = 0, m_placedEnd = 0;
    bool m_began = false;
    bool m_inKey = false, m_keyCommit = false;
};

}
