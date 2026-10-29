#pragma once

#include "hikari/application/visual_tools.h"
#include "hikari/core/style.h"

#include <array>
#include <map>
#include <memory>
#include <cstdint>
#include <optional>
#include <utility>
#include <string>

namespace hikari::application::visual {

namespace perspective {

// A quad's corners in script coordinates: the text's top left, top right,
// bottom right and bottom left as rendered.
using Quad = std::array<PointF, 4>;

// As libass composes them: shear about the text's top, scale about the alignment point at \pos, rotations
// about \org (Z, then X, then Y), projection from `screenZ` in front of the screen.
struct Transform {
    PointF pos{0, 0}, org{0, 0};
    double frx = 0, fry = 0, frz = 0;
    double fax = 0, fay = 0;
    double fscx = 100, fscy = 100;
    PointF bord{0, 0}, shad{0, 0}; // \xbord \ybord, \xshad \yshad
    int an = 2;
};

// At a scale of 100 (widest line by the lines' summed heights, or a drawing's points), relative to the text's start.
struct Box {
    PointF topLeft{0, 0}, bottomRight{0, 0};
    // The height libass aligns when not the ink's: a drawing whose \pbo is above its height or below 0.
    double lineHeight = -1;
    double width() const { return bottomRight.x - topLeft.x; }
    double height() const { return bottomRight.y - topLeft.y; }
    double layoutHeight() const { return lineHeight >= 0 ? lineHeight : height(); }
};

// libass's camera distance in script units: 20000 / 64 in layout pixels (LayoutResY, else the frame's
// height, else PlayResY).
double screenZ(int scriptHeight, int layoutHeight);

Quad textToQuad(const Transform &t, const Box &box, double screenZ);

enum class OrgMode { Center = 0, NoFax = 1, Keep = 2 };

// `from` gives the alignment, Keep mode's \org, and the scale and border the new border is relative to.
// nullopt for a degenerate quad.
std::optional<Transform> quadToTransform(const Quad &quad, const Box &box, OrgMode mode, const Transform &from,
                                         double screenZ);

// Convex with its corners in order: the diagonals cross inside it.
bool convex(const Quad &quad);
// Where the diagonals cross.
PointF centre(const Quad &quad);
// Plane coordinates: (0, 0) .. (1, 1) at the corners, through the projective map of the unit square.
PointF toUV(const Quad &quad, PointF xy);
PointF fromUV(const Quad &quad, PointF uv);
// The quad of the rectangle `a` .. `b` in `quad`'s plane coordinates.
Quad subQuad(const Quad &quad, PointF a, PointF b);
// Whether the plane whose rectangle c1 .. c2 is `inner` has a finite, convex outer quad on the near side
// of the projective horizon.
bool planeHolds(const Quad &inner, PointF c1, PointF c2);

// The first block's tags (the last of each), the first \pos, \move or \org anywhere, the first \an (or \a),
// else the Style's values and the default position.
struct LineState {
    Transform transform;
    Box box;
    bool hasMove = false;
    std::array<double, 4> move{0, 0, 0, 0}; // \move's points
    std::u16string moveTimes;               // its times, as written (",t1,t2" or empty)
    bool stacks = false; // no \pos, \move, \org or \t: libass moves it clear of others
    bool ownOrg = false; // else libass turns it about its position
};
// Valid while the session's revision holds.
class StyleCache {
public:
    struct Decoded;
    std::shared_ptr<const Decoded> get(const VisualHost &host);

private:
    std::uint64_t m_session = 0, m_revision = 0;
    const void *m_document = nullptr;
    std::shared_ptr<const Decoded> m_decoded;
};
std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line);
std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line, StyleCache &styles);
// readLine without the box.
LineState readTags(const VisualHost &host, const core::LineRecord &line);

// Writes `t` into the first block (adding one): \pos (or \move shifted by `t`.pos less `movedFrom`), \org, \fax,
// \fsc, \fr and \bord/\shad per axis; conflicting leading tags are removed, tags at the Style's value left out,
// \t(...) untouched. A \move Line without \org keeps libass's moving origin (no \org written) when `t` does not
// turn it or turns it about `t`.pos, unless `explicitOrg`.
std::u16string writeTransform(std::u16string_view text, const Transform &t, const core::StyleValues &style,
                              std::optional<PointF> movedFrom = std::nullopt, bool explicitOrg = false);

}

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

    // For tests; script coordinates.
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
    // Keeps the other quad per the plane and the lock; false when the result would fold.
    bool applyCorner(int corner, PointF to, bool fromCentre, VisualHost &host);
    // A locked outer quad's corner: it resizes the plane and changes no Line.
    bool planeOnly(int corner) const;
    // `explicitOrg`: the \org was typed or dragged, so a \move Line takes it as it is.
    bool applyInner(const perspective::Quad &inner, VisualHost &host, bool explicitOrg = false);
    // libass turns a \move Line without \org about its moving position, which no fixed \org can hold.
    bool keepsMovingOrigins(const perspective::Transform &t, VisualHost &host, bool explicitOrg) const;
    // False when a target changes outline or shadow after its leading blocks; a scale change is then refused.
    bool scalesEveryOutline(const perspective::Transform &t, VisualHost &host) const;
    // The saved plane, else the largest default that holds for the inner quad.
    void fitPlane() const;
    perspective::Quad startQuad(bool outer) const;
    PointF snap(PointF p, int skip) const;
    PointF axisLock(int corner, PointF delta) const;
    bool move(PointF delta, VisualHost &host);
    void rememberPlane(VisualHost &host);
    // The saved outer quad of the active Line, in the inner quad's plane.
    void restorePlane();
    // MUST KILL: const follow() and fitPlane() rewrite the mutable read state
    // Moves the quad where the video's time puts it (a \move, the stacking).
    void follow(const VisualHost &host) const;

    // Stays when the family changes.
    bool m_plane = false, m_lock = false, m_grid = false;
    perspective::OrgMode m_orgMode = perspective::OrgMode::Center;

    mutable bool m_valid = false;
    mutable perspective::LineState m_state;
    mutable perspective::Quad m_inner{};
    mutable std::int64_t m_time = 0; // the video time m_state was read at
    // The session's serial and revision m_state was read at.
    mutable std::optional<std::pair<std::uint64_t, std::uint64_t>> m_read;
    static std::optional<std::pair<std::uint64_t, std::uint64_t>> readAt(const VisualHost &host);
    // The inner quad's rectangle in the outer quad's plane coordinates.
    mutable PointF m_c1{0.25f, 0.25f}, m_c2{0.75f, 0.75f};
    double m_screenZ = 312.5;
    std::optional<core::LineId> m_line;
    std::uint64_t m_session = 0; // EditSession::serial of m_line and m_planes
    mutable perspective::StyleCache m_styles;
    // The outer quads, per Line, in memory only.
    mutable std::map<core::LineId, perspective::Quad> m_planes;

    // Script coordinates.
    Drag m_drag = Drag::None;
    int m_corner = -1;
    int m_selected = -1;
    PointF m_press{0, 0};
    perspective::Quad m_startInner{};
    PointF m_startC1{0, 0}, m_startC2{0, 0};
    PointF m_startOrg{0, 0};
    perspective::Transform m_startTransform;
    bool m_orgEdited = false; // the open gesture moved \org itself
    bool m_valueEdit = false;
    bool m_keyEdit = false;
    perspective::Transform m_written; // what the open gesture writes
};

}
