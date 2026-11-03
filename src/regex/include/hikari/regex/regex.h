#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::regex {

// Pcre is PCRE2's own syntax; EcmaScript follows ECMA-262 with the u flag (\uXXXX, \u{…}, two-digit \x, `$` only at the end).
enum class Syntax { Pcre, EcmaScript };

struct CompileError {
    std::string message;
    std::size_t offset = 0;
};

// Groups are views into the searched subject, which must outlive the match.
class Match {
public:
    std::size_t size() const { return m_groups.size(); }
    bool matched(std::size_t group) const { return m_groups.at(group).has_value(); }
    std::string_view operator[](std::size_t group) const;
    std::size_t position(std::size_t group = 0) const;
    std::size_t end(std::size_t group = 0) const;

private:
    friend class Pattern;
    using Span = std::optional<std::pair<std::size_t, std::size_t>>;
    Match(std::string_view subject, std::vector<Span> groups) : m_subject(subject), m_groups(std::move(groups)) {}

    std::string_view m_subject;
    std::vector<Span> m_groups;
};

// Compiled once, then safe to use from several threads. Subjects and patterns are UTF-8; invalid bytes match nothing.
class Pattern {
public:
    static std::expected<Pattern, CompileError> compile(std::string_view pattern, Syntax syntax = Syntax::Pcre);

    std::optional<Match> match(std::string_view subject) const;
    std::optional<Match> search(std::string_view subject, std::size_t from = 0) const;
    // `replacement` is literal text.
    std::string replaceFirst(std::string_view subject, std::string_view replacement) const;

private:
    struct Code;
    explicit Pattern(std::shared_ptr<const Code> code) : m_code(std::move(code)) {}
    std::optional<Match> run(std::string_view subject, std::size_t from, bool whole) const;

    std::shared_ptr<const Code> m_code;
};

}
