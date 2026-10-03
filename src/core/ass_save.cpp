#include "hikari/core/ass_save.h"

#include "text_util.h"

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
    // ActorWithStates: [bookmark] first, then the group or visibility marker
    // prepended before it (a group marker wins over visibility).
    std::u8string actor = line.bookmark ? u8"[bookmark]" + line.actor : line.actor;
    switch (line.group) {
    case GroupMarker::Description: actor.insert(0, u8"[tree_description]"); break;
    case GroupMarker::Opened: actor.insert(0, u8"[tree_opened]"); break;
    case GroupMarker::Closed: actor.insert(0, u8"[tree_closed]"); break;
    case GroupMarker::None:
        if (line.visibility == LineVisibility::Hidden)
            actor.insert(0, u8"[hidden]");
        else if (line.visibility == LineVisibility::VisibleBlock)
            actor.insert(0, u8"[visible]");
        break;
    }
    std::u8string out = line.comment ? u8"Comment: " : u8"Dialogue: ";
    out += number(line.layer.value) + u8',' + time(line.start.value) + u8',' + time(line.end.value) + u8',' +
           line.style + u8',' + actor + u8',' + number(line.marginLeft.value) + u8',' +
           number(line.marginRight.value) + u8',' + number(line.marginVertical.value) + u8',' + line.effect +
           u8',' + line.text;
    return out;
}

} // namespace legacy

std::vector<std::byte> encodeAss(const Document &document)
{
    return encodeAss(document, AssSaveOptions{});
}

std::vector<std::byte> encodeAss(const Document &document, const AssSaveOptions &options)
{
    const std::u8string tlMode = document.scriptInfo(u8"TLMode").value_or(std::u8string{});
    const std::u8string tlStyle = document.scriptInfo(u8"TLMode Style").value_or(std::u8string{});
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
    detail::NewlineTracker newlines;
    for (const auto &section : document.sections()) {
        if (section.headerSpan) {
            copy(section.headerSpan->offset, section.headerSpan->length + section.headerSpan->terminatorLength);
            newlines.see(*section.headerSpan);
        }
        for (const auto &record : section.records) {
            const auto *line = std::get_if<LineRecord>(&record);
            const auto *property = std::get_if<PropertyRecord>(&record);
            const auto *style = std::get_if<StyleRecord>(&record);
            const SourceSpan &span = std::visit([](const auto &r) -> const SourceSpan & { return r.span; }, record);
            // Legacy GetSInfos and Styles::GetRaw text for added or changed records.
            std::optional<std::u8string> generated;
            if (property && (property->inserted || property->edited))
                generated = property->key + u8": " + property->value;
            if (style && style->inserted) {
                std::u8string text = u8"Style: ";
                for (std::size_t i = 0; i < style->fields.size(); ++i)
                    text += (i ? u8"," : u8"") + style->fields[i];
                generated = text;
            }
            if ((property && property->inserted) || (style && style->inserted)) {
                append(newlines.wrap(*generated));
                newlines.openEnd = false;
                continue;
            }
            if (line && line->inserted) {
                // In translation mode a Line given a translation is an original
                // line plus its translation line, as for edited Lines below.
                if (!tlMode.empty() && tlMode != u8"Translated" && (!line->translation.empty() || line->unconfirmed)) {
                    LineRecord original = *line;
                    original.style = tlStyle;
                    original.comment = line->comment || options.hideOriginalOnVideo;
                    if (line->unconfirmed)
                        original.effect = u8"\fD";
                    append(newlines.wrap(legacy::assLineText(original)));
                    newlines.openEnd = false;
                    LineRecord translated = *line;
                    translated.text = line->translation;
                    append(newlines.wrap(legacy::assLineText(translated)));
                } else {
                    LineRecord single = *line;
                    if (!tlMode.empty() && !line->translation.empty())
                        single.text = line->translation;
                    append(newlines.wrap(legacy::assLineText(single)));
                }
                newlines.openEnd = false;
                continue;
            }
            newlines.see(span);
            if (generated) {
                append(*generated);
                copy(span.offset + span.length, span.terminatorLength);
                continue;
            }
            // A TLMode pair, or a Line that was given a translation, is written
            // as an original line plus its translation line.
            if (line && line->edited && (line->originalSpan || !line->translation.empty()) && !tlMode.empty()) {
                const SourceSpan &originalSpan = line->originalSpan ? *line->originalSpan : span;
                const bool hasTranslation = !line->translation.empty();
                if (tlMode != u8"Translated" && (hasTranslation || line->unconfirmed)) {
                    LineRecord original = *line;
                    original.style = tlStyle;
                    original.comment = line->comment || options.hideOriginalOnVideo;
                    if (line->unconfirmed)
                        original.effect = u8"\fD";
                    append(legacy::assLineText(original));
                    if (line->originalSpan)
                        copy(originalSpan.offset + originalSpan.length, originalSpan.terminatorLength);
                    else
                        append(span.terminatorLength == 1 ? u8"\n" : u8"\r\n");
                    LineRecord translated = *line;
                    translated.text = line->translation;
                    append(legacy::assLineText(translated));
                } else {
                    LineRecord single = *line;
                    if (hasTranslation)
                        single.text = line->translation;
                    append(legacy::assLineText(single));
                }
                copy(span.offset + span.length, span.terminatorLength);
            } else if (line && line->edited) {
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
