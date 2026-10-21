#include "hikari/core/graphemes.h"

#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include <memory>

namespace hikari::core {

namespace {

const pcre2_code *graphemePattern()
{
    static const pcre2_code *const code = [] {
        static constexpr char16_t pattern[] = u"\\X";
        int error = 0;
        PCRE2_SIZE offset = 0;
        return pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern), 2,
                             PCRE2_UTF | PCRE2_MATCH_INVALID_UTF | PCRE2_ANCHORED, &error, &offset, nullptr);
    }();
    return code;
}

struct MatchDataFree {
    void operator()(pcre2_match_data *data) const { pcre2_match_data_free(data); }
};

} // namespace

std::size_t graphemeEnd(std::u16string_view text, std::size_t at)
{
    if (at >= text.size())
        return text.size();
    const pcre2_code *code = graphemePattern();
    thread_local const std::unique_ptr<pcre2_match_data, MatchDataFree> data(
        code ? pcre2_match_data_create_from_pattern(code, nullptr) : nullptr);
    if (code && data &&
        pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), at, 0, data.get(), nullptr) > 0) {
        const PCRE2_SIZE end = pcre2_get_ovector_pointer(data.get())[1];
        if (end > at)
            return end;
    }
    return at + 1;
}

} // namespace hikari::core
