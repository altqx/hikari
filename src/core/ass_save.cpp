#include "hikari/core/ass_save.h"
#include "hikari/core/ass_attachments.h"
#include "hikari/core/extradata.h"

#include "text_util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <ranges>
#include <string_view>

namespace hikari::core {

namespace legacy {

std::u8string assTimeText(std::int64_t ms)
{
    const std::int64_t csec = ms / 10, sec = ms / 1000, min = ms / 60000, hours = ms / 3600000;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%01lld:%02lld:%02lld.%02lld", static_cast<long long>(hours),
                  static_cast<long long>(min % 60), static_cast<long long>(sec % 60),
                  static_cast<long long>(csec % 100));
    return std::u8string(reinterpret_cast<const char8_t *>(buf));
}

namespace {

std::u8string_view groupMarkerText(GroupMarker marker)
{
    switch (marker) {
    case GroupMarker::Description: return u8"[tree_description]";
    case GroupMarker::Opened: return u8"[tree_opened]";
    case GroupMarker::Closed: return u8"[tree_closed]";
    case GroupMarker::None: break;
    }
    return {};
}

}

std::u8string assLineText(const LineRecord &line, std::u8string_view textPrefix)
{
    auto number = [](std::int64_t v) {
        const std::string s = std::to_string(v);
        return std::u8string(s.begin(), s.end());
    };
    auto time = [](DocumentTime t) { return assTimeText(t.microseconds() / 1000); };
    // [bookmark] first, then the group or visibility marker before it (a
    // group marker wins over visibility).
    std::u8string actor = line.bookmark ? u8"[bookmark]" + line.actor : line.actor;
    if (line.group == GroupMarker::None) {
        if (line.visibility == LineVisibility::Hidden)
            actor.insert(0, u8"[hidden]");
        else if (line.visibility == LineVisibility::VisibleBlock)
            actor.insert(0, u8"[visible]");
    } else {
        std::u8string markers;
        for (const auto marker : line.outerGroups)
            markers += groupMarkerText(marker);
        markers += groupMarkerText(line.group);
        actor.insert(0, markers);
    }
    std::u8string out = line.comment ? u8"Comment: " : u8"Dialogue: ";
    out += number(line.layer.value) + u8',' + time(line.start.value) + u8',' + time(line.end.value) + u8',' +
           line.style + u8',' + actor + u8',' + number(line.marginLeft.value) + u8',' +
           number(line.marginRight.value) + u8',' + number(line.marginVertical.value) + u8',' + line.effect +
           u8',' + std::u8string(textPrefix) + line.text;
    return out;
}

}

namespace {

bool startsWithComment(const std::vector<std::byte> &src, const SourceSpan &span)
{
    static constexpr std::string_view prefix = "Comment";
    if (span.length < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (static_cast<char>(src[span.offset + i]) != prefix[i])
            return false;
    return true;
}

// The first line end in the source, "\n" without one.
std::u8string sourceNewline(const std::vector<std::byte> &src)
{
    for (std::size_t i = 0; i < src.size(); ++i)
        if (src[i] == std::byte{'\n'})
            return i > 0 && src[i - 1] == std::byte{'\r'} ? u8"\r\n" : u8"\n";
    return u8"\n";
}

// Runs at the end of a record's iteration, `continue` included.
struct RecordEnd {
    std::function<void()> run;
    ~RecordEnd() { run(); }
};

bool endsWithBlankLine(const std::vector<std::byte> &out)
{
    const std::size_t n = out.size();
    if (n < 2 || out[n - 1] != std::byte{'\n'})
        return false;
    return out[n - 2] == std::byte{'\n'} || (n >= 3 && out[n - 2] == std::byte{'\r'} && out[n - 3] == std::byte{'\n'});
}

}

std::vector<std::byte> encodeAss(const Document &document)
{
    return encodeAss(document, AssSaveOptions{});
}

std::vector<std::byte> encodeAss(const Document &document, const AssSaveOptions &options)
{
    const std::u8string tlMode = document.scriptInfo(u8"TLMode").value_or(std::u8string{});
    const std::u8string tlStyle = document.scriptInfo(u8"TLMode Style").value_or(std::u8string{});
    const bool tlYes = tlMode == u8"Yes";
    const auto &src = document.source().bytes;
    std::vector<std::byte> out;
    out.reserve(src.size() + 64);
    // The renderer script's Lines outside an [Events] section, held for the next one.
    std::vector<std::byte> held;
    std::vector<std::byte> *sink = &out;
    auto copy = [&](std::size_t from, std::size_t count) {
        sink->insert(sink->end(), src.begin() + static_cast<std::ptrdiff_t>(from),
                     src.begin() + static_cast<std::ptrdiff_t>(from + count));
    };
    auto append = [&](const std::u8string &text) {
        for (char8_t c : text)
            sink->push_back(static_cast<std::byte>(c));
    };
    if (document.source().encoding == TextEncoding::Utf8WithBom)
        copy(0, 3);
    detail::NewlineTracker newlines;
    const ExtradataPlan extradata = options.renderer ? ExtradataPlan{} : planExtradata(document);
    auto eventText = [&](const LineRecord &l) {
        const auto ids = extradata.ids.find(l.id);
        return legacy::assLineText(l, ids == extradata.ids.end() ? std::u8string{} : extradataPrefixText(ids->second));
    };
    const Section *extradataSection = nullptr;
    const Record *lastEntry = nullptr;
    for (const auto &section : document.sections())
        if (section.kind == SectionKind::AegisubExtradata)
            extradataSection = &section;
    if (extradataSection)
        for (const auto &record : extradataSection->records)
            if (std::visit([](const auto &r) { return r.span.length > 0; }, record))
                lastEntry = &record;
    auto writeAddedExtradata = [&] {
        for (const auto &entry : extradata.added) {
            append(newlines.wrap(extradataLine(entry)));
            newlines.openEnd = false;
        }
    };
    const Section *beforeEmbedded = nullptr;
    for (const auto &section : document.sections() | std::views::reverse) {
        if (section.kind != SectionKind::Fonts && section.kind != SectionKind::Graphics && !continuesAttachment(section))
            break;
        beforeEmbedded = &section;
    }
    auto writeNewExtradataSection = [&] {
        if (extradataSection || extradata.added.empty())
            return;
        if (newlines.openEnd)
            append(newlines.newline);
        if (!out.empty() && !endsWithBlankLine(out))
            append(newlines.newline);
        append(u8"[Aegisub Extradata]" + newlines.newline);
        newlines.openEnd = false;
        writeAddedExtradata();
        if (beforeEmbedded)
            append(newlines.newline);
    };
    auto flushHeld = [&] {
        if (held.empty())
            return;
        if (newlines.openEnd)
            append(newlines.newline);
        out.insert(out.end(), held.begin(), held.end());
        held.clear();
        newlines.openEnd = false;
    };
    bool fonts = false;
    bool inEvents = false; // the last header written is an [Events] one
    for (const auto &section : document.sections()) {
        if (&section == beforeEmbedded)
            writeNewExtradataSection();
        fonts = section.kind == SectionKind::Fonts || (fonts && continuesAttachment(section));
        if (options.renderer && fonts)
            continue;
        // The [Hikari] section's header and properties are the editor's; its
        // Lines are drawn as the others are.
        const bool editorOnly = options.renderer && section.kind == SectionKind::Hikari;
        if (!editorOnly && (section.headerSpan || !section.header.empty()))
            inEvents = section.kind == SectionKind::Events;
        // The renderers read events only under [Events]: Lines held from
        // other sections go into the next one, after its Format line, or
        // before its first Line when it has none.
        const bool takesHeld = options.renderer && !editorOnly && section.kind == SectionKind::Events;
        const bool formatted = takesHeld && std::ranges::any_of(section.records, [](const auto &record) {
                                   return std::holds_alternative<FormatRecord>(record);
                               });
        // Its own Lines before the Format line wait with the held ones, so
        // the renderers have every Line in the Document's order.
        bool holdsOwn = formatted && !held.empty();
        // A Script Info section added at the top has no bytes: its header,
        // properties and a blank line, in the source's line ends.
        const bool added = !section.headerSpan && !section.header.empty();
        if (editorOnly) {
            // no header: its Lines continue the section before it
        } else if (section.headerSpan) {
            copy(section.headerSpan->offset, section.headerSpan->length + section.headerSpan->terminatorLength);
            newlines.see(*section.headerSpan);
        } else if (added && (section.kind == SectionKind::Hikari || section.kind == SectionKind::Unknown)) {
            // An added [Hikari] section (at the end, or before trailing
            // embedded data) or [Aegisub Project Garbage]: after a blank
            // line, in the line ends of the record before it.
            if (newlines.openEnd)
                append(newlines.newline);
            if (!out.empty() && !endsWithBlankLine(out))
                append(newlines.newline);
            append(section.header + newlines.newline);
            newlines.openEnd = false;
        } else if (added) {
            newlines.newline = sourceNewline(src);
            append(newlines.wrap(section.header));
            newlines.openEnd = false;
        }
        for (const auto &record : section.records) {
            const auto *line = std::get_if<LineRecord>(&record);
            const auto *property = std::get_if<PropertyRecord>(&record);
            const auto *style = std::get_if<StyleRecord>(&record);
            if (editorOnly && !line)
                continue;
            const bool format = std::holds_alternative<FormatRecord>(record);
            if (format)
                holdsOwn = false;
            if (takesHeld && !formatted && line)
                flushHeld();
            // A renderer Line outside [Events] goes to `held`, ending with a
            // line end; the output's own open end is kept for what follows.
            const bool hold = options.renderer && line && (!inEvents || holdsOwn);
            const bool openBefore = newlines.openEnd;
            if (hold) {
                sink = &held;
                newlines.openEnd = false;
            }
            const RecordEnd recordEnd{[&] {
                if (hold) {
                    if (!held.empty() && held.back() != std::byte{'\n'})
                        append(newlines.newline);
                    sink = &out;
                    newlines.openEnd = openBefore;
                } else if (takesHeld && format) {
                    flushHeld();
                }
            }};
            const SourceSpan &span = std::visit([](const auto &r) -> const SourceSpan & { return r.span; }, record);
            std::optional<std::u8string> generated;
            if (property && (property->inserted || property->edited))
                generated = property->key + (section.kind == SectionKind::Hikari ? u8"=" : u8": ") + property->value;
            if (style && (style->inserted || style->edited)) {
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
            if (line && options.renderer &&
                (tlYes ? line->originalSpan || !line->translation.empty() || line->unconfirmed
                       : !line->translation.empty())) {
                const std::u8string &newline = newlines.newline;
                std::u8string lines;
                if (tlYes && !line->translation.empty()) {
                    if (!options.hideOriginalOnVideo) {
                        LineRecord original = *line;
                        if (!tlStyle.empty()) {
                            original.style = tlStyle;
                            if (line->unconfirmed)
                                original.effect = u8"\fD";
                        }
                        lines = legacy::assLineText(original) + newline;
                    }
                    LineRecord translated = *line;
                    translated.text = line->translation;
                    lines += legacy::assLineText(translated);
                } else {
                    lines = legacy::assLineText(*line);
                }
                if (line->inserted) {
                    append(newlines.wrap(lines));
                    newlines.openEnd = false;
                } else {
                    newlines.see(span);
                    append(lines);
                    copy(span.offset + span.length, span.terminatorLength);
                }
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
                    append(newlines.wrap(eventText(translated)));
                } else {
                    LineRecord single = *line;
                    if (!tlMode.empty() && !line->translation.empty())
                        single.text = line->translation;
                    append(newlines.wrap(eventText(single)));
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
                    append(eventText(translated));
                } else {
                    LineRecord single = *line;
                    if (hasTranslation)
                        single.text = line->translation;
                    append(eventText(single));
                }
                copy(span.offset + span.length, span.terminatorLength);
            } else if (line && line->edited) {
                append(eventText(*line));
                copy(span.offset + span.length, span.terminatorLength);
            } else if (line && line->originalSpan && options.hideOriginalOnVideo && !tlMode.empty() &&
                       tlMode != u8"Translated" && (!line->translation.empty() || line->unconfirmed) &&
                       !startsWithComment(src, *line->originalSpan)) {
                // With the option on every original is written again as a
                // Comment; the translation line after it keeps its bytes.
                LineRecord original = *line;
                original.style = tlStyle;
                original.comment = true;
                if (line->unconfirmed)
                    original.effect = u8"\fD";
                append(legacy::assLineText(original));
                const std::size_t from = line->originalSpan->offset + line->originalSpan->length;
                copy(from, span.offset + span.length + span.terminatorLength - from);
            } else {
                copy(span.offset, span.length + span.terminatorLength);
            }
            if (&record == lastEntry)
                writeAddedExtradata();
        }
        // The added Script Info at the top, and an added [Hikari] before
        // embedded data, end with a blank line before the next section.
        if (&section == extradataSection && !lastEntry)
            writeAddedExtradata();
        if (takesHeld)
            flushHeld();
        if (added && !editorOnly && (section.kind != SectionKind::Hikari || &section != &document.sections().back()))
            append(newlines.newline);
    }
    if (!beforeEmbedded)
        writeNewExtradataSection();
    if (!held.empty()) {
        // No [Events] section after them: one of their own at the end.
        if (newlines.openEnd)
            append(newlines.newline);
        if (!out.empty() && !endsWithBlankLine(out))
            append(newlines.newline);
        append(u8"[Events]" + newlines.newline);
        newlines.openEnd = false;
        flushHeld();
    }
    return out;
}


namespace {

struct TextLine {
    std::size_t begin = 0, end = 0, next = 0;
};

std::vector<TextLine> textLines(std::string_view text, std::size_t from)
{
    std::vector<TextLine> lines;
    std::size_t pos = from;
    while (pos < text.size()) {
        const std::size_t end = text.find_first_of("\r\n", pos);
        if (end == std::string_view::npos) {
            lines.push_back({pos, text.size(), text.size()});
            break;
        }
        const std::size_t next = text.compare(end, 2, "\r\n") == 0 ? end + 2 : end + 1;
        lines.push_back({pos, end, next});
        pos = next;
    }
    return lines;
}

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && std::ranges::equal(s.substr(0, prefix.size()), prefix, [](char a, char b) {
               return (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b);
           });
}

std::string_view commentText(std::string_view line)
{
    line = trimmed(line);
    if (!line.starts_with(';'))
        return {};
    return trimmed(line.substr(1));
}

}

std::vector<std::byte> withGeneratorComment(std::vector<std::byte> bytes, std::u8string_view generator,
                                            std::u8string_view url)
{
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const std::size_t start = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    const auto lines = textLines(text, start);
    const auto header = std::ranges::find_if(lines, [&](const TextLine &l) {
        return startsWithNoCase(trimmed(text.substr(l.begin, l.end - l.begin)), "[script info]") &&
               trimmed(text.substr(l.begin, l.end - l.begin)).size() == 13;
    });
    if (header == lines.end())
        return bytes;
    std::string newline(text.substr(header->end, header->next - header->end));
    if (newline.empty())
        newline = "\n";
    std::vector<std::pair<std::size_t, std::size_t>> dropped;
    bool afterGenerator = false;
    for (auto it = std::next(header); it != lines.end(); ++it) {
        const std::string_view line = text.substr(it->begin, it->end - it->begin);
        if (trimmed(line).starts_with('['))
            break;
        const std::string_view comment = commentText(line);
        const bool generatorLine = startsWithNoCase(comment, "script generated by");
        if (generatorLine || (afterGenerator && startsWithNoCase(comment, "http")))
            dropped.emplace_back(it->begin, it->next);
        afterGenerator = generatorLine;
    }
    std::string out(text.substr(0, header->next));
    if (header->next == header->end)
        out += newline;
    out += "; ";
    out.append(reinterpret_cast<const char *>(generator.data()), generator.size());
    out += newline;
    out += "; ";
    out.append(reinterpret_cast<const char *>(url.data()), url.size());
    out += newline;
    std::size_t copied = header->next;
    for (const auto &[from, to] : dropped) {
        out.append(text.substr(copied, from - copied));
        copied = to;
    }
    out.append(text.substr(copied));
    std::vector<std::byte> result(out.size());
    std::memcpy(result.data(), out.data(), out.size());
    return result;
}

}
