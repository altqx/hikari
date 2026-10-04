#pragma once

// F4: fix minor errors (legacy GLOBAL_MISSPELLS_REPLACER "Fix minor errors
// (experimental)": the MisspellReplacer "Multireplacer" dialog and its
// FindResultDialog "Search results" at 20d647c4). Ordered rules (a regular
// expression, a replacement and case/tag options) are found in, or replaced
// in, the Lines of one Document; each replacing run is one "Fixing minor
// errors" step.
//
// Legacy compiled the rules with wxRegEx(wxRE_ADVANCED [| wxRE_ICASE without
// Match case]), which wxWidgets 3.3.3 runs on PCRE2; here they compile with
// core::LegacyRegex and the same flags (R1-pcre2).

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

// MisspellReplacer.h rule options (GetRuleOptions bits).
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
    std::u8string find;    // "Search phrase (regular expresions)"
    std::u8string replace; // "Replace phrase"
    int options = 0;
    bool checked = false;  // the rules list's checkbox
    bool operator==(const ReplacerRule &) const = default;
};

// Rules.txt as FillRulesList reads it, from the text OpenWrite::FileOpen
// gives (backends::legacyFileOpen: on Windows CRLF is already LF, on Linux
// each line keeps its "\r", so the last checkbox reads "1\r", not "1", and a
// blank CRLF line is a rule). Empty text gives the shipped rules. A line
// Rule() cannot read whole is kept as far as it got and listed in `invalid`
// (legacy logs "Rule \"%s\" is invalid.").
struct ReplacerRules {
    std::vector<ReplacerRule> rules;
    std::vector<std::u8string> invalid;
};
ReplacerRules readReplacerRules(std::u8string_view text);
// FillWithDefaultRules (the English texts).
std::u8string defaultReplacerRules();
// SaveRules: the header, the checkbox line and one line per rule, CRLF (the
// caller writes a UTF-8 BOM before it, as OpenWrite::FileWrite does).
std::u8string writeReplacerRules(const std::vector<ReplacerRule> &rules);

// Per-character case functions (iswupper, towlower/towupper as wxString's
// MakeLower/MakeUpper and wxToupper apply them); unset: ASCII only.
struct ReplacerCase {
    std::function<bool(char16_t)> isUpper;
    std::function<char16_t(char16_t)> toLower;
    std::function<char16_t(char16_t)> toUpper;
};

// MoveCase: the replacement takes the found text's case.
std::u16string moveCase(std::u16string_view original, std::u16string result, int options,
                        const ReplacerCase &cases = {});
// KeepFinding: true keeps a find under "Replace only in tags/text" (a '{'
// before `position` with no '}' between them means inside tags).
bool keepFinding(std::u16string_view text, std::size_t position, int options);

// The rule's find phrase as MisspellReplacer compiles it: wxRegEx(find,
// wxRE_ADVANCED, plus wxRE_ICASE without "Match case") (R1-pcre2).
core::LegacyRegex compileReplacerRule(const ReplacerRule &rule);
// wxRegEx::Matches on `text` alone (its start is the start of a string),
// then GetMatch: {position, length}.
std::optional<std::pair<std::size_t, std::size_t>> replacerSearch(const core::LegacyRegex &re, std::u16string_view text);
// wxRegEx::Replace(&text, replacement) on a find: the text and how many
// matches were replaced.
std::pair<std::u16string, int> replacerReplace(const core::LegacyRegex &re, std::u16string_view text,
                                               std::u16string_view replacement);

// What wxRegEx::Compile logs for a find phrase that does not compile:
// "Invalid regular expression '%s': %s" with the converted expression and
// PCRE2's message.
struct ReplacerRegexError {
    std::u16string expression;
    std::u16string message;
    bool operator==(const ReplacerRegexError &) const = default;
};
// The errors in the order legacy compiles the rules: the checked ones when
// `checkedOnly` (Find and Replace all, which compile nothing when no rule is
// checked), every rule otherwise (the results' Replace).
std::vector<ReplacerRegexError> replacerRegexErrors(const std::vector<ReplacerRule> &rules, bool checkedOnly);

// The "Which lines" choice and the styles text beside it.
struct ReplacerScope {
    enum class Lines { All, Selected, FromSelection, Styles };
    Lines lines = Lines::All;
    std::u8string styles; // ChoosenStyles: names between commas, matched as ",name,"
};

// One row of "Search results" (legacy ReplacerSeekResults).
struct ReplacerFind {
    std::size_t row = 0;        // keyLine: the Line's row in the Document
    int lineNumber = 0;         // idLine: "Line %i: " (shown Lines counted from 1)
    std::u16string text;        // the searched text when found (the Translation when it has one)
    std::size_t position = 0;   // UTF-16 units in text
    std::size_t length = 0;     // at least 1
    int rule = 0;               // numOfRule
    bool operator==(const ReplacerFind &) const = default;
};

// SeekOnTab: the checked rules over the shown Lines in scope, rule by rule
// within each Line, in Document order. "From the selected line" takes every
// shown Line from the first selected one (S57-from-selected).
std::vector<ReplacerFind> findErrors(const EditSession &session, const std::vector<ReplacerRule> &rules,
                                     const ReplacerScope &scope);

// ReplaceOnTab: the checked rules replace in the shown Lines in scope, as one
// "Fixing minor errors" step; each rule searches the text the previous rule
// left (S57-rule-offsets). False when no Line was changed (no step).
std::expected<bool, CommandRefusal> replaceErrors(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                  const ReplacerScope &scope, const ReplacerCase &cases = {});

// What ReplaceBlock logs.
struct ReplacerProblem {
    enum class Kind {
        Edited,        // "Line %i cannot be replaced,\ncause it was edited."
        NotReplaced,   // "Cannot replace \"%s\" to \"%s\", with rule \"%s\" in line %i."
    };
    Kind kind = Kind::Edited;
    int lineNumber = 0;
    std::u16string found;  // NotReplaced: the text at the find
    std::u8string replace; // NotReplaced: the rule's replace phrase
    std::u8string find;    // NotReplaced: the rule's find phrase
    bool operator==(const ReplacerProblem &) const = default;
};
struct ReplacedFinds {
    bool changed = false; // a "Fixing minor errors" step was recorded
    std::vector<ReplacerProblem> problems;
};

// ReplaceChecked for one Document: `finds` are its checked rows in list
// order; each Line's run is replaced from the last position back, when the
// Line still has the text it was found in.
std::expected<ReplacedFinds, CommandRefusal> replaceFinds(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                          const std::vector<ReplacerFind> &finds,
                                                          const ReplacerCase &cases = {});

} // namespace hikari::application
