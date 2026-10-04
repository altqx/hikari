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
#else
#include <cerrno>
#include <iconv.h>
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

// wxCSConv's last resort: with no converter for the name (or for ASCII and
// ISO-8859-1, which it never converts), each byte is the character.
std::optional<std::u16string> latin1(QByteArrayView bytes)
{
    return toU16(QString::fromLatin1(bytes));
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

// wxCSConv(name) on Windows: wxFontMapperBase::CharsetToEncoding(name) then
// wxEncodingToCodepage (wxWidgets src/common/fmapbase.cpp, src/msw/utils.cpp),
// for the names uchardet 0.0.8 gives. 0: wx knows no encoding for the name
// (IBM852, IBM855, IBM865, IBM866, MAC-CENTRALEUROPE, MAC-CYRILLIC, TIS-620,
// GB18030, HZ-GB-2312, EUC-TW, ISO-2022-CN, ISO-2022-KR, UHC, VISCII,
// GEORGIAN-*, ISO-8859-16), so there is no converter.
unsigned codePageOf(std::string_view name)
{
    static constexpr std::array<std::pair<std::string_view, unsigned>, 26> table{{
        {"ISO-8859-2", 28592},   {"ISO-8859-3", 28593},   {"ISO-8859-4", 28594},   {"ISO-8859-5", 28595},
        {"ISO-8859-6", 28596},   {"ISO-8859-7", 28597},   {"ISO-8859-8", 28598},   {"ISO-8859-9", 28599},
        {"ISO-8859-10", 28600},  {"ISO-8859-11", 874},    {"ISO-8859-13", 28603},  {"ISO-8859-15", 28605},
        {"WINDOWS-1250", 1250},  {"WINDOWS-1251", 1251},  {"WINDOWS-1252", 1252},  {"WINDOWS-1253", 1253},
        {"WINDOWS-1254", 1254},  {"WINDOWS-1255", 1255},  {"WINDOWS-1256", 1256},  {"WINDOWS-1257", 1257},
        {"WINDOWS-1258", 1258},  {"KOI8-R", 20866},       {"SHIFT_JIS", 932},      {"BIG5", 950},
        {"EUC-KR", 949},         {"EUC-JP", 20932},
    }};
    for (const auto &[n, cp] : table)
        if (n == name)
            return cp;
    if (name == "ISO-2022-JP")
        return 50222; // which refuses MB_ERR_INVALID_CHARS, so the read fails
    if (name == "UTF-8")
        return CP_UTF8; // strict: invalid bytes fail
    return 0;
}

// The encodings wxEncodingConverter has its own tables for (encconv.cpp),
// where wxCSConv went when wxEncodingToCodepage found the code page missing
// (IsValidCodePage or GetCPInfo failed).
bool wxTableEncoding(std::string_view name)
{
    return name.starts_with("ISO-8859-") || name.starts_with("WINDOWS-125") || name == "KOI8-R";
}
#else
// wxMBConv_iconv: the wchar_t charset wx picked (UTF-32 in this byte order).
constexpr const char *kIconvWide = Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? "UTF-32LE" : "UTF-32BE";

struct Iconv {
    iconv_t cd;
    explicit Iconv(const char *from) : cd(::iconv_open(kIconvWide, from)) {}
    ~Iconv()
    {
        if (ok())
            ::iconv_close(cd);
    }
    Iconv(const Iconv &) = delete;
    Iconv &operator=(const Iconv &) = delete;
    bool ok() const { return cd != reinterpret_cast<iconv_t>(-1); }
};

// wxString(buf, wxCSConv, length) through wxMBConv::cMB2WC and
// wxMBConv_iconv::ToWChar: a first pass into a 256-character scratch buffer
// counts the characters, then a second pass on the same, never reset, iconv
// handle fills a buffer of exactly that size. Any iconv failure (EILSEQ,
// EINVAL for a sequence cut off at the end, E2BIG when the second pass gives
// more than the first, as a charset with a pending character such as
// WINDOWS-1258 or a stateful one such as ISO-2022-JP left mid-shift can) fails
// the whole conversion.
std::optional<std::u32string> iconvTwoPass(iconv_t cd, QByteArrayView bytes)
{
    std::size_t count = 0;
    {
        char *in = const_cast<char *>(bytes.data());
        std::size_t inLeft = static_cast<std::size_t>(bytes.size());
        std::size_t result = 0;
        std::array<char32_t, 256> scratch{};
        do {
            char *out = reinterpret_cast<char *>(scratch.data());
            std::size_t outLeft = sizeof(scratch);
            result = ::iconv(cd, &in, &inLeft, &out, &outLeft);
            count += (sizeof(scratch) - outLeft) / sizeof(char32_t);
        } while (result == static_cast<std::size_t>(-1) && errno == E2BIG);
        if (result == static_cast<std::size_t>(-1))
            return std::nullopt;
    }
    std::u32string text(count, U'\0');
    char *in = const_cast<char *>(bytes.data());
    std::size_t inLeft = static_cast<std::size_t>(bytes.size());
    char32_t scratch = 0;
    char *out = reinterpret_cast<char *>(count ? text.data() : &scratch);
    std::size_t outLeft = count * sizeof(char32_t);
    if (::iconv(cd, &in, &inLeft, &out, &outLeft) == static_cast<std::size_t>(-1))
        return std::nullopt;
    text.resize(count - outLeft / sizeof(char32_t));
    return text;
}

// F1-sjis-backslash (R3-hang-crash-loss): the character this charset's iconv
// gives the single ASCII byte, when that is not the ASCII character (glibc's
// SHIFT_JIS reads 0x5C as U+00A5 and 0x7E as U+203E; of uchardet's names only
// SHIFT_JIS does, and no other SHIFT_JIS sequence gives those two).
char32_t asciiAs(const std::string &name, char byte)
{
    Iconv probe(name.c_str());
    if (!probe.ok())
        return U'\0';
    char *in = &byte;
    std::size_t inLeft = 1;
    char32_t c = 0;
    char *out = reinterpret_cast<char *>(&c);
    std::size_t outLeft = sizeof(c);
    if (::iconv(probe.cd, &in, &inLeft, &out, &outLeft) == static_cast<std::size_t>(-1) || outLeft != 0 ||
        c == static_cast<char32_t>(byte))
        return U'\0';
    return c;
}

// wxCSConv(name) on Linux: glibc iconv as wxMBConv_iconv used it. A name
// iconv cannot open (of uchardet's, HZ-GB-2312) is one wx has no alias or
// table for either, so it reads as Latin-1.
std::optional<std::u16string> iconvDecode(const std::string &name, QByteArrayView bytes)
{
    const Iconv cd(name.c_str());
    if (!cd.ok())
        return latin1(bytes);
    auto text = iconvTwoPass(cd.cd, bytes);
    if (!text)
        return std::nullopt;
    // Legacy then read every "\\" in such a file as U+00A5, so a file
    // replace rewrote every tag; the ASCII characters are kept here.
    for (const char ascii : {'\\', '~'})
        if (const char32_t as = asciiAs(name, ascii))
            for (char32_t &c : *text)
                if (c == as)
                    c = static_cast<char32_t>(ascii);
    return toU16(QString::fromUcs4(text->data(), static_cast<qsizetype>(text->size())));
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
    // wxCSConv converts neither: ISO-8859-1 is its own encoding and ASCII is
    // wxFONTENCODING_DEFAULT, which it takes as ISO-8859-1.
    if (name == "ASCII" || name == "ISO-8859-1")
        return latin1(bytes);
#ifdef _WIN32
    const unsigned cp = codePageOf(name);
    if (!cp)
        return latin1(bytes);
    CPINFO info;
    if (!::IsValidCodePage(cp) || !::GetCPInfo(cp, &info)) {
        // wxEncodingToCodepage gave -1: wxMBConv_wxwin's own table where it
        // has one (Qt's decoder for the name stands in), else Latin-1.
        if (wxTableEncoding(name))
            if (QStringDecoder decoder(name.c_str(), kWhole); decoder.isValid())
                return qtDecode(std::move(decoder), bytes);
        return latin1(bytes);
    }
    return win32Decode(cp, bytes);
#else
    return iconvDecode(name, bytes);
#endif
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
