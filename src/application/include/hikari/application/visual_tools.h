#pragma once

// T1: the visual tools' shared parts (docs/qt/visual-tools.md): the eleven
// families of legacy VideoToolbar's rail, the interface each tool implements,
// the gesture transaction every tool's edit goes through and the batch
// picker. A tool sees the Video panel through a VisualHost, which owns the
// view's transform, the editing target and the one open gesture.
//
// Adding a tool (T2-T6) is one class implementing VisualTool and one line in
// makeVisualTool (visual_tools.cpp); the rail, the pointer and key routing,
// Esc, the warnings and the overlay drawing need no change.

#include "hikari/application/edit_session.h"
#include "hikari/application/visual_view.h"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// Legacy Visuals.h:56-68, in the rail's order.
enum class Family : int {
    Crosshair = 0, // CROSS
    Position,      // CHANGEPOS
    Move,          // MOVE
    Scale,         // SCALE
    RotationZ,     // ROTATEZ
    RotationXY,    // ROTATEXY
    RectangleClip, // CLIPRECT
    VectorClip,    // VECTORCLIP
    Drawing,       // VECTORDRAW
    PositionShifter, // MOVEALL
    Hydra,         // ALL_TAGS
};
inline constexpr int kFamilyCount = 11; // VideoToolbar::toolsSize

struct FamilyInfo {
    Family family;
    std::string_view icon;    // VideoToolbar.cpp's bitmap name
    std::string_view tooltip; // VideoToolbar.cpp:44-54
    std::string_view history; // SubsFile.cpp:228-238: the edit's history name
};
const std::array<FamilyInfo, kFamilyCount> &families();
const FamilyInfo &familyInfo(Family family);

// A pointer event over the video window, in its device pixels (legacy
// wxMouseEvent::GetX/GetY on VideoBox).
struct Pointer {
    enum class Kind { Enter, Leave, Move, Press, Release, Wheel };
    enum class Button { None, Left, Middle, Right };
    Kind kind = Kind::Move;
    int x = 0, y = 0;
    Button button = Button::None; // the button pressed or released
    bool leftDown = false;        // held during a move
    bool control = false, shift = false, alt = false;
    int wheelSteps = 0;
};

// A key while the video has focus. A nudge commits on its release.
struct Key {
    int key = 0; // Qt::Key
    bool release = false;
    bool autoRepeat = false;
    bool control = false, shift = false, alt = false;
};

// What a tool draws over the frame, in device pixels of the video window.
struct OverlayLine {
    PointF from, to;
    float width = 1;
    std::uint32_t argb = 0xFFFFFFFF;
};
struct OverlayText {
    IntRect rect; // drawn from its top-left
    std::u16string text;
    std::uint32_t argb = 0xFFFFFFFF;
    bool outline = true; // a one-pixel black outline (legacy DRAWOUTTEXT)
    int pixelSize = 0;   // 0: the label font (VisualHost::measureLabel)
};
struct OverlayCircle {
    PointF centre;
    float radius = 0;
    std::uint32_t argb = 0xFFFFFFFF;
    bool filled = false;
};
// A filled polygon with a one-pixel border (legacy DrawRect's and DrawArrow's
// triangle strip and line strip, the clips' masks). T4: `more` holds further
// contours filled in the same path (non-zero: the rectangle clip's two fans
// without a seam), and `above` draws it after the lines (the vector points'
// handles, which legacy drew over the path) instead of before them.
struct OverlayPolygon {
    std::vector<PointF> points;
    std::uint32_t fill = 0;
    std::uint32_t border = 0xFFFFFFFF; // 0: none
    std::vector<std::vector<PointF>> more;
    bool above = false;
};
// Drawn in this order: polygons, lines, circles, polygons `above`, texts.
struct Overlay {
    std::vector<OverlayLine> lines;
    std::vector<OverlayCircle> circles;
    std::vector<OverlayText> texts;
    std::vector<OverlayPolygon> polygons; // T4
    bool empty() const { return lines.empty() && circles.empty() && texts.empty() && polygons.empty(); }
};

// A numeric value a tool shows below the canvas (the rail's keyboard and
// numeric alternative). Editable ones are set through VisualTool::setValue.
struct ToolValue {
    std::string name;
    std::u16string label;
    std::u16string text;
    bool editable = false;
};

// A tool's own option on the rail's second row (legacy VideoToolbar's
// VisualItem for the family, T2-T6): a toggle with its icon role, a choice,
// or (T4) an action button. setOption takes 0/1 for a toggle, the index for
// a choice and 1 for an action.
struct ToolOption {
    enum class Kind { Toggle, Choice, Action };
    std::string name;
    Kind kind = Kind::Toggle;
    std::string iconRole;     // the K1 set's role (toggles, actions, a choice's menu button)
    std::u16string tooltip;   // legacy's help text
    bool checked = false;     // a toggle's state
    bool enabled = true;      // legacy's greyed icons
    std::vector<std::u16string> choices;
    int index = 0;            // a choice's selection
};

// One gesture's edit (docs/qt/proposals/edit-transactions.md, accepted on
// #55): it stages new texts for its target Lines while the pointer or key is
// down and commits them on release as one history step named for the tool;
// cancelling (Esc) drops them. Nothing reaches the Document, the pending
// draft or the history until the commit, so a cancel leaves the pre-gesture
// draft as it was. Its targets are fixed when it begins: a later change of
// the batch picker, selection or active Line never retargets it.
class Gesture {
public:
    // Refused (and nothing begins) for a protected reference, a Document a
    // macro holds, no targets or a target that no longer exists.
    static std::expected<Gesture, CommandRefusal> begin(const EditSession &session, std::vector<core::LineId> targets,
                                                        std::string history);

    const std::vector<core::LineId> &targets() const { return m_targets; }
    const std::string &history() const { return m_history; }
    // A target as the gesture began: the committed Line with the pending
    // draft applied when the draft is on it.
    const core::LineRecord &before(core::LineId line) const;
    // Stages a target's text, or its translation (TLMode's translated role).
    void stage(core::LineId line, std::u8string text, bool translation = false);
    bool hasChanges() const { return !m_staged.empty(); }
    // The staged text of a target, if any.
    std::optional<std::u8string> staged(core::LineId line, bool translation = false) const;
    // The staged texts put into a copy of the Document (the video's preview
    // while the gesture is open, legacy's dummy rendering).
    void applyTo(core::Document &document) const;

    // One history step with every staged text. A pending draft on a target
    // is committed first, as every command does (its own step); the gesture
    // is refused when anything else changed the Document since it began.
    // Without staged changes nothing is recorded.
    std::expected<void, CommandRefusal> commit(EditSession &session) const;

private:
    Gesture() = default;
    std::vector<core::LineId> m_targets;
    std::string m_history;
    std::uint64_t m_revision = 0;
    std::map<core::LineId, core::LineRecord> m_before;
    std::map<std::pair<core::LineId, bool>, std::u8string> m_staged;
};

// The batch picker (accepted on #55): the Lines a batch tool edits are
// picked explicitly and kept by identity, independent of the active Line
// and of later Grid selections. With nothing picked a tool edits the active
// Line alone.
class BatchPicker {
public:
    void pick(std::vector<core::LineId> lines);
    void clear() { m_picked.clear(); }
    const std::vector<core::LineId> &picked() const { return m_picked; }
    // The picked Lines that still exist, in Document order; the active Line
    // when none is picked (or none of them remains).
    std::vector<core::LineId> targets(const EditSession &session) const;

private:
    std::vector<core::LineId> m_picked;
};

class VisualTool;
struct ShapePreset; // shape_presets.h (T5)

// The Video panel as a tool sees it.
class VisualHost {
public:
    virtual ~VisualHost() = default;
    virtual const VideoView &view() const = 0;
    // The editing target (null without one) and its active Line.
    virtual const EditSession *session() const = 0;
    virtual std::optional<core::LineId> activeLine() const = 0;
    // The batch picker's targets for a new gesture.
    virtual std::vector<core::LineId> batchTargets() const = 0;
    // The open gesture: beginGesture refuses while one is open.
    virtual std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> targets,
                                                                  std::string history) = 0;
    virtual Gesture *gesture() = 0;
    // Commits the open gesture (one step) and closes it.
    virtual std::expected<void, CommandRefusal> commitGesture() = 0;
    virtual void cancelGesture() = 0;
    // The crosshair label's size in device pixels (legacy GetTextExtent of
    // the program font + 4, or the D3DX font's DT_CALCRECT).
    virtual std::pair<int, int> measureLabel(std::u16string_view text) const = 0;
    // The tool's drawing or values changed.
    virtual void toolChanged() = 0;
    // T4: legacy wxBell (a refused point insertion) and a notice legacy
    // showed in a message box; shown without blocking.
    virtual void bell() {}
    virtual void notice(std::u16string_view text) { (void)text; }
    // T5: the video's time (VideoBox::Tell: the shown frame's start in ms; a
    // \move drawing's position follows it) and the drawing's shape presets
    // (VideoToolbar::GetShapesSettings: Config/ShapesSettings.txt, or
    // legacy's defaults; null for none).
    virtual std::int64_t videoTimeMs() const { return 0; }
    virtual const std::vector<ShapePreset> *shapePresets() const { return nullptr; }
};

// One visual family. The host gives a tool only the events legacy's
// Visuals got: pointer events over the video with a video open, keys while
// the video has focus, and resets when the view, script or Line changed.
class VisualTool {
public:
    virtual ~VisualTool() = default;
    virtual Family family() const = 0;
    // Legacy SetCurVisual: the view, script resolution or active Line changed.
    virtual void reset(VisualHost &host) { (void)host; }
    virtual void pointer(const Pointer &event, VisualHost &host) = 0;
    // True when the tool used the key (a nudge: begin on press, commit on release).
    virtual bool key(const Key &event, VisualHost &host)
    {
        (void)event;
        (void)host;
        return false;
    }
    virtual Overlay overlay(const VisualHost &host) const = 0;
    virtual std::vector<ToolValue> values(const VisualHost &host) const
    {
        (void)host;
        return {};
    }
    virtual bool setValue(const std::string &name, const std::u16string &text, VisualHost &host)
    {
        (void)name;
        (void)text;
        (void)host;
        return false;
    }
    // Legacy Visuals::Draw skipped these tools' warning (VisualCross overrides
    // Draw); the others are blocked outside their Line's time or on comments.
    virtual bool warnsOutsideLine() const { return family() != Family::Crosshair; }
    // The rail's second row for the family (legacy VisualItem).
    virtual std::vector<ToolOption> options(const VisualHost &host) const
    {
        (void)host;
        return {};
    }
    virtual bool setOption(const std::string &name, int value, VisualHost &host)
    {
        (void)name;
        (void)value;
        (void)host;
        return false;
    }
    // T4: Lines the video renders after the Document's while the tool is on
    // (legacy Visuals::AppendClipMask: the vector clip's mask).
    virtual std::vector<core::LineRecord> previewLines(const VisualHost &host) const
    {
        (void)host;
        return {};
    }
};

// The tool for a family: nullptr while its card (T2-T6) has not landed.
std::unique_ptr<VisualTool> makeVisualTool(Family family);

// Legacy Visuals::Draw / DrawWarning (Visuals.cpp:503-545): a tool other
// than the crosshair works only on a Dialogue Line visible at the video's
// time (start <= time < end); Vector clip and Drawing work on comments too.
enum class LineWarning { None, NotVisible, Comment };
LineWarning lineWarning(Family family, const core::LineRecord &line, std::int64_t videoMs);
// "Line is not visible on video\nor has zero duration" /
// "Visual editing tools\ndo not work on comments" (Visuals.cpp:534-535).
std::u16string_view warningText(LineWarning warning);

} // namespace hikari::application::visual
