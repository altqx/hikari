#include "hikari/core/ass_attachments.h"

#include <algorithm>
#include <variant>

namespace hikari::core {

namespace {

std::string_view recordText(const Document &document, const Record &record)
{
    const SourceSpan &span = std::visit([](const auto &r) -> const SourceSpan & { return r.span; }, record);
    const auto &bytes = document.source().bytes;
    if (span.offset > bytes.size() || span.length > bytes.size() - span.offset)
        return {};
    return std::string_view(reinterpret_cast<const char *>(bytes.data()) + span.offset, span.length);
}

std::string_view trimmed(std::string_view s)
{
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return s;
}

} // namespace

std::vector<std::byte> decodeAssAttachment(std::string_view text)
{
    std::vector<std::byte> out;
    const std::size_t size = text.size();
    if (size % 4 == 1)
        return out;
    out.reserve(size / 4 * 3 + 2);
    const auto at = [&](std::size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(text[i]) - 33u) & 63u; };
    const auto push = [&](unsigned v) { out.push_back(static_cast<std::byte>(v & 0xFFu)); };
    const std::size_t whole = size & ~std::size_t(3);
    for (std::size_t i = 0; i < whole; i += 4) {
        const unsigned a = at(i), b = at(i + 1), c = at(i + 2), d = at(i + 3);
        push((a << 2) | (b >> 4));
        push(((b & 15u) << 4) | (c >> 2));
        push(((c & 3u) << 6) | d);
    }
    if (size % 4 >= 2) {
        const unsigned a = at(whole), b = at(whole + 1);
        push((a << 2) | (b >> 4));
        if (size % 4 == 3)
            push(((b & 15u) << 4) | (at(whole + 2) >> 2));
    }
    return out;
}

bool continuesAttachment(const Section &section)
{
    if (section.kind != SectionKind::Unknown || section.header.empty() || section.header.front() != u8'[')
        return false;
    return std::ranges::all_of(section.header, [](char8_t c) { return c >= 33 && c <= 96; });
}

namespace {

// Calls `line` with each line of the [Fonts] sections in order, the header
// of a section that continues one included, and `end` where a run of them
// ends.
template <typename Line, typename End>
void fontsLines(const Document &document, Line &&line, End &&end)
{
    bool inFonts = false;
    for (const auto &section : document.sections()) {
        if (section.kind == SectionKind::Fonts) {
            if (inFonts)
                end();
            inFonts = true;
        } else if (inFonts && continuesAttachment(section)) {
            line(std::string_view(reinterpret_cast<const char *>(section.header.data()), section.header.size()),
                 section.headerSpan ? section.headerSpan->length : section.header.size());
        } else {
            if (inFonts)
                end();
            inFonts = false;
            continue;
        }
        for (const auto &record : section.records)
            line(recordText(document, record), std::visit([](const auto &r) { return r.span.length; }, record));
    }
    if (inFonts)
        end();
}

} // namespace

std::vector<AttachedFont> attachedFonts(const Document &document)
{
    std::vector<AttachedFont> fonts;
    std::string name, data;
    bool inFont = false;
    const auto finish = [&] {
        if (inFont) {
            auto bytes = decodeAssAttachment(data);
            if (!bytes.empty())
                fonts.push_back({name, std::move(bytes)});
        }
        inFont = false;
        name.clear();
        data.clear();
    };
    fontsLines(
        document,
        [&](std::string_view text, std::size_t) {
            const std::string_view line = trimmed(text);
            if (line.empty())
                return;
            if (line.starts_with("fontname:")) {
                finish();
                inFont = true;
                name = std::string(trimmed(line.substr(9)));
            } else if (inFont) {
                data += line;
            }
        },
        finish); // a font ends with its sections
    return fonts;
}

AttachedFontsKey attachedFontsKey(const Document &document)
{
    AttachedFontsKey key;
    fontsLines(
        document,
        [&](std::string_view, std::size_t length) {
            ++key.records;
            key.bytes += length;
        },
        [] {});
    return key;
}

} // namespace hikari::core
