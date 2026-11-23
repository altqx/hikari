#include "hikari/application/reading_limits.h"

#include "hikari/core/document.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace hikari::application {

namespace {

struct Field {
    std::u8string_view key;
    std::string_view setting;
    int ReadingLimits::*limit;
    std::optional<int> ScriptReadingLimits::*script;
};

constexpr std::array kFields{
    Field{u8"Hikari CPS Warning", "grid.cpsWarning", &ReadingLimits::cpsWarning, &ScriptReadingLimits::cpsWarning},
    Field{u8"Hikari CPS Error", "grid.cpsError", &ReadingLimits::cpsError, &ScriptReadingLimits::cpsError},
    Field{u8"Hikari Line Length", "grid.lineLength", &ReadingLimits::lineLength, &ScriptReadingLimits::lineLength},
    Field{u8"Hikari Max Lines", "grid.maxLines", &ReadingLimits::maxLines, &ScriptReadingLimits::maxLines},
};

std::optional<int> positive(const std::optional<std::u8string> &text)
{
    if (!text)
        return std::nullopt;
    const auto *first = reinterpret_cast<const char *>(text->data());
    const auto *last = first + text->size();
    int value = 0;
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc() || end != last || value <= 0)
        return std::nullopt;
    return value;
}

std::u8string digits(int value)
{
    const std::string text = std::to_string(value);
    return {text.begin(), text.end()};
}

}

double cpsSeverity(const ReadingLimits &limits, int cps)
{
    const int from = limits.cpsWarning;
    const int to = std::max(from, limits.cpsError);
    if (cps <= from)
        return 0;
    return std::min(static_cast<double>(cps - from + 1) / (to - from + 1), 1.0);
}

ReadingLimits profileReadingLimits(const Settings &settings)
{
    ReadingLimits limits;
    for (const Field &f : kFields)
        limits.*f.limit = static_cast<int>(std::clamp<std::int64_t>(settings.integer(f.setting), 1, 1000));
    return limits;
}

ReadingLimits ScriptReadingLimits::over(const ReadingLimits &profile) const
{
    ReadingLimits limits = profile;
    for (const Field &f : kFields)
        if (const auto value = this->*f.script)
            limits.*f.limit = *value;
    return limits;
}

ScriptReadingLimits scriptReadingLimits(const core::Document &document)
{
    ScriptReadingLimits limits;
    for (const Field &f : kFields)
        limits.*f.script = positive(document.scriptInfo(f.key));
    return limits;
}

std::vector<ScriptInfoWrite> readingLimitsInfo(const std::optional<ReadingLimits> &limits)
{
    std::vector<ScriptInfoWrite> writes;
    for (const Field &f : kFields)
        writes.push_back({std::u8string(f.key), limits ? std::optional(digits((*limits).*f.limit)) : std::nullopt});
    return writes;
}

std::expected<void, CommandRefusal> applyReadingLimits(EditSession &session, const std::optional<ReadingLimits> &limits)
{
    auto writes = readingLimitsInfo(limits);
    std::erase_if(writes, [&](const ScriptInfoWrite &w) { return session.document().scriptInfo(w.key) == w.value; });
    if (writes.empty())
        return {};
    const auto ran = session.run(Command{"Changing the subtitle header", session.revision(), {}, [&](core::Document &d) {
                                             for (const auto &w : writes)
                                                 if (w.value ? !d.setScriptInfo(w.key, *w.value) : !d.removeScriptInfo(w.key))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

}
