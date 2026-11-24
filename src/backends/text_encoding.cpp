#include "hikari/backends/text_encoding.h"

#include "hikari/backends/legacy_text_file.h"
#include "hikari/core/lyrics_import.h"

#include <unicode/ucnv.h>
#include <unicode/ustring.h>

#include <array>

namespace hikari::backends {

namespace {

constexpr std::array<TextEncoding, 17> kReopen{{
    {"UTF-8", "UTF-8"},
    {"UTF-16LE", "UTF-16 LE"},
    {"UTF-16BE", "UTF-16 BE"},
    {"Shift_JIS", "Shift-JIS"},
    {"GB18030", "GB18030"},
    {"Big5", "Big5"},
    {"EUC-KR", "EUC-KR"},
    {"windows-1250", "Windows-1250"},
    {"windows-1251", "Windows-1251"},
    {"windows-1252", "Windows-1252"},
    {"windows-1253", "Windows-1253"},
    {"windows-1254", "Windows-1254"},
    {"windows-1257", "Windows-1257"},
    {"ISO-8859-1", "ISO-8859-1"},
    {"ISO-8859-2", "ISO-8859-2"},
    {"ISO-8859-15", "ISO-8859-15"},
    {"KOI8-R", "KOI8-R"},
}};

bool readable(const std::string &name)
{
    UErrorCode status = U_ZERO_ERROR;
    UConverter *converter = ucnv_open(name.c_str(), &status);
    if (converter)
        ucnv_close(converter);
    return U_SUCCESS(status);
}

std::optional<std::u16string> toUtf16(std::span<const std::byte> bytes, const std::string &encoding)
{
    if (bytes.empty())
        return std::u16string();
    UErrorCode status = U_ZERO_ERROR;
    UConverter *converter = ucnv_open(encoding.c_str(), &status);
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

void substituteAndRecord(const void *context, UConverterFromUnicodeArgs *args, const UChar *units, int32_t length,
                         UChar32 codePoint, UConverterCallbackReason reason, UErrorCode *status)
{
    if (reason == UCNV_UNASSIGNED || reason == UCNV_ILLEGAL || reason == UCNV_IRREGULAR) {
        auto &lost = *static_cast<std::u32string *>(const_cast<void *>(context));
        if (lost.find(char32_t(codePoint)) == std::u32string::npos)
            lost.push_back(char32_t(codePoint));
    }
    UCNV_FROM_U_CALLBACK_SUBSTITUTE(nullptr, args, units, length, codePoint, reason, status);
}

}

std::span<const TextEncoding> reopenEncodings()
{
    return kReopen;
}

std::string encodingLabel(std::string_view name)
{
    const std::string wanted(name);
    for (const auto &e : kReopen)
        if (ucnv_compareNames(std::string(e.name).c_str(), wanted.c_str()) == 0)
            return std::string(e.label);
    return wanted;
}

std::optional<std::string> suggestedEncoding(std::span<const std::byte> bytes)
{
    if (const auto evident = core::evidentLyricsEncoding(bytes)) {
        if (*evident == core::LyricsEncoding::Utf16Le)
            return "UTF-16LE";
        if (*evident == core::LyricsEncoding::Utf16Be)
            return "UTF-16BE";
        return std::nullopt;
    }
    std::string name =
            legacyDetectCharset(QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    if (name.empty() || name == "ASCII" || name == "UTF-8" || !readable(name))
        return std::nullopt;
    for (const auto &e : kReopen)
        if (ucnv_compareNames(std::string(e.name).c_str(), name.c_str()) == 0)
            return std::string(e.name);
    return name;
}

std::optional<std::vector<std::byte>> utf8From(std::span<const std::byte> bytes, std::string_view encoding)
{
    auto text = toUtf16(bytes, std::string(encoding));
    if (!text)
        return std::nullopt;
    std::u16string_view view = *text;
    if (view.starts_with(u'﻿'))
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

std::optional<Encoded> encodeUtf8(std::span<const std::byte> utf8, std::string_view encoding)
{
    UErrorCode status = U_ZERO_ERROR;
    int32_t length = 0;
    const auto *source = reinterpret_cast<const char *>(utf8.data());
    u_strFromUTF8(nullptr, 0, &length, source, static_cast<int32_t>(utf8.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        return std::nullopt;
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    status = U_ZERO_ERROR;
    u_strFromUTF8(reinterpret_cast<UChar *>(text.data()), length, nullptr, source, static_cast<int32_t>(utf8.size()),
                  &status);
    if (U_FAILURE(status))
        return std::nullopt;
    if (text.starts_with(u'\uFEFF'))
        text.erase(0, 1);
    const std::string name(encoding);
    if (ucnv_compareNames(name.c_str(), "UTF-16LE") == 0 || ucnv_compareNames(name.c_str(), "UTF-16BE") == 0)
        text.insert(text.begin(), u'\uFEFF');

    UConverter *converter = ucnv_open(name.c_str(), &status);
    if (U_FAILURE(status))
        return std::nullopt;
    Encoded encoded;
    ucnv_setFromUCallBack(converter, substituteAndRecord, &encoded.unencodable, nullptr, nullptr, &status);
    encoded.bytes.resize(static_cast<std::size_t>(
        UCNV_GET_MAX_BYTES_FOR_STRING(static_cast<int32_t>(text.size()), ucnv_getMaxCharSize(converter))));
    const int32_t written =
        ucnv_fromUChars(converter, reinterpret_cast<char *>(encoded.bytes.data()), static_cast<int32_t>(encoded.bytes.size()),
                        reinterpret_cast<const UChar *>(text.data()), static_cast<int32_t>(text.size()), &status);
    ucnv_close(converter);
    if (U_FAILURE(status))
        return std::nullopt;
    encoded.bytes.resize(static_cast<std::size_t>(written));
    return encoded;
}

}
