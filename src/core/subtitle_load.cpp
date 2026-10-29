#include "hikari/core/subtitle_load.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"

namespace hikari::core {

namespace {

std::optional<LoadResult> found(LoadResult result)
{
    if (result.document.lines().empty())
        return std::nullopt;
    return result;
}

}

std::optional<LoadResult> loadSubtitle(std::span<const std::byte> bytes, std::u8string_view extension)
{
    if (extension == u8"ass" || extension == u8"ssa") {
        if (auto r = found(loadAss(bytes)))
            return r;
        if (auto r = found(loadSrt(bytes)))
            return r;
        return found(loadLineFormats(bytes));
    }
    if (extension == u8"srt") {
        if (auto r = found(loadSrt(bytes)))
            return r;
        if (auto r = found(loadAss(bytes)))
            return r;
        return found(loadLineFormats(bytes));
    }
    // The first timed Line's format decides; ASS or SRT content is reloaded by its reader.
    return found(loadLineFormats(bytes));
}

std::vector<std::byte> encodeSubtitle(const Document &document)
{
    return encodeSubtitle(document, AssSaveOptions{});
}

std::vector<std::byte> encodeSubtitle(const Document &document, const AssSaveOptions &options)
{
    switch (document.format()) {
    case SubtitleFormat::Ass:
        return encodeAss(document, options);
    case SubtitleFormat::Srt:
        return encodeSrt(document);
    default:
        return encodeLineFormat(document);
    }
}

}
