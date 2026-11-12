#include "hikari/backends/icu_text.h"

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/ustring.h>

#include <memory>
#include <string>
#include <vector>

namespace hikari::backends {

namespace {

using CaseMap = int32_t (*)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *);

std::u16string mapCase(std::u16string_view text, CaseMap map)
{
    if (text.empty())
        return {};
    const auto *source = reinterpret_cast<const UChar *>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    std::u16string out(text.size() + 8, u'\0');
    UErrorCode status = U_ZERO_ERROR;
    auto written = map(reinterpret_cast<UChar *>(out.data()), static_cast<int32_t>(out.size()), source, length, "", &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) {
        out.assign(static_cast<std::size_t>(written), u'\0');
        status = U_ZERO_ERROR;
        written = map(reinterpret_cast<UChar *>(out.data()), written, source, length, "", &status);
    }
    if (U_FAILURE(status))
        return std::u16string(text);
    out.resize(static_cast<std::size_t>(written));
    return out;
}

std::vector<std::size_t> lineBreaks(std::u16string_view text)
{
    std::vector<std::size_t> out;
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> lines(icu::BreakIterator::createLineInstance(icu::Locale::getRoot(), status));
    std::unique_ptr<icu::BreakIterator> graphemes(
        icu::BreakIterator::createCharacterInstance(icu::Locale::getRoot(), status));
    if (U_FAILURE(status))
        return out;
    const icu::UnicodeString string(false, reinterpret_cast<const UChar *>(text.data()),
                                    static_cast<int32_t>(text.size()));
    lines->setText(string);
    graphemes->setText(string);
    for (auto at = lines->next(); at != icu::BreakIterator::DONE; at = lines->next())
        if (at > 0 && static_cast<std::size_t>(at) < text.size() && graphemes->isBoundary(at))
            out.push_back(static_cast<std::size_t>(at));
    return out;
}

}

application::text_commands::TextServices icuTextServices()
{
    return {
        [](std::u16string_view t) { return mapCase(t, u_strToUpper); },
        [](std::u16string_view t) { return mapCase(t, u_strToLower); },
        lineBreaks,
    };
}

}
