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
#include "hikari/application/legacy_timebase.h"
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

namespace hikari::application {
class TextMeasurePort; // automation_services.h
}

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
    // DoubleClick: the second press of a double click, after its Press
    // (legacy wxEVT_LEFT_DCLICK; T2's Position uses it).
    enum class Kind { Enter, Leave, Move, Press, Release, Wheel, DoubleClick };
    enum class Button { None, Left, Middle, Right };
    Kind kind = Kind::Move;
    int x = 0, y = 0;
    Button button = Button::None; // the button pressed or released
    bool leftDown = false;        // held during a move
    bool rightDown = false;       // T2: legacy RightIsDown (Move drags with either)
    bool middleDown = false;      // T3: legacy MiddleIsDown
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
// triangle strip and line strip, the clips' masks; T2). T3: a border of 0
// draws the fill alone (RotationZ's ring), a fill of 0 the border alone. T4:
// `more` holds further contours filled in the same path (non-zero: the
// rectangle clip's two fans without a seam), and `above` draws it after the
// lines (the vector points' handles, which legacy drew over the path)
// instead of before them.
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
    std::vector<OverlayPolygon> polygons;
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
    // T5: legacy's shape list: the first entry is "no choice" and the last
    // an action ("Edit"); the row's menu puts it after a separator.
    bool listEnds = false;
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
    // T6: a staged text that differs from the target's as the gesture began.
    bool changesAnyLine() const;
    // The staged text of a target, if any.
    std::optional<std::u8string> staged(core::LineId line, bool translation = false) const;
    // The staged texts put into a copy of the Document (the video's preview
    // while the gesture is open, legacy's dummy rendering).
    void applyTo(core::Document &document) const;
    // What the video renders while the gesture is open (Position::
    // ChangeMultiline / Visuals::RenderSubs on SubsGrid::GetVisible,
    // SubsGridBase.cpp:1517-1593): only the Lines shown at the video's time
    // (5 ms either side, or any not yet over while playing), the targets
    // with their staged texts while they show (VisualPosition.cpp:470-481).
    core::Document preview(const core::Document &document, std::int64_t timeMs, bool playing) const;

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
struct AllTagsSetting; // all_tags.h (T6)

// The Line warnings (lineWarning below).
enum class LineWarning { None, NotVisible, Comment };

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

    // T2: what Visuals read from the video and the Grid. The video's time
    // (VideoBox::Tell: the shown frame's start in ms), its legacy Timebase
    // (VideoBox::GetTimebase), the text measure GetTextSize uses
    // (GetLineTextExtents; none: measuring fails), the Grid's "ignore
    // filtering in some actions" (SubsGrid::ignoreFiltered) and HikariLog.
    virtual std::int64_t videoTimeMs() const { return 0; }
    virtual LegacyTimebase timebase() const { return {}; }
    virtual TextMeasurePort *textMeasure() const { return nullptr; }
    virtual bool ignoreFiltered() const { return false; }
    virtual void log(std::u16string_view text) { (void)text; }
    // T3: the Line editor's selection in the text the tools edit (legacy
    // TagFindReplace::FindTag's editor->GetSelection with one Line selected,
    // TagFindReplace.cpp:40-41), in UTF-16 code units.
    virtual std::pair<long, long> editorSelection() const { return {0, 0}; }
    // Legacy put the editor's caret at the tag it changed
    // (Visuals::SetVisual, Visuals.cpp:815); called after the commit.
    virtual void setEditorSelection(long from, long to)
    {
        (void)from;
        (void)to;
    }
    // T4: legacy wxBell (a refused point insertion) and a notice legacy
    // showed in a message box; shown without blocking.
    virtual void bell() {}
    virtual void notice(std::u16string_view text) { (void)text; }
    // T5: the drawing's shape presets (VideoToolbar::GetShapesSettings:
    // Config/ShapesSettings.txt, or legacy's defaults; null for none). A
    // \move drawing's position follows videoTimeMs.
    virtual const std::vector<ShapePreset> *shapePresets() const { return nullptr; }
    // T6: whether the Grid shows a Line (legacy Dialogue::isVisible: not
    // hidden by the Grid filter or a closed Line group). The Position
    // shifter and the all-tags tool edit only the selected Lines it shows
    // (SubsFile::GetSelections, SubsFile.cpp:503-512).
    virtual bool lineShown(core::LineId line) const
    {
        (void)line;
        return true;
    }
    // T6: the all-tags tool's definitions (VideoToolbar::GetTagsSettings:
    // Config/AllTagsSettings.txt, or legacy's defaults) and the toolbar's
    // selection in their list after a change the tool made (Shift+wheel,
    // AllTagsItem::SetItemToggled).
    virtual const std::vector<AllTagsSetting> *allTagsSettings() const { return nullptr; }
    // T6: the accelerator text ("Ctrl-,") the Line editor's hotkey `id` has
    // (Hotkeys::GetHKey with EDITBOX_HOTKEY); legacy's default unless the
    // host knows the user's.
    virtual std::string editorHotkey(int id) const;
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
    // T3: true when the tool's own commit must not reset it. The host resets
    // a tool on every new revision; legacy's Scale and rotations sent their
    // edit with the visual dummy flag, so no SetVisual followed it
    // (Visuals.cpp:791-829, SubsGridBase.cpp:1125-1157) and the tool kept
    // its state (the caret at the tag, the angles, the press point).
    virtual bool keepsStateAfterCommit() const { return false; }
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
    // A tool that overrides Visuals::Draw decides its own warning (T2's
    // Position: none while any of its Lines is visible); nullopt: the
    // active Line's (lineWarning).
    virtual std::optional<LineWarning> warning(const VisualHost &host) const
    {
        (void)host;
        return std::nullopt;
    }
    // Visuals::Draw found nothing to show and set blockevents: the host calls
    // this when it renders, or takes a pointer event, while the tool is
    // blocked. Position::Draw also ends a helper-cross drag there.
    virtual void blocked(VisualHost &host) { (void)host; }
    // The family became the active one: legacy Visuals::Get made a new tool
    // (RendererVideo::SetVisual), so the tool's own state starts over; the
    // rail's options are the toolbar's and stay.
    virtual void selected(VisualHost &host) { (void)host; }
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
    // T3: Esc with no gesture open: true when the tool dropped a pending
    // step (RotationZ's first point of the two-point angle).
    virtual bool cancelPending(VisualHost &host)
    {
        (void)host;
        return false;
    }
    virtual bool hasPending() const { return false; }
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
LineWarning lineWarning(Family family, const core::LineRecord &line, std::int64_t videoMs);
// "Line is not visible on video\nor has zero duration" /
// "Visual editing tools\ndo not work on comments" (Visuals.cpp:534-535).
std::u16string_view warningText(LineWarning warning);

} // namespace hikari::application::visual
