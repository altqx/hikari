#include "hikari/core/ass_attachments.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <variant>

namespace hikari::core {

namespace {

std::string_view recordText(const Document &document, const Record &record)
{
    if (const auto *opaque = std::get_if<OpaqueRecord>(&record); opaque && opaque->inserted)
        return std::string_view(reinterpret_cast<const char *>(opaque->inserted->data()), opaque->inserted->size());
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

}

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

std::size_t recordLength(const Record &record)
{
    if (const auto *opaque = std::get_if<OpaqueRecord>(&record); opaque && opaque->inserted)
        return opaque->inserted->size();
    return std::visit([](const auto &r) { return r.span.length; }, record);
}

std::optional<AttachmentKind> embeddedKind(SectionKind kind)
{
    if (kind == SectionKind::Fonts)
        return AttachmentKind::Font;
    if (kind == SectionKind::Graphics)
        return AttachmentKind::Graphic;
    return std::nullopt;
}

template <typename Line, typename End>
void embeddedLines(const Document &document, bool graphics, Line &&line, End &&end)
{
    std::optional<AttachmentKind> in;
    for (const auto &section : document.sections()) {
        const auto kind = embeddedKind(section.kind);
        if (kind && (graphics || *kind == AttachmentKind::Font)) {
            if (in)
                end();
            in = kind;
        } else if (in && continuesAttachment(section)) {
            line(*in, std::string_view(reinterpret_cast<const char *>(section.header.data()), section.header.size()),
                 section.headerSpan ? section.headerSpan->length : section.header.size());
        } else {
            if (in)
                end();
            in.reset();
            continue;
        }
        for (const auto &record : section.records)
            line(*in, recordText(document, record), recordLength(record));
    }
    if (in)
        end();
}

template <typename Line, typename End>
void fontsLines(const Document &document, Line &&line, End &&end)
{
    embeddedLines(
        document, false, [&](AttachmentKind, std::string_view text, std::size_t length) { line(text, length); }, end);
}

std::string_view entryPrefix(AttachmentKind kind)
{
    return kind == AttachmentKind::Font ? "fontname:" : "filename:";
}

template <typename Entry>
void eachAttachment(const Document &document, Entry &&entry)
{
    std::optional<AttachmentKind> kind;
    std::string name, data;
    const auto finish = [&] {
        if (kind)
            entry(*kind, name, data);
        kind.reset();
        name.clear();
        data.clear();
    };
    embeddedLines(
        document, true,
        [&](AttachmentKind in, std::string_view text, std::size_t) {
            const std::string_view line = trimmed(text);
            if (line.empty())
                return;
            if (line.starts_with(entryPrefix(in))) {
                finish();
                kind = in;
                name = std::string(trimmed(line.substr(entryPrefix(in).size())));
            } else if (kind) {
                data += line;
            }
        },
        finish);
}

std::size_t decodedSize(std::size_t encoded)
{
    return encoded / 4 * 3 + (encoded % 4 >= 2 ? encoded % 4 - 1 : 0);
}

constexpr std::uint32_t tag(const char (&name)[5])
{
    return std::uint32_t(std::uint8_t(name[0])) << 24 | std::uint32_t(std::uint8_t(name[1])) << 16 |
           std::uint32_t(std::uint8_t(name[2])) << 8 | std::uint32_t(std::uint8_t(name[3]));
}

std::uint32_t bigEndian(const std::vector<std::byte> &bytes, std::size_t at, std::size_t width)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value = (value << 8) | std::to_integer<std::uint32_t>(bytes[at + i]);
    return value;
}

}

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

std::vector<Attachment> attachments(const Document &document)
{
    std::vector<Attachment> out;
    eachAttachment(document, [&](AttachmentKind kind, const std::string &name, const std::string &data) {
        out.push_back({kind, name, data.size() % 4 == 1 ? 0 : decodedSize(data.size())});
    });
    return out;
}

std::vector<std::byte> attachmentBytes(const Document &document, std::size_t index)
{
    std::vector<std::byte> bytes;
    std::size_t at = 0;
    eachAttachment(document, [&](AttachmentKind, const std::string &, const std::string &data) {
        if (at++ == index)
            bytes = decodeAssAttachment(data);
    });
    return bytes;
}

std::u8string attachmentName(AttachmentKind kind, std::u8string_view fileName)
{
    if (kind != AttachmentKind::Font)
        return std::u8string(fileName);
    const auto dot = fileName.rfind(u8'.');
    if (dot == std::u8string_view::npos)
        return std::u8string(fileName) + u8"_0";
    return std::u8string(fileName.substr(0, dot)) + u8"_0" + std::u8string(fileName.substr(dot));
}

std::vector<std::u8string> encodeAssAttachment(const std::vector<std::byte> &bytes)
{
    std::vector<std::u8string> lines(1);
    const auto put = [&](unsigned v) {
        if (lines.back().size() == 80)
            lines.emplace_back();
        lines.back() += static_cast<char8_t>((v & 63u) + 33u);
    };
    for (std::size_t pos = 0; pos < bytes.size(); pos += 3) {
        unsigned src[3] = {0, 0, 0};
        for (std::size_t k = 0; k < 3 && pos + k < bytes.size(); ++k)
            src[k] = std::to_integer<unsigned>(bytes[pos + k]);
        const unsigned dst[4] = {src[0] >> 2, ((src[0] & 3u) << 4) | (src[1] >> 4), ((src[1] & 15u) << 2) | (src[2] >> 6),
                                 src[2] & 63u};
        const std::size_t chars = std::min<std::size_t>(bytes.size() - pos + 1, 4);
        for (std::size_t k = 0; k < chars; ++k)
            put(dst[k]);
    }
    if (lines.back().empty())
        lines.pop_back();
    return lines;
}

bool embeddingRestricted(const std::vector<std::byte> &font)
{
    std::size_t base = 0;
    if (font.size() >= 16 && bigEndian(font, 0, 4) == tag("ttcf"))
        base = bigEndian(font, 12, 4);
    if (base > font.size() || font.size() - base < 12)
        return false;
    const std::size_t tables = bigEndian(font, base + 4, 2);
    for (std::size_t i = 0; i < tables; ++i) {
        const std::size_t record = base + 12 + i * 16;
        if (record + 16 > font.size())
            return false;
        if (bigEndian(font, record, 4) != tag("OS/2"))
            continue;
        const std::size_t table = bigEndian(font, record + 8, 4);
        if (table > font.size() || font.size() - table < 10)
            return false;
        const std::uint32_t fsType = bigEndian(font, table + 8, 2);
        return (fsType & 0x2u) && !(fsType & 0xCu);
    }
    return false;
}

namespace {

bool blankRecord(const Document &document, const Record &record)
{
    return trimmed(recordText(document, record)).empty();
}

void mergeContinuations(std::vector<Section> &sections)
{
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (!embeddedKind(sections[i].kind))
            continue;
        while (i + 1 < sections.size() && continuesAttachment(sections[i + 1])) {
            Section next = std::move(sections[i + 1]);
            sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(i + 1));
            if (next.headerSpan)
                sections[i].records.push_back(OpaqueRecord{*next.headerSpan});
            else
                sections[i].records.push_back(OpaqueRecord{SourceSpan{}, next.header});
            for (auto &record : next.records)
                sections[i].records.push_back(std::move(record));
        }
    }
}

std::size_t recordEnd(const Record &record)
{
    return std::visit([](const auto &r) { return r.span.offset + r.span.length + r.span.terminatorLength; }, record);
}

}

bool Document::addAttachment(AttachmentKind kind, std::u8string_view name, const std::vector<std::byte> &bytes)
{
    if (m_format != SubtitleFormat::Ass)
        return false;
    mergeContinuations(m_sections);
    const SectionKind sectionKind = kind == AttachmentKind::Font ? SectionKind::Fonts : SectionKind::Graphics;
    Section *section = nullptr;
    for (auto &candidate : m_sections)
        if (candidate.kind == sectionKind)
            section = &candidate;
    if (!section)
        section = &m_sections.emplace_back(
            Section{sectionKind, kind == AttachmentKind::Font ? u8"[Fonts]" : u8"[Graphics]", std::nullopt, {}});
    std::size_t at = section->records.size();
    while (at > 0 && blankRecord(*this, section->records[at - 1]))
        --at;
    std::size_t offset = section->headerSpan
                             ? section->headerSpan->offset + section->headerSpan->length + section->headerSpan->terminatorLength
                             : 0;
    for (std::size_t i = 0; i < at; ++i)
        offset = std::max(offset, recordEnd(section->records[i]));
    std::vector<Record> added;
    const auto prefix = entryPrefix(kind);
    added.push_back(OpaqueRecord{SourceSpan{offset, 0, 0},
                                 std::u8string(prefix.begin(), prefix.end()) + u8" " + std::u8string(name)});
    for (auto &line : encodeAssAttachment(bytes))
        added.push_back(OpaqueRecord{SourceSpan{offset, 0, 0}, std::move(line)});
    section->records.insert(section->records.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(added.begin()),
                            std::make_move_iterator(added.end()));
    return true;
}

bool Document::removeAttachment(std::size_t index)
{
    mergeContinuations(m_sections);
    std::size_t seen = 0;
    for (std::size_t s = 0; s < m_sections.size(); ++s) {
        const auto kind = embeddedKind(m_sections[s].kind);
        if (!kind)
            continue;
        auto &records = m_sections[s].records;
        std::vector<std::size_t> starts;
        for (std::size_t r = 0; r < records.size(); ++r)
            if (trimmed(recordText(*this, records[r])).starts_with(entryPrefix(*kind)))
                starts.push_back(r);
        if (index >= seen + starts.size()) {
            seen += starts.size();
            continue;
        }
        const std::size_t entry = index - seen;
        const std::size_t from = starts[entry];
        std::size_t to = entry + 1 < starts.size() ? starts[entry + 1] : records.size();
        if (entry + 1 == starts.size())
            while (to > from + 1 && blankRecord(*this, records[to - 1]))
                --to;
        records.erase(records.begin() + static_cast<std::ptrdiff_t>(from), records.begin() + static_cast<std::ptrdiff_t>(to));
        if (starts.size() == 1)
            m_sections.erase(m_sections.begin() + static_cast<std::ptrdiff_t>(s));
        return true;
    }
    return false;
}

}
