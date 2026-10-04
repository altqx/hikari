#include "hikari/application/misspell_replacer.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <map>
#include <set>

namespace hikari::application {

namespace {

using u16sv = std::u16string_view;

constexpr std::u8string_view kRulesHeader = u8"#HikariSub rules file";

char16_t lowerOf(char16_t c, const ReplacerCase &cases)
{
    if (cases.toLower)
        return cases.toLower(c);
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

char16_t upperOf(char16_t c, const ReplacerCase &cases)
{
    if (cases.toUpper)
        return cases.toUpper(c);
    return c >= u'a' && c <= u'z' ? static_cast<char16_t>(c - u'a' + u'A') : c;
}

bool isUpperChar(char16_t c, const ReplacerCase &cases)
{
    if (cases.isUpper)
        return cases.isUpper(c);
    return c >= u'A' && c <= u'Z';
}

// wxString::Mid: a start past the end gives "", a count past the end is cut.
std::u16string mid(u16sv s, std::size_t first, std::size_t count = std::u16string::npos)
{
    if (first > s.size())
        return {};
    return std::u16string(s.substr(first, count));
}

std::vector<std::u8string_view> split(std::u8string_view text, char8_t delimiter, bool keepEmpty)
{
    std::vector<std::u8string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(delimiter, start);
        const auto token = text.substr(start, at == std::u8string_view::npos ? std::u8string_view::npos : at - start);
        if (keepEmpty || !token.empty())
            out.push_back(token);
        if (at == std::u8string_view::npos)
            break;
        start = at + 1;
    }
    return out;
}

// Rule::Rule(const wxString &): description \f find \f replace \f options
// (wxTOKEN_RET_EMPTY_ALL). A missing field fails; legacy then leaves the
// options uninitialized, here 0.
ReplacerRule parseRule(std::u8string_view line, bool &valid)
{
    const auto fields = split(line, u8'\f', true);
    ReplacerRule rule;
    rule.description = std::u8string(fields[0]);
    valid = false;
    if (fields.size() < 2)
        return rule;
    rule.find = std::u8string(fields[1]);
    if (fields.size() < 3)
        return rule;
    rule.replace = std::u8string(fields[2]);
    if (fields.size() < 4)
        return rule;
    rule.options = static_cast<int>(core::legacy::atoi(fields[3]));
    valid = true;
    return rule;
}

// The text legacy searches: the translation when the Line has one
// (Dialogue::Text.CheckTl(TextTl, TextTl != "")).
std::u16string searchedText(const core::LineRecord &line)
{
    return core::toUtf16(line.translation.empty() ? line.text : line.translation);
}

void setSearchedText(core::LineRecord &line, const std::u16string &text)
{
    (line.translation.empty() ? line.text : line.translation) = core::toUtf8(text);
}

bool inScope(const core::LineRecord &line, const ReplacerScope &scope, const Selection &selection)
{
    using L = ReplacerScope::Lines;
    switch (scope.lines) {
    case L::All: return true;
    case L::Styles:
        return (u8"," + scope.styles + u8",").find(u8"," + line.style + u8",") != std::u8string::npos;
    case L::Selected: return selection.selected.contains(line.id);
    // S57-from-selected / R3-hang-crash-loss: every Line from the first
    // selected one on (legacy started the walk there and then took no Line).
    case L::FromSelection: return true;
    }
    return false;
}

// FirstSelection: the first selected shown Line's row, 0 without one.
std::size_t startRow(const std::vector<const core::LineRecord *> &lines, const ReplacerScope &scope,
                     const Selection &selection)
{
    if (scope.lines != ReplacerScope::Lines::FromSelection)
        return 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (selection.selected.contains(lines[i]->id) && lines[i]->visibility != core::LineVisibility::Hidden)
            return i;
    return 0;
}

const core::LineRecord *lineById(const core::Document &d, core::LineId id)
{
    for (const auto *line : d.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

struct CompiledRule {
    core::LegacyRegex regex;
    int rule = 0;
};

// wx_regexec's wxLogError after a Matches or Replace that failed with an
// error (PCRE2's match or heap limit), which then counted as no match.
void noteMatchError(const core::LegacyRegex &re, ReplacerMatchErrors *errors)
{
    if (!errors)
        return;
    if (auto error = re.matchError(); !error.empty())
        errors->push_back(std::move(error));
}

// The checked rules that compile, each with its own index.
std::vector<CompiledRule> compileChecked(const std::vector<ReplacerRule> &rules)
{
    std::vector<CompiledRule> out;
    for (std::size_t i = 0; i < rules.size(); ++i)
        if (rules[i].checked)
            if (auto re = compileReplacerRule(rules[i]); re.isValid())
                out.push_back({std::move(re), static_cast<int>(i)});
    return out;
}

// ReplaceOnTab's work on one Line's text; nullopt when no find was replaced.
std::optional<std::u16string> replaceInText(const std::u16string &lineText, const std::vector<CompiledRule> &compiled,
                                            const std::vector<ReplacerRule> &rules, const ReplacerCase &cases,
                                            ReplacerMatchErrors *errors)
{
    std::u16string changedText = lineText;
    bool changed = false;
    for (const auto &[re, index] : compiled) {
        const ReplacerRule &rule = rules[static_cast<std::size_t>(index)];
        const std::u16string replacement = core::toUtf16(rule.replace);
        std::size_t textPos = 0;
        // S57-rule-offsets / R3-hang-crash-loss: each rule searches the text
        // the previous rule produced, so every position is one in the
        // changed text. Legacy started each rule on the Line's original text
        // and applied those positions to the changed one (wrong places, and
        // std::out_of_range past its end).
        std::u16string text = changedText;
        for (;;) {
            const auto match = replacerSearch(re, text);
            if (!match) {
                noteMatchError(re, errors);
                break;
            }
            const std::size_t start = match->first;
            std::size_t length = std::max<std::size_t>(match->second, 1);
            if (rule.options < 16 || keepFinding(text, start, rule.options)) {
                const std::size_t at = start + textPos;
                const std::u16string found = mid(changedText, at, length);
                auto result = replacerReplace(re, found, replacement).first;
                noteMatchError(re, errors);
                std::u16string replaced = moveCase(found, std::move(result), rule.options, cases);
                changedText.replace(std::min(at, changedText.size()), length, replaced);
                length = replaced.size();
                changed = true;
            }
            // R3-hang-crash-loss: a match at the end of the text is the last
            // (legacy found the empty match there again forever).
            if (start >= text.size())
                break;
            textPos += start + length;
            text = mid(changedText, textPos);
        }
    }
    if (!changed)
        return std::nullopt;
    return changedText;
}

} // namespace

// ---- Rules.txt

std::u8string defaultReplacerRules()
{
    return u8"#HikariSub rules file\n0|0|0|0|0|0|0|0|0|0|0|0\n"
           u8"Remove space before comma or dot\f ([,.!?%])\f\\1\f0\n"
           u8"Remove doubled spaces\f(  +)\f \f0\n"
           u8"Replace more then three dots to suspension point\f\\.{4,}\f...\f0\n"
           u8"Replace two dots to suspension point\f([^.])\\.\\.([^.])\f\\1...\\2\f0\n"
           u8"Replace missing spaces after dot or comma\f([^.])([,.!?%])([^ ,.!?%\\\"\\\\0-9-])\f\\1\\2 \\3\f0\n"
           u8"Removing japanese suffixes\f ?- ?(san|chan|kun|sama|nee|dono|senpai|sensei)\\M\f\f0\n"
           u8"Fixing Polish \"sie\"\f\\msie\\M\fsię\f0\n"
           u8"Fixing Polish \"nie mozliwe\"\f\\mnie możliwe\\M\fniemożliwe\f0\n"
           u8"Fixing Polish \"nie wazne\"\f\\mnie ważne\\M\fnieważne\f0\n"
           u8"Fixing Polish \"w ogole\"\f\\mw ?og[uo]le\\M\fw ogóle\f0\n"
           u8"Fixing Polish \"w ogole\"\f\\mwogóle\\M\fw ogóle\f0\n"
           u8"Fixing Polish \"bede\"\f\\mbed[eę]\\M\fbędę\f0\n"
           u8"Fixing Polish \"bede\"\f\\mbęde\\M\fbędę\f0";
}

ReplacerRules readReplacerRules(std::u8string_view fileText)
{
    std::u8string text(fileText);
    if (text.empty())
        text = defaultReplacerRules();

    // wxTOKEN_STRTOK: empty lines are skipped. The checkbox line follows the
    // header, or is the first line when the header is missing.
    const auto lines = split(text, u8'\n', false);
    std::size_t next = 0;
    std::u8string_view header = next < lines.size() ? lines[next++] : std::u8string_view();
    if (header.starts_with(kRulesHeader))
        header = next < lines.size() ? lines[next++] : std::u8string_view();
    const auto onOff = split(header, u8'|', false);

    ReplacerRules out;
    for (std::size_t i = 0; next < lines.size(); ++next, ++i) {
        bool valid = true;
        ReplacerRule rule = parseRule(lines[next], valid);
        if (!valid)
            out.invalid.emplace_back(lines[next]);
        rule.checked = i < onOff.size() && onOff[i] == u8"1";
        out.rules.push_back(std::move(rule));
    }
    return out;
}

std::u8string writeReplacerRules(const std::vector<ReplacerRule> &rules)
{
    std::u8string text = std::u8string(kRulesHeader) + u8"\r\n";
    for (const auto &rule : rules)
        text += rule.checked ? u8"1|" : u8"0|";
    if (text.ends_with(u8'|')) {
        text.pop_back();
        text += u8"\r\n";
    }
    for (const auto &rule : rules) {
        const std::string options = std::to_string(rule.options);
        text += rule.description + u8"\f" + rule.find + u8"\f" + rule.replace + u8"\f" +
                std::u8string(options.begin(), options.end()) + u8"\r\n";
    }
    return text;
}

// ---- case and tags

std::u16string moveCase(std::u16string_view original, std::u16string result, int options, const ReplacerCase &cases)
{
    if (options & kReplacerUnchangedCase || original.empty() || result.empty())
        return result;
    if (options & kReplacerLowerCase) {
        for (auto &c : result)
            c = lowerOf(c, cases);
        return result;
    }
    if (options & kReplacerUpperCase) {
        for (auto &c : result)
            c = upperOf(c, cases);
        return result;
    }
    // More than one capital in the find: all capitals; one (anywhere): the
    // first character capital.
    const auto capitals = std::ranges::count_if(original, [&](char16_t c) { return isUpperChar(c, cases); });
    if (capitals > 1) {
        for (auto &c : result)
            c = upperOf(c, cases);
    } else if (capitals > 0) {
        result[0] = upperOf(result[0], cases);
    }
    return result;
}

bool keepFinding(std::u16string_view text, std::size_t position, int options)
{
    bool inTags = false;
    // From the find's own character back; the end of the text reads as '\0'.
    for (std::size_t i = position + 1; i-- > 0;) {
        const char16_t c = i < text.size() ? text[i] : u'\0';
        if (c == u'}')
            break;
        if (c == u'{') {
            inTags = true;
            break;
        }
    }
    if (options & kReplacerOnlyTags && inTags)
        return true;
    if (options & kReplacerOnlyText && !inTags)
        return true;
    return false;
}

// ---- the regular expression

core::LegacyRegex compileReplacerRule(const ReplacerRule &rule)
{
    int flags = core::LegacyRegex::Advanced;
    if (!(rule.options & kReplacerMatchCase))
        flags |= core::LegacyRegex::IgnoreCase;
    return core::LegacyRegex(core::toUtf16(rule.find), flags);
}

std::vector<ReplacerRegexError> replacerRegexErrors(const std::vector<ReplacerRule> &rules, bool checkedOnly)
{
    std::vector<ReplacerRegexError> out;
    if (checkedOnly && std::ranges::none_of(rules, &ReplacerRule::checked))
        return out; // SeekOnTab / ReplaceOnTab return before compiling anything
    for (const auto &rule : rules) {
        if (checkedOnly && !rule.checked)
            continue;
        if (const auto re = compileReplacerRule(rule); !re.isValid())
            out.push_back({re.converted(), re.errorMessage()});
    }
    return out;
}

std::optional<std::pair<std::size_t, std::size_t>> replacerSearch(const core::LegacyRegex &re, std::u16string_view text)
{
    if (!re.matches(text))
        return std::nullopt;
    return re.match(0);
}

std::pair<std::u16string, int> replacerReplace(const core::LegacyRegex &re, std::u16string_view text,
                                               std::u16string_view replacement)
{
    std::u16string out(text);
    const int count = re.replace(out, replacement);
    return {std::move(out), std::max(count, 0)};
}

// ---- find and replace in a Document

std::vector<ReplacerFind> findErrors(const EditSession &session, const std::vector<ReplacerRule> &rules,
                                     const ReplacerScope &scope, ReplacerMatchErrors *matchErrors)
{
    // SeekOnTab numbers each find with checkedRules[k], k counting only the
    // rules that compile: after an invalid checked rule the numbers shift.
    std::vector<int> checked;
    for (std::size_t i = 0; i < rules.size(); ++i)
        if (rules[i].checked)
            checked.push_back(static_cast<int>(i));
    std::vector<std::pair<core::LegacyRegex, int>> compiled; // {expression, options}
    for (const int i : checked) {
        const auto &rule = rules[static_cast<std::size_t>(i)];
        if (auto re = compileReplacerRule(rule); re.isValid())
            compiled.emplace_back(std::move(re), rule.options);
    }
    std::vector<ReplacerFind> out;
    if (checked.empty())
        return out;

    const auto lines = session.document().lines();
    const Selection &selection = session.selection();
    const std::size_t start = startRow(lines, scope, selection);
    int shown = 0; // positionId: shown Lines before the row
    for (std::size_t row = 0; row < start; ++row)
        shown += lines[row]->visibility != core::LineVisibility::Hidden;
    for (std::size_t row = start; row < lines.size(); ++row) {
        const auto &line = *lines[row];
        if (line.visibility == core::LineVisibility::Hidden)
            continue;
        if (inScope(line, scope, selection)) {
            const std::u16string lineText = searchedText(line);
            for (std::size_t k = 0; k < compiled.size(); ++k) {
                const auto &[re, options] = compiled[k];
                std::size_t textPos = 0;
                while (textPos <= lineText.size()) {
                    const auto match = replacerSearch(re, u16sv(lineText).substr(textPos));
                    if (!match) {
                        noteMatchError(re, matchErrors);
                        break;
                    }
                    const std::size_t length = std::max<std::size_t>(match->second, 1);
                    if (options < 16 || keepFinding(lineText, match->first + textPos, options))
                        out.push_back({row, shown + 1, lineText, match->first + textPos, length, checked[k]});
                    // R3-hang-crash-loss: a find at the end of the text is the
                    // last (legacy searched "" past the end forever when an
                    // expression matched it).
                    textPos += match->first + length;
                }
            }
        }
        ++shown;
    }
    return out;
}

std::expected<bool, CommandRefusal> replaceErrors(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                  const ReplacerScope &scope, const ReplacerCase &cases,
                                                  ReplacerMatchErrors *matchErrors)
{
    const auto compiled = compileChecked(rules);
    if (compiled.empty())
        return false;
    const auto lines = session.document().lines();
    const Selection &selection = session.selection();
    std::set<core::LineId> touches;
    // Legacy walked each Line once and logged its match errors as it went.
    // Each Line's errors come from the walk that decides its text: this one,
    // or for a changed Line the command's walk below.
    std::map<std::size_t, ReplacerMatchErrors> lineErrors; // by row
    std::map<core::LineId, std::size_t> rowOf;
    for (std::size_t row = startRow(lines, scope, selection); row < lines.size(); ++row) {
        const auto &line = *lines[row];
        if (line.visibility == core::LineVisibility::Hidden || !inScope(line, scope, selection))
            continue;
        ReplacerMatchErrors errors;
        if (replaceInText(searchedText(line), compiled, rules, cases, &errors)) {
            touches.insert(line.id);
            rowOf[line.id] = row;
        }
        if (!errors.empty())
            lineErrors[row] = std::move(errors);
    }
    const auto report = [&] {
        if (matchErrors)
            for (const auto &[row, errors] : lineErrors)
                matchErrors->insert(matchErrors->end(), errors.begin(), errors.end());
    };
    if (touches.empty()) {
        report();
        return false;
    }
    // The Lines are worked out again on the content the command sees (a
    // pending draft on one of them is committed first).
    const auto ran = session.run(Command{"Fixing minor errors", session.revision(), touches, [&](core::Document &d) {
                                             for (const auto id : touches) {
                                                 const auto *line = lineById(d, id);
                                                 if (!line)
                                                     return false;
                                                 ReplacerMatchErrors errors;
                                                 const auto text = replaceInText(searchedText(*line), compiled, rules, cases, &errors);
                                                 if (errors.empty())
                                                     lineErrors.erase(rowOf[id]);
                                                 else
                                                     lineErrors[rowOf[id]] = std::move(errors);
                                                 if (text && !d.editLine(id, [&](core::LineRecord &l) { setSearchedText(l, *text); }))
                                                     return false;
                                             }
                                             return true;
                                         }});
    report();
    if (!ran)
        return std::unexpected(ran.error());
    return true;
}

std::expected<ReplacedFinds, CommandRefusal> replaceFinds(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                          const std::vector<ReplacerFind> &finds,
                                                          const ReplacerCase &cases)
{
    // ReplaceChecked compiles every rule that compiles and indexes them by
    // the find's rule number: after an invalid rule the expressions shift.
    std::vector<core::LegacyRegex> compiled;
    for (const auto &rule : rules)
        if (auto re = compileReplacerRule(rule); re.isValid())
            compiled.push_back(std::move(re));

    // Consecutive finds on one row form a block; rows past the end are skipped.
    const auto lines = session.document().lines();
    std::vector<std::vector<ReplacerFind>> blocks;
    std::set<core::LineId> touches;
    for (const auto &find : finds) {
        if (find.row >= lines.size())
            continue;
        if (blocks.empty() || blocks.back().front().row != find.row)
            blocks.emplace_back();
        blocks.back().push_back(find);
        touches.insert(lines[find.row]->id);
    }
    ReplacedFinds out;
    if (blocks.empty())
        return out;
    // Each block from its last position back (legacy std::sort on few
    // elements keeps equal positions in list order).
    for (auto &block : blocks)
        std::ranges::stable_sort(block, [](const auto &a, const auto &b) { return a.position > b.position; });

    bool changed = false;
    const auto ran = session.run(Command{
        "Fixing minor errors", session.revision(), touches, [&](core::Document &d) {
            out.problems.clear();
            changed = false;
            const auto now = d.lines();
            for (const auto &block : blocks) {
                const core::LineId id = now[block.front().row]->id;
                std::u16string lineText = searchedText(*now[block.front().row]);
                if (lineText != block.front().text) {
                    out.problems.push_back({ReplacerProblem::Kind::Edited, block.front().lineNumber, {}, {}, {}});
                    continue;
                }
                bool lineChanged = false;
                for (const auto &find : block) {
                    const ReplacerRule *rule =
                        find.rule >= 0 && static_cast<std::size_t>(find.rule) < rules.size() ? &rules[static_cast<std::size_t>(find.rule)]
                                                                                              : nullptr;
                    const std::u16string found = mid(lineText, find.position, find.length);
                    // R3-hang-crash-loss: legacy read past its expressions (and
                    // rules) here when the number has none; bounds-checked, such
                    // a find is not replaced and is logged.
                    const core::LegacyRegex *re =
                        find.rule >= 0 && static_cast<std::size_t>(find.rule) < compiled.size() ? &compiled[static_cast<std::size_t>(find.rule)] : nullptr;
                    const auto replaced = re && rule ? replacerReplace(*re, found, core::toUtf16(rule->replace))
                                                     : std::pair<std::u16string, int>{found, 0};
                    if (re && rule)
                        if (auto error = re->matchError(); !error.empty())
                            out.problems.push_back({ReplacerProblem::Kind::MatchError, find.lineNumber, std::move(error), {}, {}});
                    if (replaced.second > 0) {
                        lineText.replace(std::min(find.position, lineText.size()), find.length,
                                         moveCase(found, replaced.first, rule->options, cases));
                        lineChanged = true;
                    } else {
                        out.problems.push_back({ReplacerProblem::Kind::NotReplaced, find.lineNumber, found,
                                                rule ? rule->replace : std::u8string(), rule ? rule->find : std::u8string()});
                    }
                }
                if (lineChanged) {
                    if (!d.editLine(id, [&](core::LineRecord &l) { setSearchedText(l, lineText); }))
                        return false;
                    changed = true;
                }
            }
            // Nothing replaced: no step (SetModified is not called).
            return changed;
        }});
    if (!ran && changed)
        return std::unexpected(ran.error());
    if (!ran && ran.error() != CommandRefusal::Invalid)
        return std::unexpected(ran.error());
    out.changed = ran.has_value();
    return out;
}

} // namespace hikari::application
