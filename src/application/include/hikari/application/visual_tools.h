#pragma once

// A tool sees the Video panel through a VisualHost, which owns the view's
// transform, the editing target and the one open gesture.

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
class TextMeasurePort;
}

namespace hikari::application::visual {

// HikariSub legacy's family numbers, then Perspective.
enum class Family : int {
    Crosshair = 0,
    Position,
    Move,
    Scale,
    RotationZ,
    RotationXY,
    RectangleClip,
    VectorClip,
    Drawing,
    PositionShifter,
    Hydra,
    Perspective,
};
inline constexpr int kLegacyFamilyCount = 11;
inline constexpr int kFamilyCount = 12;

struct FamilyInfo {
    Family family;
    std::string_view icon;
    std::string_view tooltip;
    std::string_view history; // the edit's history name
    std::string_view hotkey;  // the Video command choosing it
};
const std::array<FamilyInfo, kFamilyCount> &families();
const FamilyInfo &familyInfo(Family family);
// Perspective comes after RotationXY.
const std::array<Family, kFamilyCount> &railOrder();
// The family a VIDEO_TOOL_* command chooses.
std::optional<Family> familyOfHotkey(std::string_view symbol);

// A pointer event over the video window, in its device pixels.
struct Pointer {
    // DoubleClick: the second press of a double click, after its Press.
    enum class Kind { Enter, Leave, Move, Press, Release, Wheel, DoubleClick };
    enum class Button { None, Left, Middle, Right };
    Kind kind = Kind::Move;
    int x = 0, y = 0;
    Button button = Button::None; // the button pressed or released
    bool leftDown = false;        // held during a move
    bool rightDown = false;
    bool middleDown = false;
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

// The arrow keys (Qt::Key values; the application layer does not link Qt).
namespace keys {
inline constexpr int Left = 0x01000012, Up = 0x01000013, Right = 0x01000014, Down = 0x01000015;
}

// -1, 0 or 1 on each axis for `key`'s arrow; nullopt for another key, with Ctrl,
// or with Alt unless `alt`. The tool applies Shift's tenth.
std::optional<PointF> nudgeDirection(const Key &key, bool alt = false);
// Whether `key` is an arrow, whatever the modifiers (a nudge's release).
bool isArrowKey(int key);

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
    bool outline = true; // a one-pixel black outline
    int pixelSize = 0;   // 0: the label font (VisualHost::measureLabel)
};
struct OverlayCircle {
    PointF centre;
    float radius = 0;
    std::uint32_t argb = 0xFFFFFFFF;
    bool filled = false;
};
// A filled polygon with a one-pixel border; a border or fill of 0 draws only the
// other. `more`: further contours filled in the same path (non-zero). `above`:
// drawn after the lines instead of before them.
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
    // Device pixels per HikariSub legacy pixel, so handles keep their on-screen size.
    float unit = 1;
    bool empty() const { return lines.empty() && circles.empty() && texts.empty() && polygons.empty(); }
};

// A numeric value in the inspector bar. Editable ones are set through
// VisualTool::setValue, typed (Enter) or scrubbed (`step` per pixel). Values of
// one group sit together after its label; `unit` follows the number.
struct ToolValue {
    std::string name;
    std::u16string label;
    std::u16string text;
    bool editable = false;
    std::u16string group;
    std::u16string unit;
    double step = 1;
    // swatch: a colour beside its first channel (0xAARRGGBB; 0: none); marked: the
    // value a drag over the video changes.
    std::uint32_t swatch = 0;
    bool marked = false;
};

// A value's text as a number (a plain decimal, spaces around it and a
// leading '+' allowed, at most kValueLimit either way); false for anything
// else.
inline constexpr double kValueLimit = 1e8;
bool parseValue(const std::u16string &text, double &out);

// Commit stages the value and commits one history step; Sample stages it on the
// gesture a scrub's first sample opens, which the next Commit commits and Esc
// drops.
enum class ValueEdit { Commit, Sample };

// setOption takes 0/1 for a toggle, the index for a choice or switch and 1 for
// an action. A Switch is a choice shown as a row of text buttons; a searchable
// choice opens with a search field.
struct ToolOption {
    enum class Kind { Toggle, Choice, Action, Switch };
    std::string name;
    Kind kind = Kind::Toggle;
    std::string iconRole;     // the icon set's role (toggles, actions, a choice's menu button)
    std::u16string tooltip;
    bool checked = false;     // a toggle's state
    bool enabled = true;
    std::vector<std::u16string> choices;
    int index = 0;            // a choice's selection
    // The first entry is "no choice" and the last an action; the menu puts it
    // after a separator.
    bool listEnds = false;
    std::u16string group;
    // Searchable: each entry's note beside it, under its section (sectionNames index).
    bool searchable = false;
    std::vector<std::u16string> notes;
    std::vector<int> sections;
    std::vector<std::u16string> sectionNames;
    bool afterValues = false; // shown after the tool's values
};

// A tool's last edit, which it can redo with other values in place of that
// history step.
struct LastChange {
    std::u16string tag;
    int change = 0;
    int lines = 0;
    std::vector<ToolValue> values;
};

// Stages new texts for its targets while the pointer or key is down and commits
// them as one history step; cancelling drops them. Nothing reaches the
// Document, the draft or the history before the commit. Targets are fixed when
// it begins.
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
    // A staged text that differs from the target's as the gesture began.
    bool changesAnyLine() const;
    std::optional<std::u8string> staged(core::LineId line, bool translation = false) const;
    // The staged texts put into a copy of the Document.
    void applyTo(core::Document &document) const;
    // Only the Lines shown at the video's time (5 ms either side, or any not yet
    // over while playing), the targets with their staged texts.
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

// Lines picked explicitly and kept by identity; with nothing picked a tool
// edits the active Line alone.
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
struct ShapePreset;
struct AllTagsSetting;

enum class LineWarning { None, NotVisible, Comment };

enum class PointerShape { Arrow, SizeWE, SizeNS, SizeNWSE, SizeNESW, Sizing };

// From a HitTest result below INSIDE (LEFT 1, RIGHT 2, TOP 4, BOTTOM 8 or their
// sums).
PointerShape edgePointerShape(int hit);

class VisualHost {
public:
    virtual ~VisualHost() = default;
    // The host's, not a tool's; the host puts the arrow back when the tool changes
    // or its events are blocked.
    void setPointerShape(PointerShape shape) { m_pointerShape = shape; }
    PointerShape pointerShape() const { return m_pointerShape; }
    bool hasArrow() const { return m_pointerShape == PointerShape::Arrow; }
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
    // Undoes the last history step when the Document is still at `revision` and no
    // gesture is open; the tool then commits its step again in its place.
    virtual bool undoStep(std::uint64_t revision)
    {
        (void)revision;
        return false;
    }
    // The crosshair label's size in device pixels.
    virtual std::pair<int, int> measureLabel(std::u16string_view text) const = 0;
    // The tool's drawing or values changed.
    virtual void toolChanged() = 0;

    // The video's time (the shown frame's start, ms), its timebase, the text
    // measure (null: measuring fails) and whether filtering is ignored in some
    // actions.
    virtual std::int64_t videoTimeMs() const { return 0; }
    virtual LegacyTimebase timebase() const { return {}; }
    virtual TextMeasurePort *textMeasure() const { return nullptr; }
    // Whether the renderer has a font of this name (family, full or PostScript, ASCII
    // case ignored, as libass matches it) from the attachments or external fonts,
    // which the text measure does not see.
    virtual bool fontLeased(const std::string &name) const
    {
        (void)name;
        return false;
    }
    // Whether fonts are leased and `font` lacks a character of `text`, which libass
    // may then take from a leased font the measure does not see.
    virtual bool fallsBackToLeased(const std::string &font, bool bold, bool italic, std::u16string_view text) const
    {
        (void)font;
        (void)bold;
        (void)italic;
        (void)text;
        return false;
    }
    virtual bool ignoreFiltered() const { return false; }
    virtual void log(std::u16string_view text) { (void)text; }
    // The Line editor's selection in the text the tools edit, in UTF-16 code units.
    virtual std::pair<long, long> editorSelection() const { return {0, 0}; }
    // Places the editor's caret at the tag a tool changed; called after the commit.
    virtual void setEditorSelection(long from, long to)
    {
        (void)from;
        (void)to;
    }
    // A refused point insertion's bell and a notice, shown without blocking.
    virtual void bell() {}
    virtual void notice(std::u16string_view text) { (void)text; }
    // The drawing's shape presets (null for none).
    virtual const std::vector<ShapePreset> *shapePresets() const { return nullptr; }
    // The Lines the Grid shows (not hidden by its filter or a closed group); asked
    // once per gesture. An empty function means every Line.
    virtual std::function<bool(core::LineId)> shownLines() const { return {}; }
    // The all-tags tool's definitions.
    virtual const std::vector<AllTagsSetting> *allTagsSettings() const { return nullptr; }
    // The accelerator text ("Ctrl-,") of the Line editor's hotkey `id`; the default
    // unless the host knows the user's.
    virtual std::string editorHotkey(int id) const;

private:
    PointerShape m_pointerShape = PointerShape::Arrow;
};

// Device pixels per HikariSub legacy pixel: handle, ring and arrow sizes and grab
// and snap distances times this keep their on-screen size.
inline float unitOf(const VisualHost &host)
{
    return static_cast<float>(host.view().devicePixelRatio());
}

// The host gives a tool pointer events over the video with a video open, keys
// while the video has focus, and resets when the view, script or Line changed.
class VisualTool {
public:
    virtual ~VisualTool() = default;
    virtual Family family() const = 0;
    // The view, script resolution or active Line changed.
    virtual void reset(VisualHost &host) { (void)host; }
    // True when the tool's own commit must not reset it (the host resets a tool on
    // every new revision).
    virtual bool keepsStateAfterCommit() const { return false; }
    virtual void pointer(const Pointer &event, VisualHost &host) = 0;
    // True for a key the tool takes ahead of the Video and Global bindings (its
    // nudge's arrows), whatever its state.
    virtual bool takesKey(const Key &event) const
    {
        (void)event;
        return false;
    }
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
    // True when the value was staged (Sample) or committed (Commit). A gesture the
    // tool's pointer or keys opened refuses it.
    virtual bool setValue(const std::string &name, const std::u16string &text, VisualHost &host,
                          ValueEdit edit = ValueEdit::Commit)
    {
        (void)name;
        (void)text;
        (void)host;
        (void)edit;
        return false;
    }
    // False: the tool works outside its Line's time and on comments.
    virtual bool warnsOutsideLine() const { return family() != Family::Crosshair; }
    // A tool's own warning; nullopt: the active Line's (lineWarning).
    virtual std::optional<LineWarning> warning(const VisualHost &host) const
    {
        (void)host;
        return std::nullopt;
    }
    // Called while the tool is blocked (nothing to show), when the host renders or
    // takes a pointer event.
    virtual void blocked(VisualHost &host) { (void)host; }
    // The family became active: the tool's own state starts over; the rail's
    // options stay.
    virtual void selected(VisualHost &host) { (void)host; }
    // The rail's second row for the family.
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
    // The value a drag over the video changes (a click on its field).
    virtual bool markValue(const std::string &name, VisualHost &host)
    {
        (void)name;
        (void)host;
        return false;
    }
    // The picked Lines changed.
    virtual void targetsChanged(VisualHost &host) { (void)host; }
    // The last change card; editing a value redoes its step in place of the one it
    // replaces.
    virtual std::optional<LastChange> lastChange(const VisualHost &host) const
    {
        (void)host;
        return std::nullopt;
    }
    virtual bool editLastChange(const std::string &name, const std::u16string &text, VisualHost &host)
    {
        (void)name;
        (void)text;
        (void)host;
        return false;
    }
    virtual void dismissLastChange() {}
    // Esc with no gesture open: true when the tool dropped a pending step
    // (RotationZ's first point).
    virtual bool cancelPending(VisualHost &host)
    {
        (void)host;
        return false;
    }
    virtual bool hasPending() const { return false; }
    // Lines the video renders after the Document's while the tool is on (the vector
    // clip's mask).
    virtual std::vector<core::LineRecord> previewLines(const VisualHost &host) const
    {
        (void)host;
        return {};
    }

};

// nullptr for a family without a tool.
std::unique_ptr<VisualTool> makeVisualTool(Family family);

// A tool other than the crosshair works only on a Dialogue Line visible at the
// video's time (start <= time < end); Vector clip and Drawing work on comments too.
LineWarning lineWarning(Family family, const core::LineRecord &line, std::int64_t videoMs);
std::u16string_view warningText(LineWarning warning);

}
