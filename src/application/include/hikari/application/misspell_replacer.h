#pragma once

// F4: fix minor errors (legacy GLOBAL_MISSPELLS_REPLACER "Fix minor errors
// (experimental)": the MisspellReplacer "Multireplacer" dialog and its
// FindResultDialog "Search results" at 20d647c4). Ordered rules (a regular
// expression, a replacement and case/tag options) are found in, or replaced
// in, the Lines of one Document; each replacing run is one "Fixing minor
// errors" step.
//
// Legacy compiled the rules with wxRegEx(wxRE_ADVANCED), which wxWidgets 3.3
// runs on PCRE2 (UTF, DOTALL, no UCP) after turning \m \M \y \Y into word
// boundaries. Here they run as ECMAScript (std::wregex, the approximation
// select_lines.cpp uses) after the same word-boundary translation, with
// ASCII-only \w \d \s and word boundaries as PCRE2 without UCP has them, and
// case folding through ReplacerCase (PCRE2 folds Unicode case in UTF mode).

#include "hikari/application/edit_session.h"

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

// Rules.txt as FillRulesList reads it: the text as a text-mode read gives it
// (CRLF read as LF, a UTF-8 BOM dropped); empty gives the shipped rules. A
// line Rule() cannot read whole is kept as far as it got and listed in
// `invalid` (legacy logs "Rule \"%s\" is invalid.").
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

// One rule's find phrase compiled as legacy compiled it (see the top note).
class ReplacerRegex {
public:
    // std::nullopt when the phrase is not a valid expression (legacy skips the rule).
    static std::optional<ReplacerRegex> compile(std::u16string_view pattern, bool matchCase,
                                                const ReplacerCase &cases = {});
    // wxRegEx::Matches on `text` alone (its start is the start of a string),
    // then GetMatch: {position, length}.
    std::optional<std::pair<std::size_t, std::size_t>> search(std::u16string_view text) const;
    // wxRegEx::Replace(text, replacement): every match in `text`, "^" only
    // at the first; \N back references (several digits) and & for the whole
    // match, another escaped character stands for itself. Returns the text
    // and how many matches were replaced.
    std::pair<std::u16string, int> replace(std::u16string_view text, std::u16string_view replacement) const;

    ReplacerRegex(ReplacerRegex &&) noexcept;
    ReplacerRegex &operator=(ReplacerRegex &&) noexcept;
    ~ReplacerRegex();

private:
    struct Impl;
    explicit ReplacerRegex(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

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
// within each Line, in Document order.
std::vector<ReplacerFind> findErrors(const EditSession &session, const std::vector<ReplacerRule> &rules,
                                     const ReplacerScope &scope, const ReplacerCase &cases = {});

// ReplaceOnTab: the checked rules replace in the shown Lines in scope, as one
// "Fixing minor errors" step. False when no Line was changed (no step).
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
