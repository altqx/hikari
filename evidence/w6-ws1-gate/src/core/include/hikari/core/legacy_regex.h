#pragma once

// wxRegEx as the legacy app runs it (wxWidgets 3.3.3 src/common/regex.cpp
// over PCRE2 10.47; approved R1-pcre2): the Tcl-style metasyntax and basic
// syntax converted to PCRE, PCRE2_UTF | PCRE2_ALT_BSUX with DOTALL unless
// newline mode, UTF-16 subjects and offsets (wxString on Windows), and
// Replace with \0-\9 and & back-references.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::core {

class LegacyRegex {
public:
    // wxRE_* compile flags (wx/regex.h).
    enum CompileFlag : int { Extended = 0, Advanced = 1, Basic = 2, IgnoreCase = 4, NoSub = 8, Newline = 16 };
    // wxRE_NOTBOL / wxRE_NOTEOL / wxRE_NOTEMPTY.
    enum MatchFlag : int { NotBol = 32, NotEol = 64, NotEmpty = 128 };

    LegacyRegex();
    LegacyRegex(std::u16string_view pattern, int flags);
    ~LegacyRegex();
    LegacyRegex(LegacyRegex &&) noexcept;
    LegacyRegex &operator=(LegacyRegex &&) noexcept;

    // wxRegEx::Compile. False for an invalid pattern; errorMessage() then
    // holds PCRE2's text (legacy logs "Invalid regular expression '%s': %s").
    bool compile(std::u16string_view pattern, int flags);
    bool isValid() const;
    const std::u16string &errorMessage() const { return m_error; }
    // The pattern after wxRegEx's conversions, as given to PCRE2.
    const std::u16string &converted() const { return m_converted; }

    // wxRegEx::Matches on `text` (searching anywhere in it).
    bool matches(std::u16string_view text, int flags = 0) const;
    // wxRegEx::GetMatch after matches(): start and length of the whole match
    // (0) or a group; a group that did not take part gives npos and 0.
    std::optional<std::pair<std::size_t, std::size_t>> match(std::size_t index = 0) const;
    std::size_t matchCount() const; // groups + 1 (0 with NoSub)
    // PCRE2's text for an error of the last matches() or replace() other
    // than "no match" (a match or heap limit; wx logs "Failed to find match
    // for regular expression: %s" and treats it as no match), else "".
    std::u16string matchError() const;

    // wxRegEx::Replace / ReplaceAll / ReplaceFirst: the number of
    // replacements, or -1 when the expression is invalid. After an empty
    // match the search moves on by one character (R3-hang-crash-loss;
    // legacy repeated the empty match forever).
    int replace(std::u16string &text, std::u16string_view replacement, std::size_t maxMatches = 0) const;
    int replaceAll(std::u16string &text, std::u16string_view replacement) const { return replace(text, replacement, 0); }
    int replaceFirst(std::u16string &text, std::u16string_view replacement) const { return replace(text, replacement, 1); }

    // wxRegEx::ConvertFromBasic, for tests and callers that need it.
    static std::u16string convertFromBasic(std::u16string_view bre);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::u16string m_error, m_converted;
};

} // namespace hikari::core
