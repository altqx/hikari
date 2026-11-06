#include "export_flat.h"

#include "hikari/core/ass_save.h"
#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"

#include <cstring>
#include <string>
#include <string_view>

namespace hikari::core::detail {

namespace {

struct DroppedTag {
    u8sv name;
    ExportLoss loss;
};

// YTSubConverter reads tags as \\(\d?[a-z]+) and ignores every name it has no handler for.
constexpr DroppedTag kDroppedTags[] = {
    {u8"p", ExportLoss::Drawing},       {u8"clip", ExportLoss::Clip},       {u8"iclip", ExportLoss::Clip},
    {u8"fr", ExportLoss::Rotation},     {u8"frx", ExportLoss::Rotation},    {u8"fry", ExportLoss::Rotation},
    {u8"frz", ExportLoss::Rotation},    {u8"fax", ExportLoss::Rotation},    {u8"fay", ExportLoss::Rotation},
    {u8"org", ExportLoss::Rotation},    {u8"fscx", ExportLoss::Font},       {u8"fscy", ExportLoss::Font},
    {u8"fsp", ExportLoss::Font},        {u8"bord", ExportLoss::Outline},    {u8"xbord", ExportLoss::Outline},
    {u8"ybord", ExportLoss::Outline},   {u8"shad", ExportLoss::Outline},    {u8"xshad", ExportLoss::Outline},
    {u8"yshad", ExportLoss::Outline},   {u8"be", ExportLoss::Outline},      {u8"blur", ExportLoss::Outline},
    {u8"s", ExportLoss::Formatting},    {u8"a", ExportLoss::Position},      {u8"kf", ExportLoss::Karaoke},
    {u8"ko", ExportLoss::Karaoke},
};

bool lower(char8_t c)
{
    return c >= u8'a' && c <= u8'z';
}

bool digit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

std::uint32_t droppedIn(u8sv block)
{
    std::uint32_t losses = 0;
    for (std::size_t at = block.find(u8'\\'); at != u8sv::npos; at = block.find(u8'\\', at + 1)) {
        std::size_t end = at + 1;
        if (end < block.size() && block[end] == u8'K') {
            losses |= bit(ExportLoss::Karaoke);
            continue;
        }
        if (end < block.size() && digit(block[end]))
            ++end;
        const std::size_t nameStart = at + 1;
        while (end < block.size() && lower(block[end]))
            ++end;
        const u8sv name = block.substr(nameStart, end - nameStart);
        const bool switchedOn = end < block.size() && block[end] >= u8'1' && block[end] <= u8'9';
        for (const auto &tag : kDroppedTags)
            if (tag.name == name && ((name != u8"p" && name != u8"s") || switchedOn))
                losses |= bit(tag.loss);
    }
    return losses;
}

std::uint32_t srv3Losses(const LineRecord &line)
{
    std::uint32_t losses = 0;
    const u8sv text = line.text;
    for (std::size_t open = text.find(u8'{'); open != u8sv::npos; open = text.find(u8'{', open + 1)) {
        const std::size_t close = text.find(u8'}', open);
        losses |= droppedIn(text.substr(open, close == u8sv::npos ? u8sv::npos : close - open));
        if (close == u8sv::npos)
            break;
    }
    const u8sv effect = line.effect;
    if (effect.starts_with(u8"Banner;") || effect.starts_with(u8"Scroll up;") || effect.starts_with(u8"Scroll down;"))
        losses |= bit(ExportLoss::Effect);
    return losses;
}

bool attachmentHeader(std::u16string_view line)
{
    return line == u"[Fonts]" || line == u"[Graphics]";
}

// An encoded attachment line can start with '[', but only a real header has a space or a lowercase letter.
bool realHeader(std::u16string_view line)
{
    return line.starts_with(u'[') && line.ends_with(u']') &&
           line.find_first_of(u" abcdefghijklmnopqrstuvwxyz") != std::u16string_view::npos;
}

// YTSubConverter refuses the uuencoded lines of [Fonts] and [Graphics].
std::u16string withoutAttachments(std::u16string_view script)
{
    std::u16string out;
    out.reserve(script.size());
    bool skipping = false;
    for (std::size_t at = 0; at < script.size();) {
        const std::size_t newline = script.find(u'\n', at);
        const std::size_t next = newline == std::u16string_view::npos ? script.size() : newline + 1;
        std::u16string_view line = script.substr(at, next - at);
        std::u16string_view bare = line;
        while (!bare.empty() && (bare.back() == u'\n' || bare.back() == u'\r' || bare.back() == u' '))
            bare.remove_suffix(1);
        if (attachmentHeader(bare))
            skipping = true;
        else if (realHeader(bare))
            skipping = false;
        if (!skipping)
            out += line;
        at = next;
    }
    return out;
}

}

ExportResult exportSrv3(const Document &document, const ExportOptions &options)
{
    ExportResult result;
    Document chosen = document;
    if (options.lines)
        chosen.removeLinesIf([&](const LineRecord &line) { return !options.lines->contains(line.id); });
    for (const auto *line : chosen.lines()) {
        if (line->comment || line->unparsed) {
            ++result.commentsSkipped;
            continue;
        }
        ++result.linesWritten;
        if (const auto losses = srv3Losses(*line))
            result.notes.push_back({line->id, losses});
    }

    const auto encoded = encodeAss(chosen, AssSaveOptions{.renderer = true});
    const std::u16string script = withoutAttachments(
        toUtf16(u8sv(reinterpret_cast<const char8_t *>(encoded.data()), encoded.size())));
    auto converted = srv3FromAss(script, srv3StyleOptions(document));
    if (!converted) {
        result = {};
        result.refusal = std::move(converted.error());
        return result;
    }
    result.bytes = std::move(*converted);
    return result;
}

}
