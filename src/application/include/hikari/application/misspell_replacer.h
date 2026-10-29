#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/legacy_regex.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

// Option bits as Rules.txt stores them.
enum ReplacerOption : int {
    kReplacerMatchCase = 1,
    kReplacerLowerCase = 2,
    kReplacerUpperCase = 4,
    kReplacerUnchangedCase = 8,
    kReplacerOnlyTags = 16,
    kReplacerOnlyText = 32,
};

struct ReplacerRule {
    std::u8string description;
    std::u8string find;
    std::u8string replace;
    int options = 0;
    bool checked = false;
    bool operator==(const ReplacerRule &) const = default;
};

// Rules.txt as HikariSub legacy writes it; empty text gives the shipped rules. A line that does not read
// whole is kept as far as it got and listed in `invalid`. Read on Linux, each line keeps its "\r" (a blank
// CRLF line is a rule) except the checkbox line.
enum class RulesReadBy { Windows, Linux };
#ifdef _WIN32
inline constexpr RulesReadBy kRulesReadBy = RulesReadBy::Windows;
#else
inline constexpr RulesReadBy kRulesReadBy = RulesReadBy::Linux;
#endif
struct ReplacerRules {
    std::vector<ReplacerRule> rules;
    std::vector<std::u8string> invalid;
};
ReplacerRules readReplacerRules(std::u8string_view text, RulesReadBy build = kRulesReadBy);
// English texts.
std::u8string defaultReplacerRules();
// CRLF lines; the caller writes the UTF-8 BOM.
std::u8string writeReplacerRules(const std::vector<ReplacerRule> &rules);

// Unset functions fall back to ASCII.
struct ReplacerCase {
    std::function<bool(char16_t)> isUpper;
    std::function<char16_t(char16_t)> toLower;
    std::function<char16_t(char16_t)> toUpper;
};

// The replacement takes the found text's case, as `options` say.
std::u16string moveCase(std::u16string_view original, std::u16string result, int options,
                        const ReplacerCase &cases = {});
// For the only-tags/only-text options; inside tags means a '{' before `position` with no '}' between.
bool keepFinding(std::u16string_view text, std::size_t position, int options);

// Advanced syntax; case-insensitive without kReplacerMatchCase.
core::LegacyRegex compileReplacerRule(const ReplacerRule &rule);
// {position, length}; ^ matches at the start of `text`.
std::optional<std::pair<std::size_t, std::size_t>> replacerSearch(const core::LegacyRegex &re, std::u16string_view text);
// The replaced text and how many matches were replaced.
std::pair<std::u16string, int> replacerReplace(const core::LegacyRegex &re, std::u16string_view text,
                                               std::u16string_view replacement);

// The converted expression and PCRE2's message.
struct ReplacerRegexError {
    std::u16string expression;
    std::u16string message;
    bool operator==(const ReplacerRegexError &) const = default;
};
// In rule order: the checked rules when `checkedOnly` (none when no rule is checked), else every rule.
std::vector<ReplacerRegexError> replacerRegexErrors(const std::vector<ReplacerRule> &rules, bool checkedOnly);

struct ReplacerScope {
    enum class Lines { All, Selected, FromSelection, Styles };
    Lines lines = Lines::All;
    std::u8string styles; // comma-separated names
};

// One row of "Search results".
struct ReplacerFind {
    std::size_t row = 0;        // Document row
    int lineNumber = 0;         // 1-based among shown Lines
    std::u16string text;        // the searched text when found (the Translation when it has one)
    std::size_t position = 0;   // UTF-16 units in text
    std::size_t length = 0;     // at least 1
    int rule = 0;
    bool operator==(const ReplacerFind &) const = default;
};

// PCRE2's text for each match that failed other than "no match" (match or heap limit); each counts as no match.
using ReplacerMatchErrors = std::vector<std::u16string>;

// The checked rules over the shown Lines in scope, rule by rule within each Line; FromSelection takes
// every shown Line from the first selected one.
std::vector<ReplacerFind> findErrors(const EditSession &session, const std::vector<ReplacerRule> &rules,
                                     const ReplacerScope &scope, ReplacerMatchErrors *matchErrors = nullptr);

// One undo step; each rule searches the text the previous rule left. False when no Line changed (no step).
std::expected<bool, CommandRefusal> replaceErrors(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                  const ReplacerScope &scope, const ReplacerCase &cases = {},
                                                  ReplacerMatchErrors *matchErrors = nullptr);

struct ReplacerProblem {
    enum class Kind {
        Edited,        // the Line changed since the find
        NotReplaced,
        MatchError,    // comes before its NotReplaced
    };
    Kind kind = Kind::Edited;
    int lineNumber = 0;
    std::u16string found;  // NotReplaced: the text at the find; MatchError: PCRE2's text
    std::u8string replace; // NotReplaced only
    std::u8string find;    // NotReplaced only
    bool operator==(const ReplacerProblem &) const = default;
};
struct ReplacedFinds {
    bool changed = false; // an undo step was recorded
    std::vector<ReplacerProblem> problems;
};

// `finds` in list order; each Line is replaced from its last find back, only while it still has the found text.
std::expected<ReplacedFinds, CommandRefusal> replaceFinds(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                          const std::vector<ReplacerFind> &finds,
                                                          const ReplacerCase &cases = {});

}
