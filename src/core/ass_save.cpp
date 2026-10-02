#include "hikari/core/ass_save.h"

#include <cstdio>

namespace hikari::core {

namespace legacy {

std::u8string assTimeText(std::int64_t ms)
{
    // Integer arithmetic exactly as SubsTime::raw (truncation toward zero).
    const std::int64_t csec = ms / 10, sec = ms / 1000, min = ms / 60000, hours = ms / 3600000;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%01lld:%02lld:%02lld.%02lld", static_cast<long long>(hours),
                  static_cast<long long>(min % 60), static_cast<long long>(sec % 60),
                  static_cast<long long>(csec % 100));
    return std::u8string(reinterpret_cast<const char8_t *>(buf));
}

std::u8string assLineText(const LineRecord &line)
{
    auto number = [](std::int64_t v) {
        const std::string s = std::to_string(v);
        return std::u8string(s.begin(), s.end());
    };
    // DocumentTime is microseconds; the legacy field is integer milliseconds.
    auto time = [](DocumentTime t) { return assTimeText(t.microseconds() / 1000); };
    std::u8string out = line.comment ? u8"Comment: " : u8"Dialogue: ";
    out += number(line.layer.value) + u8',' + time(line.start.value) + u8',' + time(line.end.value) + u8',' +
           line.style + u8',' + line.actor + u8',' + number(line.marginLeft.value) + u8',' +
           number(line.marginRight.value) + u8',' + number(line.marginVertical.value) + u8',' + line.effect +
           u8',' + line.text;
    return out;
}

} // namespace legacy

std::vector<std::byte> encodeAss(const Document &document)
{
    const auto &src = document.source().bytes;
    std::vector<std::byte> out;
    out.reserve(src.size() + 64);
    auto copy = [&](std::size_t from, std::size_t count) {
        out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(from),
                   src.begin() + static_cast<std::ptrdiff_t>(from + count));
    };
    auto append = [&](const std::u8string &text) {
        for (char8_t c : text)
            out.push_back(static_cast<std::byte>(c));
    };
    if (document.source().encoding == TextEncoding::Utf8WithBom)
        copy(0, 3);
    for (const auto &section : document.sections()) {
        if (section.headerSpan)
            copy(section.headerSpan->offset, section.headerSpan->length + section.headerSpan->terminatorLength);
        for (const auto &record : section.records) {
            const auto *line = std::get_if<LineRecord>(&record);
            const SourceSpan &span = std::visit([](const auto &r) -> const SourceSpan & { return r.span; }, record);
            if (line && line->edited) {
                append(legacy::assLineText(*line));
                copy(span.offset + span.length, span.terminatorLength);
            } else {
                copy(span.offset, span.length + span.terminatorLength);
            }
        }
    }
    return out;
}

} // namespace hikari::core
