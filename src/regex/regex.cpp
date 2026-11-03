#include "hikari/regex/regex.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <cstdint>

namespace hikari::regex {

struct Pattern::Code {
    pcre2_code *code = nullptr;
    ~Code() { pcre2_code_free(code); }
};

namespace {

PCRE2_SPTR units(std::string_view s)
{
    return reinterpret_cast<PCRE2_SPTR>(s.data());
}

struct CompileContext {
    pcre2_compile_context *context = pcre2_compile_context_create(nullptr);
    ~CompileContext() { pcre2_compile_context_free(context); }
};

struct MatchData {
    pcre2_match_data *data;
    explicit MatchData(const pcre2_code *code) : data(pcre2_match_data_create_from_pattern(code, nullptr)) {}
    ~MatchData() { pcre2_match_data_free(data); }
};

}

std::string_view Match::operator[](std::size_t group) const
{
    const Span &span = m_groups.at(group);
    return span ? m_subject.substr(span->first, span->second - span->first) : std::string_view();
}

std::size_t Match::position(std::size_t group) const
{
    return m_groups.at(group).value().first;
}

std::size_t Match::end(std::size_t group) const
{
    return m_groups.at(group).value().second;
}

std::expected<Pattern, CompileError> Pattern::compile(std::string_view pattern, Syntax syntax)
{
    std::uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
    CompileContext context;
    if (syntax == Syntax::EcmaScript) {
        options |= PCRE2_ALT_BSUX | PCRE2_DOLLAR_ENDONLY | PCRE2_ALLOW_EMPTY_CLASS;
        pcre2_set_compile_extra_options(context.context, PCRE2_EXTRA_ALT_BSUX);
        pcre2_set_newline(context.context, PCRE2_NEWLINE_ANYCRLF);
    }
    int error = 0;
    PCRE2_SIZE offset = 0;
    pcre2_code *code = pcre2_compile(units(pattern), pattern.size(), options, &error, &offset, context.context);
    if (!code) {
        std::array<PCRE2_UCHAR, 256> buffer{};
        const int length = pcre2_get_error_message(error, buffer.data(), buffer.size());
        return std::unexpected(CompileError{
            std::string(reinterpret_cast<const char *>(buffer.data()), length > 0 ? std::size_t(length) : 0), offset});
    }
    auto owned = std::make_shared<Code>();
    owned->code = code;
    return Pattern(std::move(owned));
}

std::optional<Match> Pattern::run(std::string_view subject, std::size_t from, bool whole) const
{
    if (from > subject.size())
        return std::nullopt;
    MatchData data(m_code->code);
    const std::uint32_t options = whole ? PCRE2_ANCHORED | PCRE2_ENDANCHORED : 0;
    const int rc = pcre2_match(m_code->code, units(subject), subject.size(), from, options, data.data, nullptr);
    if (rc < 0)
        return std::nullopt;
    const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(data.data);
    std::vector<Match::Span> groups(pcre2_get_ovector_count(data.data));
    for (std::size_t i = 0; i < groups.size(); ++i)
        if (ovector[2 * i] != PCRE2_UNSET)
            groups[i] = std::pair(std::size_t(ovector[2 * i]), std::size_t(ovector[2 * i + 1]));
    return Match(subject, std::move(groups));
}

std::optional<Match> Pattern::match(std::string_view subject) const
{
    return run(subject, 0, true);
}

std::optional<Match> Pattern::search(std::string_view subject, std::size_t from) const
{
    return run(subject, from, false);
}

std::string Pattern::replaceFirst(std::string_view subject, std::string_view replacement) const
{
    const auto found = search(subject);
    if (!found)
        return std::string(subject);
    std::string out(subject.substr(0, found->position()));
    out += replacement;
    out += subject.substr(found->end());
    return out;
}

}
