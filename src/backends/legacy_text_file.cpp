#include "hikari/backends/legacy_text_file.h"

#include <QFile>
#include <QStringDecoder>

#include <uchardet.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace hikari::backends {

bool legacyIsUtf8WithoutBom(QByteArrayView bytes)
{
    // A transcription of OpenWrite::IsUTF8withoutBOM.
    bool onlySawAscii = true;
    qsizetype pos = 0;
    const qsizetype size = bytes.size();
    const auto at = [&](qsizetype i) { return static_cast<unsigned char>(bytes[i]); };
    const auto isContinuation = [](unsigned char ch) { return ch >= 0x80 && ch <= 0xBF; };
    while (pos < size) {
        const unsigned char first = at(pos++);
        if (first <= 0x7F)
            continue;
        onlySawAscii = false;
        if (first >= 0xC2 && first <= 0xDF) {
            if (pos >= size || !isContinuation(at(pos++)))
                return false;
        } else if (first >= 0xE0 && first <= 0xEF) {
            if (pos + 1 >= size)
                return false;
            const unsigned char second = at(pos++);
            const unsigned char third = at(pos++);
            if (!isContinuation(third) || (first == 0xE0   ? second < 0xA0 || second > 0xBF
                                           : first == 0xED ? second < 0x80 || second > 0x9F
                                                           : !isContinuation(second)))
                return false;
        } else if (first >= 0xF0 && first <= 0xF4) {
            if (pos + 2 >= size)
                return false;
            const unsigned char second = at(pos++);
            const unsigned char third = at(pos++);
            const unsigned char fourth = at(pos++);
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

QByteArray legacyDetectCharset(QByteArrayView bytes)
{
    const std::unique_ptr<std::remove_pointer_t<uchardet_t>, decltype(&uchardet_delete)> detector(uchardet_new(),
                                                                                                 &uchardet_delete);
    if (!detector)
        return {};
    if (uchardet_handle_data(detector.get(), bytes.data(), static_cast<size_t>(bytes.size())) != 0)
        return {};
    uchardet_data_end(detector.get());
    const char *encoding = uchardet_get_charset(detector.get());
    return encoding ? QByteArray(encoding) : QByteArray();
}

namespace {

QString strictUtf8(QByteArrayView bytes)
{
    // wxConvAuto after a UTF-8 BOM, or on text IsUTF8withoutBOM accepted: a
    // later U+FEFF stays text.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    return decoder.hasError() ? QString() : text;
}

// wxCSConv's last resort when no converter exists for the name: Latin-1 bytes
// as characters (wxCSConv::ToWChar without m_convReal).
QString latin1Direct(QByteArrayView bytes)
{
    return QString::fromLatin1(bytes);
}

#ifdef _WIN32

// wxFFile "r" is a text-mode read with the Microsoft CRT: CR LF becomes LF and
// Ctrl+Z ends the text. It works on bytes, before any conversion.
QByteArray textModeRead(const QByteArray &raw)
{
    QByteArray out;
    out.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\x1A')
            break;
        if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
            continue;
        out += c;
    }
    return out;
}

// wxMBConv_win32: MultiByteToWideChar with MB_ERR_INVALID_CHARS, failing on
// any invalid sequence (and on code pages that refuse the flag).
QString multiByteToWide(UINT codePage, QByteArrayView bytes)
{
    if (bytes.isEmpty())
        return {};
    const int size = static_cast<int>(bytes.size());
    const int length = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
    if (length <= 0)
        return {};
    QString text(length, Qt::Uninitialized);
    if (::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size,
                              reinterpret_cast<wchar_t *>(text.data()), length) != length)
        return {};
    return text;
}

// wxMBConvUTF16LE / wxMBConvUTF32LE: no BOM handling, so a BOM stays as
// U+FEFF; an odd length or a lone surrogate fails.
QString utfLittleEndian(QStringDecoder::Encoding encoding, QByteArrayView bytes)
{
    QStringDecoder decoder(encoding, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    return decoder.hasError() ? QString() : text;
}

// wxCSConv(name) on Windows: wxFontMapperBase::CharsetToEncoding(name) and
// wxEncodingToCodepage, for the names uchardet 0.0.8 gives. Names wx does not
// map (UHC, GB18030, IBM852, IBM866, MAC-CYRILLIC, TIS-620, VISCII,
// ISO-8859-16, ...) get no converter and are read as Latin-1.
QString decodeNamed(const QByteArray &name, QByteArrayView bytes)
{
    if (name == "UTF-16")
        return utfLittleEndian(QStringDecoder::Utf16LE, bytes);
    if (name == "UTF-32")
        return utfLittleEndian(QStringDecoder::Utf32LE, bytes);
    struct Mapping {
        const char *name;
        UINT codePage;
    };
    static constexpr Mapping mappings[] = {
        {"WINDOWS-1250", 1250}, {"WINDOWS-1251", 1251}, {"WINDOWS-1252", 1252}, {"WINDOWS-1253", 1253},
        {"WINDOWS-1255", 1255}, {"WINDOWS-1256", 1256}, {"WINDOWS-1257", 1257}, {"WINDOWS-1258", 1258},
        {"ISO-8859-2", 28592},  {"ISO-8859-3", 28593},  {"ISO-8859-4", 28594},  {"ISO-8859-5", 28595},
        {"ISO-8859-6", 28596},  {"ISO-8859-7", 28597},  {"ISO-8859-8", 28598},  {"ISO-8859-8-I", 28598},
        {"ISO-8859-9", 28599},  {"ISO-8859-10", 28600}, {"ISO-8859-11", 874},   {"ISO-8859-13", 28603},
        {"ISO-8859-15", 28605}, {"KOI8-R", 20866},      {"SHIFT_JIS", 932},     {"GB2312", 936},
        {"CP949", 949},         {"EUC-KR", 949},        {"BIG5", 950},          {"EUC-JP", 20932},
        {"ISO-2022-JP", 50222},
    };
    for (const auto &mapping : mappings)
        if (name == mapping.name)
            return ::IsValidCodePage(mapping.codePage) ? multiByteToWide(mapping.codePage, bytes) : latin1Direct(bytes);
    return latin1Direct(bytes);
}

// wxConvLocal: the ANSI code page.
QString decodeLocal(QByteArrayView bytes)
{
    return multiByteToWide(CP_ACP, bytes);
}

#else

// wxFFile "r" on Linux reads the bytes as they are.
QByteArray textModeRead(const QByteArray &raw)
{
    return raw;
}

// wxCSConv(name) on Linux: iconv with the name ("UTF-16" takes its byte
// order from the BOM and drops it); no converter: Latin-1. An invalid or
// incomplete sequence fails the whole conversion.
QString decodeNamed(const QByteArray &name, QByteArrayView bytes)
{
    const iconv_t cd = ::iconv_open("UTF-16LE", name.constData());
    if (cd == reinterpret_cast<iconv_t>(-1))
        return latin1Direct(bytes);
    QByteArray out;
    out.resize(bytes.size() * 4 + 16);
    char *in = const_cast<char *>(bytes.data());
    size_t inLeft = static_cast<size_t>(bytes.size());
    char *dst = out.data();
    size_t outLeft = static_cast<size_t>(out.size());
    bool ok = true;
    while (inLeft > 0) {
        if (::iconv(cd, &in, &inLeft, &dst, &outLeft) != static_cast<size_t>(-1))
            continue;
        if (errno == E2BIG) {
            const qsizetype used = dst - out.data();
            out.resize(out.size() * 2);
            dst = out.data() + used;
            outLeft = static_cast<size_t>(out.size() - used);
            continue;
        }
        ok = false;
        break;
    }
    ::iconv_close(cd);
    if (!ok)
        return {};
    const qsizetype used = dst - out.data();
    return QString(reinterpret_cast<const QChar *>(out.constData()), used / 2);
}

// wxConvLocal: the C library's multibyte conversion in the user's locale.
QString decodeLocal(QByteArrayView bytes)
{
    QStringDecoder decoder(QStringDecoder::System, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString text = decoder.decode(bytes);
    return decoder.hasError() ? QString() : text;
}

#endif

} // namespace

QString legacyFileOpenText(const QByteArray &raw)
{
    // The charset is decided on the binary read, the text comes from wxFFile.
    const bool bom = raw.startsWith("\xEF\xBB\xBF");
    const bool utf8 = bom || legacyIsUtf8WithoutBom(raw);
    const QByteArray charset = utf8 ? QByteArray() : legacyDetectCharset(raw).toUpper();
    const QByteArray bytes = textModeRead(raw);
    if (utf8)
        return strictUtf8(QByteArrayView(bytes).sliced(bytes.startsWith("\xEF\xBB\xBF") ? 3 : 0));
    if (!charset.isEmpty())
        return decodeNamed(charset, bytes);
    return decodeLocal(bytes);
}

std::optional<QString> legacyFileOpen(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QString text = legacyFileOpenText(file.readAll());
    if (text.isEmpty())
        return std::nullopt;
    return text;
}

} // namespace hikari::backends
