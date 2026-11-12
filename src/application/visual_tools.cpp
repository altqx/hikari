#include "hikari/application/visual_tools.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/visual_all_tags.h"
#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_crosshair.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_gradient.h"
#include "hikari/application/visual_text_on_path.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/visual_position.h"
#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/visual_shift.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <set>
#include <string>

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

bool replaceLine(core::Document &document, core::LineId target, const std::vector<Replacement> &copies,
                 std::vector<core::LineId> *added)
{
    const core::LineRecord *source = findLine(document, target);
    if (!source)
        return false;
    core::LineRecord copy = *source;
    copy.comment = false;
    core::LineId after = target;
    for (const auto &replacement : copies) {
        copy.text = replacement.text;
        copy.start.value = replacement.start.value_or(source->start.value);
        copy.end.value = replacement.end.value_or(source->end.value);
        const auto id = document.insertLineAfter(after, copy);
        if (!id)
            return false;
        after = *id;
        if (added)
            added->push_back(*id);
    }
    return document.editLine(target, [](core::LineRecord &line) { line.comment = true; });
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
        {Family::Gradient, "gradient", "Gradient", "Gradient", "VIDEO_TOOL_GRADIENT"},
        {Family::TextOnPath, "text-on-path", "Text on path", "Text on path", "VIDEO_TOOL_TEXT_ON_PATH"},
    }};
    return table;
}

const std::array<Family, kFamilyCount> &railOrder()
{
    static const std::array<Family, kFamilyCount> order{
        Family::Crosshair,     Family::Position,   Family::Move,    Family::Scale,
        Family::RotationZ,     Family::RotationXY, Family::Perspective, Family::RectangleClip,
        Family::VectorClip,    Family::Drawing,    Family::PositionShifter, Family::Hydra,
        Family::Gradient,      Family::TextOnPath};
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

std::optional<std::string> vectorClipOptionOfHotkey(std::string_view symbol)
{
    static constexpr std::string_view kModes[] = {"VIDEO_VECTOR_CLIP_DRAG",    "VIDEO_VECTOR_CLIP_LINE",
                                                  "VIDEO_VECTOR_CLIP_BEZIER",  "VIDEO_VECTOR_CLIP_BSPLINE",
                                                  "VIDEO_VECTOR_CLIP_POINT",   "VIDEO_VECTOR_CLIP_DELETE"};
    for (std::size_t i = 0; i < std::size(kModes); ++i)
        if (kModes[i] == symbol)
            return "mode" + std::to_string(i);
    if (symbol == "VIDEO_VECTOR_CLIP_INVERT")
        return "invert";
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

void Gesture::stageExtradata(core::LineId line, std::vector<core::Extradata> extra)
{
    if (m_before.contains(line))
        m_stagedExtra[line] = std::move(extra);
}

void Gesture::stageReplacement(core::LineId line, std::vector<std::u8string> texts)
{
    std::vector<Replacement> copies;
    for (auto &text : texts)
        copies.push_back({std::move(text), std::nullopt, std::nullopt});
    stageReplacement(line, std::move(copies));
}

void Gesture::stageReplacement(core::LineId line, std::vector<Replacement> copies)
{
    if (m_before.contains(line))
        m_replacements[line] = std::move(copies);
}

bool Gesture::changesAnyLine() const
{
    if (!m_replacements.empty())
        return true;
    for (const auto &[key, text] : m_staged) {
        const core::LineRecord &line = m_before.at(key.first);
        if ((key.second ? line.translation : line.text) != text)
            return true;
    }
    for (const auto &[id, extra] : m_stagedExtra)
        if (m_before.at(id).extra != extra)
            return true;
    return false;
}

std::optional<std::u8string> Gesture::staged(core::LineId line, bool translation) const
{
    const auto it = m_staged.find({line, translation});
    if (it == m_staged.end())
        return std::nullopt;
    return it->second;
}

std::u8string Gesture::stagedKey() const
{
    std::u8string key;
    const auto add = [&](core::LineId id, char8_t kind, const std::u8string &text) {
        for (const char c : std::to_string(id.value))
            key += static_cast<char8_t>(c);
        key += kind + text + u8'\x1f';
    };
    for (const auto &[at, text] : m_staged)
        add(at.first, at.second ? u8't' : u8's', text);
    for (const auto &[id, copies] : m_replacements)
        for (const auto &copy : copies) {
            std::u8string times;
            for (const auto &time : {copy.start, copy.end})
                for (const char c : time ? std::to_string(time->microseconds()) : std::string("-"))
                    times += static_cast<char8_t>(c);
            add(id, u8'r', times + u8' ' + copy.text);
        }
    return key;
}

void Gesture::applyTo(core::Document &document) const
{
    for (const auto &[id, texts] : m_replacements)
        (void)replaceLine(document, id, texts, nullptr);
    for (const auto &[key, text] : m_staged)
        (void)document.editLine(key.first, [&](core::LineRecord &line) {
            (key.second ? line.translation : line.text) = text;
        });
    for (const auto &[id, extra] : m_stagedExtra)
        (void)document.editLine(id, [&](core::LineRecord &line) { line.extra = extra; });
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
    if (!hasChanges())
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
    auto added = std::make_shared<std::vector<core::LineId>>();
    command.apply = [staged = m_staged, stagedExtra = m_stagedExtra, replacements = m_replacements,
                     added](core::Document &document) {
        added->clear();
        for (const auto &[id, texts] : replacements)
            if (!replaceLine(document, id, texts, added.get()))
                return false;
        for (const auto &[key, text] : staged) {
            const bool translation = key.second;
            if (!document.editLine(key.first, [&](core::LineRecord &line) {
                    (translation ? line.translation : line.text) = text;
                }))
                return false;
        }
        for (const auto &[id, extra] : stagedExtra)
            if (!document.editLine(id, [&](core::LineRecord &line) { line.extra = extra; }))
                return false;
        return true;
    };
    const auto ran = session.run(command);
    if (ran && !added->empty()) {
        Selection selection;
        selection.active = added->front();
        selection.selected.insert(added->begin(), added->end());
        session.setSelection(selection);
    }
    return ran;
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
    case Family::Gradient:
        return std::make_unique<GradientTool>();
    case Family::TextOnPath:
        return std::make_unique<TextOnPathTool>();
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
