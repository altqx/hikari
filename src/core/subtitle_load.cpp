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

} // namespace

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
    // Any other extension: LoadTXT, then the format of the first timed Line
    // decides, reloading ASS or SRT content with their readers.
    return found(loadLineFormats(bytes));
}

std::vector<std::byte> encodeSubtitle(const Document &document)
{
    switch (document.format()) {
    case SubtitleFormat::Ass:
        return encodeAss(document);
    case SubtitleFormat::Srt:
        return encodeSrt(document);
    default:
        return encodeLineFormat(document);
    }
}

} // namespace hikari::core
