#include "hikari/application/center_times.h"

#include <set>
#include <tuple>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

void center(const LegacyTimebase &timebase, core::TimeField &field)
{
    field.value = core::DocumentTime(std::int64_t{centeredTime(timebase, msOf(field.value))} * kUsPerMs);
}

}

int centeredTime(const LegacyTimebase &timebase, int ms)
{
    const int frame = timebase.frameAt(ms);
    if (frame <= 0)
        return 0;
    const int before = timebase.msAt(frame - 1), start = timebase.msAt(frame);
    const int centered = (before + (start - before) / 2 + 5) / 10 * 10;
    return centered > before && centered <= start ? centered : ms;
}

std::expected<void, CommandRefusal> centerTimesInFrames(EditSession &session, const LegacyTimebase &timebase)
{
    if (session.draftLine())
        session.commitDraft();
    std::set<core::LineId> chosen;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id))
            chosen.insert(l->id);
    if (chosen.empty() || timebase.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto selection = session.selection();
    bool changed = false;
    const auto ran = session.run(Command{"Centering times", session.revision(), chosen, [&](core::Document &d) {
                                             for (const auto id : chosen)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) {
                                                         const auto before = std::tuple(l.start.value, l.end.value);
                                                         center(timebase, l.start);
                                                         center(timebase, l.end);
                                                         changed = changed || before != std::tuple(l.start.value, l.end.value);
                                                     }))
                                                     return false;
                                             return changed;
                                         }});
    if (!changed)
        return {};
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

}
