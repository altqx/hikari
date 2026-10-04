#include "hikari/backends/legacy_text_file.h"

#include <QFile>
#include <QStringDecoder>

#include <uchardet.h>

#include <array>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::backends {

namespace {

std::u16string toU16(const QString &s)
{
    return {reinterpret_cast<const char16_t *>(s.utf16()), static_cast<std::size_t>(s.size())};
}

constexpr auto kWhole = QStringConverter::Flag::ConvertInitialBom | QStringConverter::Flag::Stateless;

// A Qt decoder over the bytes; a decoding error is a failed wxCSConv.
std::optional<std::u16string> qtDecode(QStringDecoder decoder, QByteArrayView bytes)
{
    if (!decoder.isValid())
        return std::nullopt;
    const QString text = decoder.decode(bytes);
    if (decoder.hasError())
        return std::nullopt;
    return toU16(text);
}

std::optional<std::u16string> qtDecode(QStringConverter::Encoding encoding, QByteArrayView bytes)
{
    // The caller removed the one BOM legacy removed; anything else is text.
    // Stateless: a sequence cut off at the end is an error, not pending input.
    return qtDecode(QStringDecoder(encoding, kWhole), bytes);
}

#ifdef _WIN32
// wxMBConv_win32::MB2WC: MultiByteToWideChar with MB_ERR_INVALID_CHARS
// (which some code pages refuse, so their files fail as they did).
std::optional<std::u16string> win32Decode(unsigned codePage, QByteArrayView bytes)
{
    if (bytes.isEmpty())
        return std::u16string();
    const int size = static_cast<int>(bytes.size());
    const int n = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
    if (n <= 0)
        return std::nullopt;
    std::u16string out(static_cast<std::size_t>(n), u'\0');
    if (::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size, reinterpret_cast<wchar_t *>(out.data()),
                              n) != n)
        return std::nullopt;
    return out;
}

// wxCSConv's code page for uchardet's names (wxFontMapper / the registry's
// MIME database); 0 when Windows has none.
unsigned codePageOf(std::string_view name)
{
    static constexpr std::array<std::pair<std::string_view, unsigned>, 41> table{{
        {"ISO-8859-1", 28591},      {"ISO-8859-2", 28592},  {"ISO-8859-3", 28593},  {"ISO-8859-4", 28594},
        {"ISO-8859-5", 28595},      {"ISO-8859-6", 28596},  {"ISO-8859-7", 28597},  {"ISO-8859-8", 28598},
        {"ISO-8859-9", 28599},      {"ISO-8859-13", 28603}, {"ISO-8859-15", 28605}, {"WINDOWS-1250", 1250},
        {"WINDOWS-1251", 1251},     {"WINDOWS-1252", 1252}, {"WINDOWS-1253", 1253}, {"WINDOWS-1254", 1254},
        {"WINDOWS-1255", 1255},     {"WINDOWS-1256", 1256}, {"WINDOWS-1257", 1257}, {"WINDOWS-1258", 1258},
        {"IBM852", 852},            {"IBM855", 855},        {"IBM862", 862},        {"IBM865", 865},
        {"IBM866", 866},            {"KOI8-R", 20866},      {"KOI8-U", 21866},      {"MAC-CENTRALEUROPE", 10029},
        {"MAC-CYRILLIC", 10007},    {"TIS-620", 874},       {"BIG5", 950},          {"EUC-JP", 20932},
        {"EUC-KR", 51949},          {"GB18030", 54936},     {"ISO-2022-JP", 50220}, {"ISO-2022-KR", 50225},
        {"ISO-2022-CN", 50227},     {"HZ-GB-2312", 52936},  {"SHIFT_JIS", 932},     {"UHC", 949},
        {"CP949", 949},
    }};
    for (const auto &[n, cp] : table)
        if (n == name)
            return cp;
    return 0;
}
#endif

// The system code page (wxConvLocal): the ANSI code page on Windows, the
// locale's charset (UTF-8) elsewhere.
std::optional<std::u16string> localDecode(QByteArrayView bytes)
{
#ifdef _WIN32
    return win32Decode(CP_ACP, bytes);
#else
    return qtDecode(QStringDecoder(QStringConverter::System, kWhole), bytes);
#endif
}

// A named single- or multi-byte charset (wxCSConv(name)).
std::optional<std::u16string> namedDecode(const std::string &name, QByteArrayView bytes)
{
    if (name == "ASCII")
        return qtDecode(QStringConverter::Latin1, bytes); // uchardet says ASCII only for 7-bit bytes
#ifdef _WIN32
    if (const unsigned cp = codePageOf(name))
        return win32Decode(cp, bytes);
#endif
    return qtDecode(QStringDecoder(name.c_str(), kWhole), bytes);
}

bool wideCharset(std::string_view name)
{
    return name.starts_with("UTF-16") || name.starts_with("UTF-32") || name.starts_with("X-ISO-10646-UCS-4");
}

// UTF-16 and UTF-32: the BOM decides and is removed (F1-utf16-bom); without
// one the platform's converter decided (wx: native little-endian; iconv:
// big-endian).
std::optional<std::u16string> wideDecode(std::string_view name, const QByteArray &bytes, LegacyTextPlatform platform)
{
    using E = QStringConverter::Encoding;
    const bool littleByDefault = platform == LegacyTextPlatform::Windows;
    if (name == "UTF-16LE")
        return qtDecode(E::Utf16LE, bytes.startsWith("\xFF\xFE") ? bytes.sliced(2) : bytes);
    if (name == "UTF-16BE")
        return qtDecode(E::Utf16BE, bytes.startsWith("\xFE\xFF") ? bytes.sliced(2) : bytes);
    if (name == "UTF-32LE")
        return qtDecode(E::Utf32LE, bytes.startsWith(QByteArrayView("\xFF\xFE\0\0", 4)) ? bytes.sliced(4) : bytes);
    if (name == "UTF-32BE")
        return qtDecode(E::Utf32BE, bytes.startsWith(QByteArrayView("\0\0\xFE\xFF", 4)) ? bytes.sliced(4) : bytes);
    if (name == "UTF-32") {
        if (bytes.startsWith(QByteArrayView("\xFF\xFE\0\0", 4)))
            return qtDecode(E::Utf32LE, bytes.sliced(4));
        if (bytes.startsWith(QByteArrayView("\0\0\xFE\xFF", 4)))
            return qtDecode(E::Utf32BE, bytes.sliced(4));
        return qtDecode(littleByDefault ? E::Utf32LE : E::Utf32BE, bytes);
    }
    if (name == "UTF-16") {
        if (bytes.startsWith("\xFF\xFE"))
            return qtDecode(E::Utf16LE, bytes.sliced(2));
        if (bytes.startsWith("\xFE\xFF"))
            return qtDecode(E::Utf16BE, bytes.sliced(2));
        return qtDecode(littleByDefault ? E::Utf16LE : E::Utf16BE, bytes);
    }
    return std::nullopt; // the UCS-4 orders 3412 and 2143: no converter
}

// Windows text-mode reading: each CR LF pair becomes LF.
QByteArray foldCrlf(const QByteArray &bytes)
{
    QByteArray out;
    out.reserve(bytes.size());
    for (qsizetype i = 0; i < bytes.size(); ++i)
        if (!(bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n'))
            out += bytes[i];
    return out;
}

} // namespace

bool legacyIsUtf8WithoutBom(const QByteArray &bytes)
{
    const auto *buf = reinterpret_cast<const unsigned char *>(bytes.constData());
    const std::size_t size = static_cast<std::size_t>(bytes.size());
    bool onlySawAscii = true;
    std::size_t pos = 0;
    const auto isContinuation = [](unsigned char ch) { return ch >= 0x80 && ch <= 0xBF; };
    while (pos < size) {
        const unsigned char first = buf[pos++];
        if (first <= 0x7F)
            continue;
        onlySawAscii = false;
        if (first >= 0xC2 && first <= 0xDF) {
            if (pos >= size || !isContinuation(buf[pos++]))
                return false;
        } else if (first >= 0xE0 && first <= 0xEF) {
            if (pos + 1 >= size)
                return false;
            const unsigned char second = buf[pos++];
            const unsigned char third = buf[pos++];
            if (!isContinuation(third) || (first == 0xE0   ? second < 0xA0 || second > 0xBF
                                           : first == 0xED ? second < 0x80 || second > 0x9F
                                                           : !isContinuation(second)))
                return false;
        } else if (first >= 0xF0 && first <= 0xF4) {
            if (pos + 2 >= size)
                return false;
            const unsigned char second = buf[pos++];
            const unsigned char third = buf[pos++];
            const unsigned char fourth = buf[pos++];
            if (!isContinuation(third) || !isContinuation(fourth) ||
                (first == 0xF0   ? second < 0x90 || second > 0xBF
                 : first == 0xF4 ? second < 0x80 || second > 0x8F
                                 : !isContinuation(second)))
                return false;
        } else {
            return false;
        }
    }
    return !onlySawAscii;
}

std::string legacyDetectCharset(const QByteArray &bytes)
{
    uchardet_t detector = uchardet_new();
    if (!detector)
        return {};
    std::string result;
    if (uchardet_handle_data(detector, bytes.constData(), static_cast<std::size_t>(bytes.size())) == 0) {
        uchardet_data_end(detector);
        if (const char *encoding = uchardet_get_charset(detector); encoding && *encoding)
            result = encoding;
    }
    uchardet_delete(detector);
    // FileOpen: result.MakeUpper().
    for (char &c : result)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return result;
}

std::optional<std::u16string> decodeLegacyText(const QByteArray &bytes, LegacyTextPlatform platform)
{
    const bool bom = bytes.startsWith("\xEF\xBB\xBF");
    const bool utf8 = bom || legacyIsUtf8WithoutBom(bytes);
    const std::string charset = utf8 ? std::string() : legacyDetectCharset(bytes);
    const bool wide = wideCharset(charset);
    // The text-mode read happened before decoding, on the bytes.
    const QByteArray data = platform == LegacyTextPlatform::Windows && !wide ? foldCrlf(bytes) : bytes;
    std::optional<std::u16string> text;
    if (utf8)
        text = qtDecode(QStringConverter::Utf8, bom ? QByteArrayView(data).sliced(3) : QByteArrayView(data)); // wxConvAuto
    else if (wide)
        text = wideDecode(charset, data, platform);
    else if (charset.empty())
        text = localDecode(data);
    else
        text = namedDecode(charset, data);
    if (!text || text->empty())
        return std::nullopt; // FileOpen: riddenText->empty() is a failure
    return text;
}

std::optional<std::u16string> readLegacyTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return decodeLegacyText(file.readAll());
}

} // namespace hikari::backends
