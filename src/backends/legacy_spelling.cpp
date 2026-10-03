#include "hikari/backends/legacy_spelling.h"

#include <boost/locale/boundary/index.hpp>
#include <boost/locale/boundary/segment.hpp>
#include <boost/locale/boundary/types.hpp>
#include <boost/locale/generator.hpp>
#include <boost/locale/localization_backend.hpp>
#include <hunspell.hxx>
#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>

#include <QChar>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>

namespace hikari::backends {

namespace {

namespace fs = std::filesystem;

// Hunspell opens its files with narrow paths; on Windows (MSVC) a path with
// the long-path prefix is read as UTF-8, so any name opens.
std::string hunspellPath(const fs::path &file)
{
#ifdef _WIN32
    std::error_code ec;
    fs::path full = fs::absolute(file, ec);
    if (ec)
        full = file;
    full.make_preferred();
    const std::u8string utf8 = full.u8string();
    return "\\\\?\\" + std::string(utf8.begin(), utf8.end());
#else
    return file.string();
#endif
}

struct ConverterClose {
    void operator()(UConverter *c) const { ucnv_close(c); }
};
using Converter = std::unique_ptr<UConverter, ConverterClose>;

// Hunspell's SET names (ISO8859-n, microsoft-cp1251, TIS620-2533,
// ISCII-DEVANAGARI, KOI8-R/U, UTF-8) in ICU's spelling.
std::string icuEncodingName(std::string name)
{
    std::string upper = name;
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper.starts_with("ISO8859-"))
        return "ISO-8859-" + upper.substr(8);
    if (upper == "MICROSOFT-CP1251")
        return "windows-1251";
    if (upper == "TIS620-2533")
        return "TIS-620";
    if (upper == "ISCII-DEVANAGARI")
        return "ISCII,version=0";
    return name;
}

Converter openConverter(const std::string &encoding)
{
    for (const std::string &name : {icuEncodingName(encoding), std::string("ISO-8859-1")}) {
        UErrorCode status = U_ZERO_ERROR;
        Converter converter(ucnv_open(name.c_str(), &status));
        if (U_FAILURE(status) || !converter)
            continue;
        // A character the encoding cannot hold fails the conversion (wxCSConv
        // returns no buffer), it is not replaced.
        status = U_ZERO_ERROR;
        ucnv_setFromUCallBack(converter.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
        return converter;
    }
    return {};
}

class HunspellSpelling final : public application::SpellingBackend {
public:
    HunspellSpelling(const fs::path &aff, const fs::path &dic)
        : m_hunspell(hunspellPath(aff).c_str(), hunspellPath(dic).c_str()),
          m_converter(openConverter(m_hunspell.get_dict_encoding()))
    {
    }

    bool spell(std::u16string_view word) override
    {
        // CheckWord: an unconvertible or empty word is misspelled.
        const auto bytes = encode(word);
        return bytes && !bytes->empty() && m_hunspell.spell(*bytes);
    }
    std::vector<std::u16string> suggest(std::u16string_view word) override
    {
        std::vector<std::u16string> out;
        const auto bytes = encode(word);
        if (!bytes)
            return out;
        for (const std::string &s : m_hunspell.suggest(*bytes))
            out.push_back(decode(s));
        return out;
    }
    void add(std::u16string_view word) override
    {
        // Legacy passes a failed conversion's null buffer to std::string
        // (undefined, a crash); such a word is not added (R3-hang-crash-loss).
        if (const auto bytes = encode(word); bytes && !bytes->empty())
            m_hunspell.add(*bytes);
    }
    bool remove(std::u16string_view word) override
    {
        const auto bytes = encode(word);
        return bytes && !bytes->empty() && m_hunspell.remove(*bytes) == 0;
    }

private:
    std::optional<std::string> encode(std::u16string_view word)
    {
        if (!m_converter)
            return std::nullopt;
        UErrorCode status = U_ZERO_ERROR;
        ucnv_resetFromUnicode(m_converter.get());
        const auto *source = reinterpret_cast<const UChar *>(word.data());
        const int32_t length = static_cast<int32_t>(word.size());
        const int32_t needed = ucnv_fromUChars(m_converter.get(), nullptr, 0, source, length, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
            return std::nullopt;
        std::string out(static_cast<std::size_t>(needed), '\0');
        status = U_ZERO_ERROR;
        ucnv_resetFromUnicode(m_converter.get());
        ucnv_fromUChars(m_converter.get(), out.data(), needed, source, length, &status);
        if (U_FAILURE(status) && status != U_STRING_NOT_TERMINATED_WARNING)
            return std::nullopt;
        return out;
    }
    std::u16string decode(const std::string &bytes)
    {
        if (!m_converter)
            return {};
        UErrorCode status = U_ZERO_ERROR;
        ucnv_resetToUnicode(m_converter.get());
        const int32_t length = static_cast<int32_t>(bytes.size());
        const int32_t needed = ucnv_toUChars(m_converter.get(), nullptr, 0, bytes.data(), length, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
            return {};
        std::u16string out(static_cast<std::size_t>(needed), u'\0');
        status = U_ZERO_ERROR;
        ucnv_resetToUnicode(m_converter.get());
        ucnv_toUChars(m_converter.get(), reinterpret_cast<UChar *>(out.data()), needed, bytes.data(), length, &status);
        return U_FAILURE(status) && status != U_STRING_NOT_TERMINATED_WARNING ? std::u16string() : out;
    }

    Hunspell m_hunspell;
    Converter m_converter;
};

// HikariSubFrame: boost::locale's generator over the system locale, made
// global; legacy's segment indexes use that global locale. ICU is the
// backend that implements boundary analysis.
const std::locale &segmentationLocale()
{
    static const std::locale locale = [] {
        boost::locale::localization_backend_manager manager = boost::locale::localization_backend_manager::global();
        manager.select("icu");
        boost::locale::generator generator(manager);
        for (const char *name : {"", "C.UTF-8", "en_US.UTF-8"}) {
            try {
                return generator(name);
            } catch (...) {
            }
        }
        return std::locale::classic();
    }();
    return locale;
}

std::vector<core::legacy::WordSegment> segmentWords(std::u16string_view text)
{
    namespace boundary = boost::locale::boundary;
    std::vector<core::legacy::WordSegment> out;
    if (text.empty())
        return out;
    // Legacy segments std::wstring: UTF-16 on Windows. Where wchar_t is
    // UTF-32 the text is converted and the segments mapped back to UTF-16
    // offsets (unit[k]: the UTF-16 offset of wide character k).
    std::wstring wide;
    std::vector<std::size_t> unit;
    wide.reserve(text.size());
    unit.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size(); ++i) {
        unit.push_back(i);
        const char16_t c = text[i];
        if constexpr (sizeof(wchar_t) == 2) {
            wide += static_cast<wchar_t>(c);
        } else if (QChar::isHighSurrogate(c) && i + 1 < text.size() && QChar::isLowSurrogate(text[i + 1])) {
            wide += static_cast<wchar_t>(QChar::surrogateToUcs4(c, text[i + 1]));
            ++i;
        } else {
            // A lone surrogate is not a code point; ICU sees U+FFFD.
            wide += static_cast<wchar_t>(QChar::isSurrogate(c) ? 0xFFFD : c);
        }
    }
    unit.push_back(text.size());
    try {
        const boundary::wssegment_index index(boundary::word, wide.begin(), wide.end(), segmentationLocale());
        for (auto p = index.begin(), e = index.end(); p != e; ++p) {
            const auto from = static_cast<std::size_t>(p->begin() - wide.begin());
            const auto to = static_cast<std::size_t>(p->end() - wide.begin());
            if (to <= from)
                continue;
            out.push_back({unit[from], unit[to] - unit[from], (p->rule() & boundary::word_letters) != 0,
                           (p->rule() & boundary::word_number) != 0});
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

} // namespace

application::SpellingBackendLoader hunspellSpellingLoader()
{
    return [](const fs::path &aff, const fs::path &dic) -> std::unique_ptr<application::SpellingBackend> {
        return std::make_unique<HunspellSpelling>(aff, dic);
    };
}

application::SpellingText legacySpellingText()
{
    application::SpellingText text;
    text.segment = segmentWords;
    // iswupper / wxToupper / wxTolower: one UTF-16 unit at a time.
    text.cases.isUpper = [](char16_t c) { return QChar(c).isUpper(); };
    text.cases.toUpper = [](char16_t c) { return QChar(c).toUpper().unicode(); };
    text.cases.toLower = [](char16_t c) { return QChar(c).toLower().unicode(); };
    return text;
}

} // namespace hikari::backends
