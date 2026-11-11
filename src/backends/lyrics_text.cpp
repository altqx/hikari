#include "hikari/backends/lyrics_text.h"

#include "hikari/backends/legacy_text_file.h"

#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#include <string>
#include <string_view>

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

std::optional<std::u16string> toUtf16(std::span<const std::byte> bytes, LyricsEncoding encoding)
{
    if (bytes.empty())
        return std::u16string();
    UErrorCode status = U_ZERO_ERROR;
    UConverter *converter = ucnv_open(icuName(encoding), &status);
    if (U_FAILURE(status))
        return std::nullopt;
    ucnv_setToUCallBack(converter, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    std::u16string out(bytes.size() * 2 + 1, u'\0');
    const int32_t n = ucnv_toUChars(converter, reinterpret_cast<UChar *>(out.data()), static_cast<int32_t>(out.size()),
                                    reinterpret_cast<const char *>(bytes.data()), static_cast<int32_t>(bytes.size()), &status);
    ucnv_close(converter);
    if (U_FAILURE(status))
        return std::nullopt;
    out.resize(static_cast<std::size_t>(n));
    return out;
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
    auto text = toUtf16(bytes, encoding);
    if (!text)
        return std::nullopt;
    std::u16string_view view = *text;
    if (view.starts_with(u'\uFEFF'))
        view.remove_prefix(1);
    UErrorCode status = U_ZERO_ERROR;
    int32_t length = 0;
    std::vector<std::byte> out(view.size() * 3);
    u_strToUTF8(reinterpret_cast<char *>(out.data()), static_cast<int32_t>(out.size()), &length,
                reinterpret_cast<const UChar *>(view.data()), static_cast<int32_t>(view.size()), &status);
    if (U_FAILURE(status))
        return std::nullopt;
    out.resize(static_cast<std::size_t>(length));
    return out;
}

}
