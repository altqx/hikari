#pragma once

// VT3: the Perspective tool (docs/qt/visual-tools.md, card #273), after
// arch1t3cht's Aegisub perspective tool (visual_tool_perspective.cpp,
// feature_09/10). Legacy had none.
//
// The tool draws the active Line's text box as rendered: the inner quad.
// Dragging its corners writes the \org, \pos, \frx, \fry, \frz, \fax, \fscx
// and \fscy that make libass render the box onto the quad, with \xbord,
// \ybord, \xshad and \yshad scaled along so the outline keeps its proportion.
// "Show surrounding plane" adds the outer quad, a rectangle of the same
// plane: matched to a rectangle in the shot, it holds the perspective while
// the inner quad is placed in it; "Lock outer quad" swaps the two roles.
// The \org mode chooses \org: the quad's centre, where \fax can be zero
// (for Lines with line breaks), or kept where it is (its handle dragged).
//
// Gestures (owner-approved, #273): a corner drag; Shift locks it to the
// nearer of the plane's axes; Ctrl snaps it to another handle's x or y
// within 10 units (the vector editor's FindSnapPoint rule); Alt scales from
// the centre (the opposite corner mirrors it); a drag inside the inner quad
// moves it (with the plane shown and unlocked, within the plane; inside the
// outer quad alone both move); a double click puts the nearest corner at
// the pointer; the arrows nudge the last corner grabbed (the whole quad
// before one is), Shift a tenth. Each drag, double click or nudge is one
// "Visual perspective tool" step on every picked Line, all getting the
// active Line's tags; a Line with \move keeps its motion (both points move
// by the change in position).

#include "hikari/application/visual_tools.h"
#include "hikari/core/style.h"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace hikari::application::visual {

namespace perspective {

// A quad's corners in script coordinates: the text's top left, top right,
// bottom right and bottom left as rendered.
using Quad = std::array<PointF, 4>;

// The tags the quad is made of, as libass reads them (ass_render.c's
// calc_transform_matrix): the shear about the text's top, the scale about
// the alignment point at \pos, the rotations about \org (Z, then X, then Y)
// and the projection from `screenZ` in front of the screen.
struct Transform {
    PointF pos{0, 0}, org{0, 0};
    double frx = 0, fry = 0, frz = 0;
    double fax = 0, fay = 0;
    double fscx = 100, fscy = 100;
    PointF bord{0, 0}, shad{0, 0}; // \xbord \ybord, \xshad \yshad
    int an = 2;
};

// The text's box at a scale of 100 (the widest line by the sum of the
// lines' heights, or a drawing's points), relative to where the text starts.
struct Box {
    PointF topLeft{0, 0}, bottomRight{0, 0};
    double width() const { return bottomRight.x - topLeft.x; }
    double height() const { return bottomRight.y - topLeft.y; }
};

// libass's camera distance in script units: 20000 / 64 in the layout's
// pixels (LayoutResY, else the rendered frame's height, else PlayResY).
double screenZ(int scriptHeight, int layoutHeight);

// Where the Line's box renders.
Quad textToQuad(const Transform &t, const Box &box, double screenZ);

enum class OrgMode { Center = 0, NoFax = 1, Keep = 2 };

// The tags rendering `box` onto `quad` (arch1t3cht's InnerToText): `from`
// gives the alignment, the \org kept in Keep mode and the scale and border
// the new scale's border is relative to. nullopt when a value is not finite
// (a degenerate quad).
std::optional<Transform> quadToTransform(const Quad &quad, const Box &box, OrgMode mode, const Transform &from,
                                         double screenZ);

// Whether the quad is convex with its corners in order (its diagonals cross
// inside it); a drag that would fold it is not taken.
bool convex(const Quad &quad);
// Where the diagonals cross.
PointF centre(const Quad &quad);
// The plane's coordinates: (0, 0) .. (1, 1) at the corners, through the
// projective map of the unit square onto the quad, and back.
PointF toUV(const Quad &quad, PointF xy);
PointF fromUV(const Quad &quad, PointF uv);
// The quad of the rectangle `a` .. `b` in `quad`'s plane coordinates.
Quad subQuad(const Quad &quad, PointF a, PointF b);

// A Line's transform and box as the tool reads them: the tags of its first
// block (the last of each), its first \pos, \move (at `timeMs` from its
// start) or \org anywhere, its first \an (or \a), else its Style's values
// and the default position. The box measures the text with the Style at a
// scale of 100 and the block's \fs and \fn, through `measure` (nullopt
// without one), or a drawing's points.
struct LineState {
    Transform transform;
    Box box;
    bool hasMove = false;
    std::array<double, 4> move{0, 0, 0, 0}; // \move's points
    std::u16string moveTimes;               // its times, as written (",t1,t2" or empty)
};
std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line);
// The same without the box.
LineState readTags(const VisualHost &host, const core::LineRecord &line);

// `text` with `t` written into its first block (a block put in when it has
// none): \pos (or the Line's \move moved by `t`.pos less `movedFrom`), \org,
// \fax, \fscx, \fscy, \frz, \frx, \fry, \xbord, \ybord, \xshad, \yshad, the
// block's \fay, \fr, \bord and \shad removed, a tag at its Style's value
// (\fax, \frx and \fry at 0) left out. Tags inside \t(...) are not touched.
std::u16string writeTransform(std::u16string_view text, const Transform &t, const core::StyleValues &style,
                              std::optional<PointF> movedFrom = std::nullopt);

} // namespace perspective

class PerspectiveTool : public VisualTool {
public:
    Family family() const override { return Family::Perspective; }
    void reset(VisualHost &host) override;
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    bool setValue(const std::string &name, const std::u16string &text, VisualHost &host,
                  ValueEdit edit = ValueEdit::Commit) override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // The state, for tests (script coordinates).
    bool hasQuad() const { return m_valid; }
    const perspective::Quad &inner() const { return m_inner; }
    perspective::Quad outer() const;
    const perspective::Transform &transform() const { return m_state.transform; }
    bool planeShown() const { return m_plane; }
    bool outerLocked() const { return m_lock; }
    bool gridShown() const { return m_grid; }
    perspective::OrgMode orgMode() const { return m_orgMode; }
    // The corner the arrows nudge: 0-3 inner, 4-7 outer, -1 the whole quad.
    int selectedCorner() const { return m_selected; }

private:
    enum class Drag { None, Inner, Outer, Org, Body, Plane };
    bool beginEdit(VisualHost &host);
    void stageAll(VisualHost &host);
    bool finish(VisualHost &host);
    // Applies a new inner or outer quad (a drag's or a nudge's), keeping the
    // other per the plane and the lock; false when the result would fold.
    bool applyCorner(int corner, PointF to, bool fromCentre, VisualHost &host);
    bool applyInner(const perspective::Quad &inner, VisualHost &host);
    perspective::Quad startQuad(bool outer) const;
    PointF snap(PointF p, int skip) const;
    PointF axisLock(int corner, PointF delta) const;
    void move(PointF delta, VisualHost &host);
    void rememberPlane(VisualHost &host);

    // The toolbar's state (stays when the family changes).
    bool m_plane = false, m_lock = false, m_grid = false;
    perspective::OrgMode m_orgMode = perspective::OrgMode::Center;

    // The active Line as read, the quads drawn and the plane: the inner
    // quad's rectangle c1 .. c2 in the outer quad's plane coordinates.
    bool m_valid = false;
    perspective::LineState m_state;
    perspective::Quad m_inner{};
    PointF m_c1{0.25f, 0.25f}, m_c2{0.75f, 0.75f};
    double m_screenZ = 312.5;
    std::optional<core::LineId> m_line;
    // The outer quads of the session (VT3: kept per Line in memory, not saved).
    std::map<core::LineId, std::pair<PointF, PointF>> m_planes;

    // The drag (script coordinates).
    Drag m_drag = Drag::None;
    int m_corner = -1;
    int m_selected = -1;
    PointF m_press{0, 0};
    perspective::Quad m_startInner{};
    PointF m_startC1{0, 0}, m_startC2{0, 0};
    PointF m_startOrg{0, 0};
    perspective::Transform m_startTransform;
    bool m_valueEdit = false;
    bool m_keyEdit = false;
    perspective::Transform m_written; // what the open gesture writes
};

} // namespace hikari::application::visual
