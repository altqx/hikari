#include "hikari/application/visual_tools.h"

#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_crosshair.h"
#include "hikari/application/visual_drawing.h"

#include <algorithm>
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

} // namespace

const std::array<FamilyInfo, kFamilyCount> &families()
{
    // VideoToolbar.cpp:44-54 (icons and tooltips) and SubsFile.cpp:228-238
    // (the history names of VISUAL_POSITION .. VISUAL_ALL_TAGS; the
    // crosshair's Ctrl+click records VISUAL_POSITION, VisualCross.cpp:125).
    static const std::array<FamilyInfo, kFamilyCount> table{{
        {Family::Crosshair, "cross", "Position pointer", "Visual positioning tool"},
        {Family::Position, "position", "Text positioning", "Visual positioning tool"},
        {Family::Move, "move", "Text moving", "Visual movement tool"},
        {Family::Scale, "scale", "Text scaling", "Visual scaling tool"},
        {Family::RotationZ, "frz", "Text Z rotation", "Visual Z-axis rotation tool"},
        {Family::RotationXY, "frxy", "Text X / Y rotation", "Visual X/Y-axis rotation tool"},
        {Family::RectangleClip, "cliprect", "Rectangle clipping", "Visual rectangular clipping tool"},
        {Family::VectorClip, "clip", "Vector clipping", "Visual vector clipping tool"},
        {Family::Drawing, "drawing", "Vector drawing", "Visual vector drawing tool"},
        {Family::PositionShifter, "MOVEAll", "Position shifter", "Visual position adjustment tool"},
        {Family::Hydra, "ALL_TAGS", "Hydra", "Visual Hydra tool"},
    }};
    return table;
}

const FamilyInfo &familyInfo(Family family)
{
    return families()[static_cast<std::size_t>(family)];
}

std::expected<Gesture, CommandRefusal> Gesture::begin(const EditSession &session, std::vector<core::LineId> targets,
                                                      std::string history)
{
    // Refused up front with the reasons EditSession::run would give, so a
    // protected reference never shows a gesture it cannot keep.
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
        document.editLine(key.first, [&](core::LineRecord &line) { (key.second ? line.translation : line.text) = text; });
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
        return std::unexpected(CommandRefusal::StaleRevision); // never retargeted onto newer content
    std::set<core::LineId> touches(m_targets.begin(), m_targets.end());
    // The staged texts were made from the draft's text: commit it first, as
    // EditSession::run would, then the gesture on top of it.
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

std::unique_ptr<VisualTool> makeVisualTool(Family family)
{
    // One line per tool. T2-T6 add theirs here; until then a family has no
    // tool and its rail button selects nothing to draw.
    switch (family) {
    case Family::Crosshair:
        return std::make_unique<CrosshairTool>();
    case Family::RectangleClip:
        return std::make_unique<RectangleClipTool>(); // T4
    case Family::VectorClip:
        return std::make_unique<VectorClipTool>(); // T4
    case Family::Drawing:
        return std::make_unique<DrawingTool>(); // T5
    default:
        return nullptr;
    }
}

LineWarning lineWarning(Family family, const core::LineRecord &line, std::int64_t videoMs)
{
    // Visuals::Draw (Visuals.cpp:503-516): the comment text whenever the Line
    // is a comment, even for the tools that work on comments.
    const std::int64_t start = line.start.value.microseconds() / 1000;
    const std::int64_t end = line.end.value.microseconds() / 1000;
    const bool visible = videoMs >= start && videoMs < end;
    const bool commentBlocks = line.comment && family != Family::Drawing && family != Family::VectorClip;
    if (visible && !commentBlocks)
        return LineWarning::None;
    return line.comment ? LineWarning::Comment : LineWarning::NotVisible;
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

} // namespace hikari::application::visual
