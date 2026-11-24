#include "hikari/backends/lyrics_text.h"

#include "hikari/backends/legacy_text_file.h"
#include "hikari/backends/text_encoding.h"

namespace hikari::backends {

namespace {

using core::LyricsEncoding;

const char *icuName(LyricsEncoding encoding)
{
    switch (encoding) {
    case LyricsEncoding::Utf8: return "UTF-8";
    case LyricsEncoding::Utf16Le: return "UTF-16LE";
    case LyricsEncoding::Utf16Be: return "UTF-16BE";
    case LyricsEncoding::Gb18030: return "GB18030";
    case LyricsEncoding::Big5: return "Big5";
    case LyricsEncoding::ShiftJis: return "Shift_JIS";
    case LyricsEncoding::EucKr: return "EUC-KR";
    }
    return "UTF-8";
}

}

LyricsEncoding detectLyricsEncoding(std::span<const std::byte> bytes)
{
    if (const auto evident = core::evidentLyricsEncoding(bytes))
        return *evident;
    const std::string name = legacyDetectCharset(
            QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    if (name == "BIG5")
        return LyricsEncoding::Big5;
    if (name == "SHIFT_JIS")
        return LyricsEncoding::ShiftJis;
    if (name == "EUC-KR" || name == "UHC")
        return LyricsEncoding::EucKr;
    return LyricsEncoding::Gb18030;
}

std::optional<std::vector<std::byte>> lyricsUtf8(std::span<const std::byte> bytes, LyricsEncoding encoding)
{
    return utf8From(bytes, icuName(encoding));
}

}
