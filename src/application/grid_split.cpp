#include "hikari/application/grid_split.h"

#include <cstdio>
#include <set>
#include <vector>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

int zeroIt(int ms)
{
    return ms / 10 * 10;
}

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

std::vector<const core::LineRecord *> shownSelected(const EditSession &s, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : s.document().lines())
        if (s.selection().selected.contains(l->id) && l->visibility != core::LineVisibility::Hidden &&
            (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

} // namespace

namespace legacy {

std::u8string floatText(float value)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%5.3f", static_cast<double>(value));
    std::string s(buf);
    // Trailing zeros, then the point (legacy getfloat).
    std::size_t remove = 0;
    for (std::size_t i = s.size() - 1; i > 0; --i) {
        if (s[i] == '0')
            ++remove;
        else if (s[i] == '.') {
            ++remove;
            break;
        } else
            break;
    }
    s.resize(s.size() - remove);
    const auto first = s.find_first_not_of(' ');
    s = first == std::string::npos ? std::string() : s.substr(first);
    return std::u8string(s.begin(), s.end());
}

std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms)
{
    // The first \move( or \pos( inside an override block.
    std::size_t at = std::u8string_view::npos;
    bool isMove = false;
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{')
            block = true;
        else if (text[i] == u8'}')
            block = false;
        else if (block && text[i] == u8'\\') {
            if (text.substr(i + 1, 5) == u8"move(") {
                at = i;
                isMove = true;
                break;
            }
            if (text.substr(i + 1, 4) == u8"pos(") {
                at = i;
                break;
            }
        }
    }
    if (at == std::u8string_view::npos)
        return std::u8string(text);
    const std::size_t open = text.find(u8'(', at), close = text.find(u8')', at);
    if (close == std::u8string_view::npos)
        return std::u8string(text);
    if (!isMove)
        return std::u8string(text); // a \pos stays where it is
    // GetMultiValueFloat: up to six comma-separated values.
    double v[6] = {0, 0, 0, 0, 0, 0};
    int count = 0;
    std::string values(text.begin() + static_cast<std::ptrdiff_t>(open + 1), text.begin() + static_cast<std::ptrdiff_t>(close));
    std::size_t p = 0;
    while (count < 6 && p <= values.size()) {
        const auto comma = values.find(',', p);
        v[count++] = std::strtod(values.substr(p, comma - p).c_str(), nullptr);
        if (comma == std::string::npos)
            break;
        p = comma + 1;
    }
    float t1 = static_cast<float>(v[4]), t2 = static_cast<float>(v[5]);
    if (!t1 && !t2) {
        t1 = static_cast<float>(lineStartMs);
        t2 = static_cast<float>(lineEndMs);
    } else {
        t1 += static_cast<float>(lineStartMs);
        t2 += static_cast<float>(lineStartMs);
    }
    // CalcMovePosition.
    float x = static_cast<float>(v[0]), y = static_cast<float>(v[1]);
    const float progress = (static_cast<float>(ms) - t1) / (t2 - t1);
    if (ms < t1) {
        // the start position
    } else if (ms > t2) {
        x = static_cast<float>(v[2]);
        y = static_cast<float>(v[3]);
    } else {
        x = x - (x - static_cast<float>(v[2])) * progress;
        y = y - (y - static_cast<float>(v[3])) * progress;
    }
    std::u8string out(text.substr(0, at));
    out += u8"\\pos(" + floatText(x) + u8"," + floatText(y) + u8")";
    out += text.substr(close + 1);
    return out;
}

} // namespace legacy

std::expected<void, CommandRefusal> splitAtVideoTime(EditSession &session, const LegacyTimebase &timebase,
                                                     std::int64_t videoMs, const LineVisible &visible)
{
    const auto selected = shownSelected(session, visible);
    if (selected.size() != 1 || timebase.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const int time = zeroIt(timebase.endTimeFor(timebase.frameAt(static_cast<int>(videoMs))));
    const core::LineId id = selected.front()->id;
    core::LineRecord copy = *selected.front();
    copy.start.value = core::DocumentTime(std::int64_t{time} * kUsPerMs);
    const auto ran = session.run(Command{"Splitting lines", session.revision(), {id}, [&](core::Document &d) {
                                             if (!d.editLine(id, [&](core::LineRecord &l) {
                                                     l.end.value = core::DocumentTime(std::int64_t{time} * kUsPerMs);
                                                 }))
                                                 return false;
                                             return d.insertLineAfter(id, copy).has_value();
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    const LineVisible &visible)
{
    if (!timebase.exact())
        return std::unexpected(CommandRefusal::Invalid);
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    struct Plan {
        core::LineId id;
        std::vector<core::LineRecord> frames; // the first replaces the Line
    };
    std::vector<Plan> plans;
    for (const auto *l : selected) {
        const int startMs = msOf(l->start.value), endMs = msOf(l->end.value);
        const int frameStart = timebase.frameAt(startMs), frameEnd = timebase.frameAt(endMs);
        Plan plan{l->id, {}};
        for (int j = frameStart; j < frameEnd; ++j) {
            core::LineRecord part = *l;
            part.text = legacy::moveToPos(l->text, startMs, endMs, timebase.msAt(j));
            part.start.value = core::DocumentTime(std::int64_t{zeroIt(timebase.startTimeFor(j))} * kUsPerMs);
            part.end.value = core::DocumentTime(std::int64_t{zeroIt(timebase.endTimeFor(j))} * kUsPerMs);
            plan.frames.push_back(std::move(part));
        }
        if (!plan.frames.empty())
            plans.push_back(std::move(plan));
    }
    if (plans.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &p : plans)
        touched.insert(p.id);
    const auto ran = session.run(Command{"Splitting lines", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &p : plans) {
                                                 const auto &first = p.frames.front();
                                                 if (!d.editLine(p.id, [&](core::LineRecord &l) {
                                                         l.text = first.text;
                                                         l.start.value = first.start.value;
                                                         l.end.value = first.end.value;
                                                     }))
                                                     return false;
                                                 core::LineId previous = p.id;
                                                 for (std::size_t k = 1; k < p.frames.size(); ++k) {
                                                     const auto id = d.insertLineAfter(previous, p.frames[k]);
                                                     if (!id)
                                                         return false;
                                                     previous = *id;
                                                 }
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

} // namespace hikari::application
