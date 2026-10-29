#include "hikari/application/visual_tools.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/visual_all_tags.h"
#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_crosshair.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/visual_position.h"
#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/visual_shift.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace hikari::application::visual {

namespace {

bool hasLine(const core::Document &document, core::LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return true;
    return false;
}

const core::LineRecord *findLine(const core::Document &document, core::LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

}

bool parseValue(const std::u16string &text, double &out)
{
    std::string s;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        s += static_cast<char>(c);
    }
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ')
        ++a;
    while (b > a && s[b - 1] == ' ')
        --b;
    if (a < b && s[a] == '+')
        ++a;
    if (a >= b)
        return false;
    const auto [end, ec] = std::from_chars(s.data() + a, s.data() + b, out);
    return ec == std::errc() && end == s.data() + b && std::abs(out) <= kValueLimit;
}

const std::array<FamilyInfo, kFamilyCount> &families()
{
    static const std::array<FamilyInfo, kFamilyCount> table{{
        {Family::Crosshair, "cross", "Position pointer", "Visual positioning tool", "VIDEO_TOOL_CROSSHAIR"},
        {Family::Position, "position", "Text positioning", "Visual positioning tool", "VIDEO_TOOL_POSITION"},
        {Family::Move, "move", "Text moving", "Visual movement tool", "VIDEO_TOOL_MOVE"},
        {Family::Scale, "scale", "Text scaling", "Visual scaling tool", "VIDEO_TOOL_SCALE"},
        {Family::RotationZ, "frz", "Text Z rotation", "Visual Z-axis rotation tool", "VIDEO_TOOL_ROTATION_Z"},
        {Family::RotationXY, "frxy", "Text X / Y rotation", "Visual X/Y-axis rotation tool", "VIDEO_TOOL_ROTATION_XY"},
        {Family::RectangleClip, "cliprect", "Rectangle clipping", "Visual rectangular clipping tool",
         "VIDEO_TOOL_RECTANGLE_CLIP"},
        {Family::VectorClip, "clip", "Vector clipping", "Visual vector clipping tool", "VIDEO_TOOL_VECTOR_CLIP"},
        {Family::Drawing, "drawing", "Vector drawing", "Visual vector drawing tool", "VIDEO_TOOL_DRAWING"},
        {Family::PositionShifter, "MOVEAll", "Position shifter", "Visual position adjustment tool",
         "VIDEO_TOOL_POSITION_SHIFTER"},
        {Family::Hydra, "ALL_TAGS", "Hydra", "Visual Hydra tool", "VIDEO_TOOL_HYDRA"},
        {Family::Perspective, "perspective", "Perspective", "Visual perspective tool", "VIDEO_TOOL_PERSPECTIVE"},
    }};
    return table;
}

const std::array<Family, kFamilyCount> &railOrder()
{
    static const std::array<Family, kFamilyCount> order{
        Family::Crosshair,     Family::Position,   Family::Move,    Family::Scale,
        Family::RotationZ,     Family::RotationXY, Family::Perspective, Family::RectangleClip,
        Family::VectorClip,    Family::Drawing,    Family::PositionShifter, Family::Hydra};
    return order;
}

const FamilyInfo &familyInfo(Family family)
{
    return families()[static_cast<std::size_t>(family)];
}

std::optional<Family> familyOfHotkey(std::string_view symbol)
{
    for (const FamilyInfo &f : families())
        if (f.hotkey == symbol)
            return f.family;
    return std::nullopt;
}

bool isArrowKey(int key)
{
    return key == keys::Left || key == keys::Right || key == keys::Up || key == keys::Down;
}

std::optional<PointF> nudgeDirection(const Key &key, bool alt)
{
    if (key.control || (key.alt && !alt))
        return std::nullopt;
    switch (key.key) {
    case keys::Left:
        return PointF{-1, 0};
    case keys::Right:
        return PointF{1, 0};
    case keys::Up:
        return PointF{0, -1};
    case keys::Down:
        return PointF{0, 1};
    default:
        return std::nullopt;
    }
}

std::expected<Gesture, CommandRefusal> Gesture::begin(const EditSession &session, std::vector<core::LineId> targets,
                                                      std::string history)
{
    if (session.isProtected())
        return std::unexpected(CommandRefusal::Protected);
    if (session.isReadOnly())
        return std::unexpected(CommandRefusal::ReadOnly);
    Gesture g;
    if (targets.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto draft = session.draftRecord();
    for (const auto &id : targets) {
        const core::LineRecord *line = findLine(session.document(), id);
        if (!line)
            return std::unexpected(CommandRefusal::UnknownLine);
        g.m_before[id] = (draft && draft->id == id) ? *draft : *line;
    }
    g.m_targets = std::move(targets);
    g.m_history = std::move(history);
    g.m_revision = session.revision();
    return g;
}

const core::LineRecord &Gesture::before(core::LineId line) const
{
    return m_before.at(line);
}

void Gesture::stage(core::LineId line, std::u8string text, bool translation)
{
    if (m_before.contains(line))
        m_staged[{line, translation}] = std::move(text);
}

bool Gesture::changesAnyLine() const
{
    for (const auto &[key, text] : m_staged) {
        const core::LineRecord &line = m_before.at(key.first);
        if ((key.second ? line.translation : line.text) != text)
            return true;
    }
    return false;
}

std::optional<std::u8string> Gesture::staged(core::LineId line, bool translation) const
{
    const auto it = m_staged.find({line, translation});
    if (it == m_staged.end())
        return std::nullopt;
    return it->second;
}

void Gesture::applyTo(core::Document &document) const
{
    for (const auto &[key, text] : m_staged)
        (void)document.editLine(key.first, [&](core::LineRecord &line) {
            (key.second ? line.translation : line.text) = text;
        });
}

core::Document Gesture::preview(const core::Document &document, std::int64_t timeMs, bool playing) const
{
    core::Document out = document;
    const auto ms = [](const core::TimeField &t) { return t.value.microseconds() / 1000; };
    out.removeLinesIf([&](const core::LineRecord &line) {
        const std::int64_t start = ms(line.start), end = ms(line.end);
        if (std::find(m_targets.begin(), m_targets.end(), line.id) != m_targets.end())
            return !playing && !(timeMs >= start && timeMs <= end);
        return !((playing && timeMs <= start - 5) || (timeMs >= start - 5 && timeMs < end + 5));
    });
    applyTo(out);
    return out;
}

std::expected<void, CommandRefusal> Gesture::commit(EditSession &session) const
{
    if (m_staged.empty())
        return {};
    if (session.isProtected())
        return std::unexpected(CommandRefusal::Protected);
    if (session.isReadOnly())
        return std::unexpected(CommandRefusal::ReadOnly);
    if (session.revision() != m_revision)
        return std::unexpected(CommandRefusal::StaleRevision);
    std::set<core::LineId> touches(m_targets.begin(), m_targets.end());
    if (const auto draftLine = session.draftLine(); draftLine && touches.contains(*draftLine)) {
        if (session.draftProblem() && session.invalidCommitPolicy() == InvalidCommitPolicy::Block)
            return std::unexpected(CommandRefusal::InvalidDraft);
        if (!session.commitDraft() && session.draftLine())
            return std::unexpected(CommandRefusal::InvalidDraft);
    }
    Command command;
    command.name = m_history;
    command.expectedRevision = session.revision();
    command.touches = touches;
    command.apply = [staged = m_staged](core::Document &document) {
        for (const auto &[key, text] : staged) {
            const bool translation = key.second;
            if (!document.editLine(key.first, [&](core::LineRecord &line) {
                    (translation ? line.translation : line.text) = text;
                }))
                return false;
        }
        return true;
    };
    return session.run(command);
}

void BatchPicker::pick(std::vector<core::LineId> lines)
{
    m_picked.clear();
    for (const auto &id : lines)
        if (std::find(m_picked.begin(), m_picked.end(), id) == m_picked.end())
            m_picked.push_back(id);
}

std::vector<core::LineId> BatchPicker::targets(const EditSession &session) const
{
    std::vector<core::LineId> out;
    if (!m_picked.empty()) {
        const std::set<core::LineId> picked(m_picked.begin(), m_picked.end());
        for (const auto *line : session.document().lines())
            if (picked.contains(line->id))
                out.push_back(line->id);
    }
    if (out.empty() && session.selection().active && hasLine(session.document(), *session.selection().active))
        out.push_back(*session.selection().active);
    return out;
}

std::string VisualHost::editorHotkey(int id) const
{
    return defaultHotkey({id, EditorHotkey});
}

std::unique_ptr<VisualTool> makeVisualTool(Family family)
{
    switch (family) {
    case Family::Crosshair:
        return std::make_unique<CrosshairTool>();
    case Family::Position:
        return std::make_unique<PositionTool>();
    case Family::Move:
        return std::make_unique<MoveTool>();
    case Family::Scale:
        return std::make_unique<ScaleTool>();
    case Family::RotationZ:
        return std::make_unique<RotationZTool>();
    case Family::RotationXY:
        return std::make_unique<RotationXYTool>();
    case Family::RectangleClip:
        return std::make_unique<RectangleClipTool>();
    case Family::VectorClip:
        return std::make_unique<VectorClipTool>();
    case Family::Drawing:
        return std::make_unique<DrawingTool>();
    case Family::PositionShifter:
        return std::make_unique<PositionShifterTool>();
    case Family::Hydra:
        return std::make_unique<AllTagsTool>();
    case Family::Perspective:
        return std::make_unique<PerspectiveTool>();
    default:
        return nullptr;
    }
}

LineWarning lineWarning(Family family, const core::LineRecord &line, std::int64_t videoMs)
{
    const std::int64_t start = line.start.value.microseconds() / 1000;
    const std::int64_t end = line.end.value.microseconds() / 1000;
    const bool visible = videoMs >= start && videoMs < end;
    const bool commentBlocks = line.comment && family != Family::Drawing && family != Family::VectorClip;
    if (visible && !commentBlocks)
        return LineWarning::None;
    return line.comment ? LineWarning::Comment : LineWarning::NotVisible;
}

PointerShape edgePointerShape(int hit)
{
    constexpr int left = 1, right = 2, top = 4, bottom = 8;
    if (hit < 4)
        return PointerShape::SizeWE;
    if (hit % 4 == 0)
        return PointerShape::SizeNS;
    return (hit == top + left || hit == bottom + right) ? PointerShape::SizeNWSE : PointerShape::SizeNESW;
}

std::u16string_view warningText(LineWarning warning)
{
    switch (warning) {
    case LineWarning::NotVisible:
        return u"Line is not visible on video\nor has zero duration";
    case LineWarning::Comment:
        return u"Visual editing tools\ndo not work on comments";
    case LineWarning::None:
        break;
    }
    return {};
}

}
