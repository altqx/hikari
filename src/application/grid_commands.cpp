#include "hikari/application/grid_commands.h"

#include <algorithm>
#include <vector>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

core::DocumentTime ms(std::int64_t value)
{
    return core::DocumentTime(std::max<std::int64_t>(0, value) * kUsPerMs); // SubsTime clamps at 0
}

std::int64_t msOf(core::DocumentTime t)
{
    return t.microseconds() / kUsPerMs;
}

std::int64_t zeroIt(std::int64_t value)
{
    return value / 10 * 10; // legacy ZEROIT
}

std::vector<const core::LineRecord *> linesOf(const EditSession &s)
{
    const auto lines = s.document().lines();
    return {lines.begin(), lines.end()};
}

int indexOf(const std::vector<const core::LineRecord *> &lines, core::LineId id)
{
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == id)
            return static_cast<int>(i);
    return -1;
}

// The next shown Line from `index` in `step` direction (legacy GetKeyFromPosition).
const core::LineRecord *neighbour(const std::vector<const core::LineRecord *> &lines, int index, int step,
                                  const LineVisible &visible)
{
    for (int i = index + step; i >= 0 && i < static_cast<int>(lines.size()); i += step)
        if (!visible || visible(lines[static_cast<std::size_t>(i)]->id))
            return lines[static_cast<std::size_t>(i)];
    return nullptr;
}

core::LineRecord blankCopy(const core::LineRecord &line)
{
    core::LineRecord copy = line;
    copy.text.clear();
    copy.translation.clear();
    return copy;
}

Selection selectOnly(core::LineId id)
{
    return Selection{id, {id}, id, {}};
}

Selection selectAll(const std::vector<core::LineId> &ids)
{
    Selection s;
    if (!ids.empty())
        s.active = s.anchor = ids.front();
    s.selected = {ids.begin(), ids.end()};
    return s;
}

} // namespace

std::expected<void, CommandRefusal> insertLine(EditSession &session, InsertWhere where,
                                               std::optional<std::int64_t> videoMs, const LineVisible &visible)
{
    const auto active = session.selection().active;
    const auto lines = linesOf(session);
    const int index = active ? indexOf(lines, *active) : -1;
    if (index < 0)
        return std::unexpected(CommandRefusal::Invalid);
    const core::LineRecord &current = *lines[static_cast<std::size_t>(index)];
    core::LineRecord line = blankCopy(current);
    if (videoMs) {
        line.start.value = ms(zeroIt(*videoMs));
        line.end.value = ms(zeroIt(*videoMs + 4000));
    } else if (where == InsertWhere::Before) {
        // Legacy compares the previous Line's End with this Line's Start and
        // takes it even when that leaves End before Start (characterized).
        line.end.value = current.start.value;
        const auto *previous = neighbour(lines, index, -1, visible);
        line.start.value = previous && previous->end.value > current.start.value
                               ? previous->end.value
                               : ms(msOf(current.start.value) - 4000);
    } else {
        line.start.value = current.end.value;
        const auto *next = neighbour(lines, index, +1, visible);
        line.end.value = next && next->start.value > current.end.value ? next->start.value
                                                                      : ms(msOf(current.end.value) + 4000);
    }
    std::optional<core::LineId> added;
    const core::LineId at = current.id;
    const auto ran = session.run(Command{"Inserting line", session.revision(), {at}, [&](core::Document &d) {
                                             added = where == InsertWhere::Before ? d.insertLineBefore(at, line)
                                                                                  : d.insertLineAfter(at, line);
                                             return added.has_value();
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selectOnly(*added));
    return {};
}

std::expected<void, CommandRefusal> insertWithFrameTimes(EditSession &session, InsertWhere where, FrameTimes frame)
{
    const auto lines = linesOf(session);
    std::vector<const core::LineRecord *> chosen;
    for (const auto *l : lines)
        if (session.selection().selected.contains(l->id))
            chosen.push_back(l);
    if (chosen.empty() && session.selection().active)
        if (const int i = indexOf(lines, *session.selection().active); i >= 0)
            chosen.push_back(lines[static_cast<std::size_t>(i)]);
    if (chosen.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<core::LineRecord> copies;
    for (const auto *l : chosen) {
        core::LineRecord copy = *l;
        copy.start.value = ms(zeroIt(frame.startMs));
        copy.end.value = ms(zeroIt(frame.endMs));
        copies.push_back(std::move(copy));
    }
    std::vector<core::LineId> added;
    const core::LineId first = chosen.front()->id, last = chosen.back()->id;
    const auto ran = session.run(Command{"Duplicating lines", session.revision(), {first, last}, [&](core::Document &d) {
                                             added.clear();
                                             std::optional<core::LineId> previous;
                                             for (const auto &copy : copies) {
                                                 const auto id = previous ? d.insertLineAfter(*previous, copy)
                                                                 : where == InsertWhere::Before ? d.insertLineBefore(first, copy)
                                                                                                : d.insertLineAfter(last, copy);
                                                 if (!id)
                                                     return false;
                                                 added.push_back(*id);
                                                 previous = id;
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selectAll(added));
    return {};
}

std::expected<void, CommandRefusal> duplicateLines(EditSession &session, const LineVisible &visible, bool keepSelection)
{
    const auto lines = linesOf(session);
    const auto &selected = session.selection().selected;
    int start = -1;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (selected.contains(lines[i]->id)) {
            start = static_cast<int>(i);
            break;
        }
    if (start < 0)
        return std::unexpected(CommandRefusal::Invalid);
    // The run: selected Lines from the first one; hidden unselected Lines are
    // stepped over, a shown unselected Line ends it.
    std::vector<const core::LineRecord *> run;
    const core::LineRecord *lastInRun = nullptr; // the copies go after it
    for (std::size_t i = static_cast<std::size_t>(start); i < lines.size(); ++i) {
        if (selected.contains(lines[i]->id)) {
            run.push_back(lines[i]);
            lastInRun = lines[i];
        } else if (!visible || visible(lines[i]->id)) {
            break;
        } else {
            lastInRun = lines[i]; // legacy rw1 also moves past hidden rows
        }
    }
    std::vector<core::LineRecord> copies;
    for (const auto *l : run)
        copies.push_back(*l);
    std::vector<core::LineId> added;
    const core::LineId after = lastInRun->id;
    std::set<core::LineId> touches;
    for (const auto *l : run)
        touches.insert(l->id);
    const auto ran = session.run(Command{"Duplicating lines", session.revision(), touches, [&](core::Document &d) {
                                             added.clear();
                                             core::LineId previous = after;
                                             for (const auto &copy : copies) {
                                                 const auto id = d.insertLineAfter(previous, copy);
                                                 if (!id)
                                                     return false;
                                                 added.push_back(*id);
                                                 previous = *id;
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    if (keepSelection) {
        std::vector<core::LineId> originals;
        for (const auto *l : run)
            originals.push_back(l->id);
        session.setSelection(selectAll(originals));
    } else {
        session.setSelection(selectAll(added));
    }
    return {};
}

std::expected<void, CommandRefusal> deleteLines(EditSession &session)
{
    const auto lines = linesOf(session);
    const auto selected = session.selection().selected;
    int first = -1;
    std::vector<core::LineId> doomed;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (selected.contains(lines[i]->id)) {
            if (first < 0)
                first = static_cast<int>(i);
            doomed.push_back(lines[i]->id);
        }
    if (doomed.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::optional<core::LineId> replacement;
    const auto ran = session.run(Command{"Deleting lines", session.revision(), {doomed.begin(), doomed.end()},
                                         [&](core::Document &d) {
                                             for (const auto id : doomed)
                                                 if (!d.removeLine(id))
                                                     return false;
                                             if (d.lines().empty()) {
                                                 // Legacy adds a default Dialogue (0:00:00.00-0:00:05.00, Default).
                                                 core::LineRecord line;
                                                 line.style = u8"Default";
                                                 line.end.value = ms(5000);
                                                 replacement = d.appendLine(line);
                                                 return replacement.has_value();
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    const auto after = linesOf(session);
    if (!after.empty())
        session.setSelection(selectOnly(after[std::min<std::size_t>(static_cast<std::size_t>(first), after.size() - 1)]->id));
    return {};
}

namespace {

// Legacy GetSelections: the selected Lines the Grid shows, in Document order.
std::vector<const core::LineRecord *> shownSelection(const EditSession &session, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id) && (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

std::u8string joined(const std::vector<const core::LineRecord *> &parts, bool translation)
{
    // Legacy: while nothing is collected a contributor is taken as it is;
    // after that each non-empty one follows a \\N.
    std::u8string out;
    for (const auto *l : parts) {
        const std::u8string &t = translation ? l->translation : l->text;
        if (out.empty())
            out = t;
        else if (!t.empty())
            out += u8"\\N" + t;
    }
    return out;
}

} // namespace

std::expected<void, CommandRefusal> joinLines(EditSession &session, JoinKind kind, const LineVisible &visible)
{
    const auto lines = linesOf(session);
    std::vector<const core::LineRecord *> parts;
    if (kind == JoinKind::WithPrevious || kind == JoinKind::WithNext) {
        const auto active = session.selection().active;
        const int index = active ? indexOf(lines, *active) : -1;
        if (index < 0)
            return std::unexpected(CommandRefusal::Invalid);
        const auto *other = neighbour(lines, index, kind == JoinKind::WithPrevious ? -1 : +1, visible);
        if (!other)
            return std::unexpected(CommandRefusal::Invalid);
        parts = kind == JoinKind::WithPrevious ? std::vector{other, lines[static_cast<std::size_t>(index)]}
                                               : std::vector{lines[static_cast<std::size_t>(index)], other};
    } else {
        parts = shownSelection(session, visible);
        const std::size_t limit = kind == JoinKind::Join ? 20 : 500;
        if (parts.size() < 2 || parts.size() > limit)
            return std::unexpected(CommandRefusal::Invalid);
    }
    const core::LineId survivor = parts.front()->id;
    core::LineRecord result = *parts.front();
    if (kind == JoinKind::KeepFirst || kind == JoinKind::KeepLast) {
        result.end.value = parts.back()->end.value;
        if (kind == JoinKind::KeepLast) {
            result.text = parts.back()->text;
            result.translation = parts.back()->translation;
        }
    } else {
        core::DocumentTime start = parts.front()->start.value, end = parts.front()->end.value;
        for (const auto *l : parts) {
            start = std::min(start, l->start.value);
            end = std::max(end, l->end.value);
        }
        result.start.value = start;
        result.end.value = end;
        result.text = joined(parts, false);
        result.translation = joined(parts, true);
    }
    std::set<core::LineId> touches;
    for (const auto *l : parts)
        touches.insert(l->id);
    const char *name = kind == JoinKind::WithPrevious ? "Joining line with the previous line"
                       : kind == JoinKind::WithNext   ? "Joining line with the next line"
                       : kind == JoinKind::KeepFirst  ? "Joining lines and keeping the first"
                       : kind == JoinKind::KeepLast   ? "Joining lines and keeping the last"
                                                      : "Joining lines";
    const auto ran = session.run(Command{name, session.revision(), touches, [&](core::Document &d) {
                                             if (!d.editLine(survivor, [&](core::LineRecord &l) {
                                                     l.start.value = result.start.value;
                                                     l.end.value = result.end.value;
                                                     l.text = result.text;
                                                     l.translation = result.translation;
                                                 }))
                                                 return false;
                                             for (std::size_t i = 1; i < parts.size(); ++i)
                                                 if (!d.removeLine(parts[i]->id))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selectOnly(survivor));
    return {};
}

std::expected<void, CommandRefusal> swapLines(EditSession &session)
{
    const auto lines = linesOf(session);
    std::vector<int> rows;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (session.selection().selected.contains(lines[i]->id))
            rows.push_back(static_cast<int>(i));
    if (rows.size() != 2)
        return std::unexpected(CommandRefusal::Invalid);
    const core::LineRecord first = *lines[static_cast<std::size_t>(rows[0])];
    const core::LineRecord second = *lines[static_cast<std::size_t>(rows[1])];
    const auto active = session.selection().active;
    const int activeRow = active ? indexOf(lines, *active) : -1;
    const std::optional<core::LineId> afterSecond =
        static_cast<std::size_t>(rows[1]) + 1 < lines.size() ? std::optional(lines[static_cast<std::size_t>(rows[1]) + 1]->id)
                                                             : std::nullopt;
    const auto ran = session.run(Command{"Swapping lines", session.revision(), {first.id, second.id}, [&](core::Document &d) {
                                             // Both keep their identity and source bytes.
                                             return d.moveLine(second.id, first.id) && d.moveLine(first.id, afterSecond);
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    // Selections and the active row are positions in legacy.
    const auto now = linesOf(session);
    Selection s;
    s.selected = {now[static_cast<std::size_t>(rows[0])]->id, now[static_cast<std::size_t>(rows[1])]->id};
    if (activeRow >= 0 && activeRow < static_cast<int>(now.size()))
        s.active = s.anchor = now[static_cast<std::size_t>(activeRow)]->id;
    session.setSelection(s);
    return {};
}

std::expected<void, CommandRefusal> makeContinuous(EditSession &session, bool withPrevious, const LineVisible &visible)
{
    const auto lines = linesOf(session);
    const auto chosen = shownSelection(session, visible);
    if (chosen.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<std::pair<core::LineId, core::DocumentTime>> changes;
    for (const auto *l : chosen) {
        const int i = indexOf(lines, l->id);
        if (withPrevious && i >= 1)
            changes.emplace_back(l->id, lines[static_cast<std::size_t>(i) - 1]->end.value);
        else if (!withPrevious && i + 1 < static_cast<int>(lines.size()))
            changes.emplace_back(l->id, lines[static_cast<std::size_t>(i) + 1]->start.value);
    }
    std::set<core::LineId> touches;
    for (const auto *l : chosen)
        touches.insert(l->id);
    const auto selection = session.selection();
    const auto ran = session.run(Command{"Setting line times as continuous", session.revision(), touches,
                                         [&](core::Document &d) {
                                             for (const auto &[id, time] : changes)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) {
                                                         (withPrevious ? l.start : l.end).value = time;
                                                     }))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

} // namespace hikari::application
