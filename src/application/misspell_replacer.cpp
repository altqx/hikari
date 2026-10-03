#include "hikari/application/misspell_replacer.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <climits>
#include <locale>
#include <regex>
#include <set>

namespace hikari::application {

namespace {

using u16sv = std::u16string_view;

constexpr std::u8string_view kRulesHeader = u8"#HikariSub rules file";

std::wstring wide(u16sv s)
{
    return std::wstring(s.begin(), s.end());
}

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

// PCRE2 without UCP: \w \d \s, the POSIX classes and word boundaries see
// ASCII only; case folding follows the ReplacerCase functions.
class LegacyCtype : public std::ctype<wchar_t> {
public:
    explicit LegacyCtype(ReplacerCase cases) : std::ctype<wchar_t>(std::size_t(0)), m_cases(std::move(cases)) {}

protected:
    bool do_is(mask m, char_type c) const override
    {
        return c >= 0 && c < 128 && std::ctype<wchar_t>::do_is(m, c);
    }
    const char_type *do_is(const char_type *low, const char_type *high, mask *vec) const override
    {
        for (; low < high; ++low, ++vec) {
            *vec = mask();
            if (*low >= 0 && *low < 128)
                std::ctype<wchar_t>::do_is(low, low + 1, vec);
        }
        return high;
    }
    const char_type *do_scan_is(mask m, const char_type *low, const char_type *high) const override
    {
        while (low < high && !do_is(m, *low))
            ++low;
        return low;
    }
    const char_type *do_scan_not(mask m, const char_type *low, const char_type *high) const override
    {
        while (low < high && do_is(m, *low))
            ++low;
        return low;
    }
    char_type do_tolower(char_type c) const override
    {
        return c >= 0 && c <= 0xFFFF ? static_cast<char_type>(lowerOf(static_cast<char16_t>(c), m_cases)) : c;
    }
    const char_type *do_tolower(char_type *low, const char_type *high) const override
    {
        for (; low < high; ++low)
            *low = do_tolower(*low);
        return high;
    }
    char_type do_toupper(char_type c) const override
    {
        return c >= 0 && c <= 0xFFFF ? static_cast<char_type>(upperOf(static_cast<char16_t>(c), m_cases)) : c;
    }
    const char_type *do_toupper(char_type *low, const char_type *high) const override
    {
        for (; low < high; ++low)
            *low = do_toupper(*low);
        return high;
    }

private:
    ReplacerCase m_cases;
};

// wxRegExImpl::Compile with wxRE_ADVANCED: ConvertWordBoundaries turns \m and
// \M into PCRE's [[:<:]] and [[:>:]] (\b(?=\w), \b(?<=\w)), \y into \b and
// \Y into \B, without looking at bracket expressions. ECMAScript has no
// look-behind; at a boundary (?<=\w) is (?!\w).
std::wstring convertWordBoundaries(u16sv pattern)
{
    std::wstring out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != u'\\') {
            out += static_cast<wchar_t>(pattern[i]);
            continue;
        }
        if (++i == pattern.size()) {
            out += L'\\';
            break;
        }
        switch (pattern[i]) {
        case u'm': out += L"\\b(?=\\w)"; break;
        case u'M': out += L"\\b(?!\\w)"; break;
        case u'y': out += L"\\b"; break;
        case u'Y': out += L"\\B"; break;
        default:
            out += L'\\';
            out += static_cast<wchar_t>(pattern[i]);
        }
    }
    return out;
}

bool isAsciiDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
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
    // Legacy tests All, Styles and Selected only: "From the selected line"
    // starts the walk at the first selected Line and then takes no Line.
    switch (scope.lines) {
    case L::All: return true;
    case L::Styles:
        return (u8"," + scope.styles + u8",").find(u8"," + line.style + u8",") != std::u8string::npos;
    case L::Selected: return selection.selected.contains(line.id);
    case L::FromSelection: return false;
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
    ReplacerRegex regex;
    int rule = 0;
};

// The checked rules that compile, each with its own index.
std::vector<CompiledRule> compileChecked(const std::vector<ReplacerRule> &rules, const ReplacerCase &cases)
{
    std::vector<CompiledRule> out;
    for (std::size_t i = 0; i < rules.size(); ++i)
        if (rules[i].checked)
            if (auto re = ReplacerRegex::compile(core::toUtf16(rules[i].find), rules[i].options & kReplacerMatchCase,
                                                 cases))
                out.push_back({std::move(*re), static_cast<int>(i)});
    return out;
}

// ReplaceOnTab's work on one Line's text; nullopt when no find was replaced.
std::optional<std::u16string> replaceInText(const std::u16string &lineText, const std::vector<CompiledRule> &compiled,
                                            const std::vector<ReplacerRule> &rules, const ReplacerCase &cases)
{
    std::u16string changedText = lineText;
    bool changed = false;
    for (const auto &[re, index] : compiled) {
        const ReplacerRule &rule = rules[static_cast<std::size_t>(index)];
        const std::u16string replacement = core::toUtf16(rule.replace);
        std::size_t textPos = 0;
        // Each rule's first search runs on the Line's original text, later
        // ones on the changed text from the last replacement on (legacy).
        std::u16string text = lineText;
        while (const auto match = re.search(text)) {
            const std::size_t start = match->first;
            std::size_t length = std::max<std::size_t>(match->second, 1);
            if (rule.options < 16 || keepFinding(text, start, rule.options)) {
                const std::size_t at = start + textPos;
                const std::u16string found = mid(changedText, at, length);
                std::u16string replaced = moveCase(found, re.replace(found, replacement).first, rule.options, cases);
                // Legacy's replace throws past the end; here it appends.
                changedText.replace(std::min(at, changedText.size()), length, replaced);
                length = replaced.size();
                changed = true;
            }
            // An empty match at the end repeats forever in legacy.
            if (match->second == 0 && start >= text.size())
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
    // A text-mode read: CRLF becomes LF; wxConvAuto drops the BOM.
    std::u8string text;
    if (fileText.starts_with(u8"\xEF\xBB\xBF"))
        fileText.remove_prefix(3);
    for (std::size_t i = 0; i < fileText.size(); ++i)
        if (!(fileText[i] == u8'\r' && i + 1 < fileText.size() && fileText[i + 1] == u8'\n'))
            text += fileText[i];
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

struct ReplacerRegex::Impl {
    std::wregex re;
};

ReplacerRegex::ReplacerRegex(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
ReplacerRegex::ReplacerRegex(ReplacerRegex &&) noexcept = default;
ReplacerRegex &ReplacerRegex::operator=(ReplacerRegex &&) noexcept = default;
ReplacerRegex::~ReplacerRegex() = default;

std::optional<ReplacerRegex> ReplacerRegex::compile(std::u16string_view pattern, bool matchCase,
                                                    const ReplacerCase &cases)
{
    auto impl = std::make_unique<Impl>();
    auto flags = std::regex_constants::ECMAScript;
    if (!matchCase)
        flags |= std::regex_constants::icase;
    try {
        impl->re.imbue(std::locale(std::locale::classic(), new LegacyCtype(cases)));
        impl->re.assign(convertWordBoundaries(pattern), flags);
    } catch (const std::regex_error &) {
        return std::nullopt;
    }
    return ReplacerRegex(std::move(impl));
}

std::optional<std::pair<std::size_t, std::size_t>> ReplacerRegex::search(std::u16string_view text) const
{
    const std::wstring w = wide(text);
    std::wcmatch m;
    try {
        if (!std::regex_search(w.data(), w.data() + w.size(), m, m_impl->re))
            return std::nullopt;
    } catch (const std::regex_error &) {
        return std::nullopt; // legacy: "Failed to find match", no match
    }
    return std::pair(static_cast<std::size_t>(m.position(0)), static_cast<std::size_t>(m.length(0)));
}

std::pair<std::u16string, int> ReplacerRegex::replace(std::u16string_view text, std::u16string_view replacement) const
{
    const std::wstring w = wide(text);
    std::u16string result;
    std::size_t matchStart = 0;
    int count = 0;
    while (true) {
        std::wcmatch m;
        const auto flags = count ? std::regex_constants::match_not_bol : std::regex_constants::match_default;
        try {
            if (!std::regex_search(w.data() + matchStart, w.data() + w.size(), m, m_impl->re, flags))
                break;
        } catch (const std::regex_error &) {
            break;
        }
        std::u16string textNew;
        for (std::size_t i = 0; i < replacement.size(); ++i) {
            std::optional<std::size_t> index;
            if (replacement[i] == u'\\') {
                if (++i == replacement.size()) {
                    textNew += u'\\'; // a trailing backslash stays
                    break;
                }
                if (isAsciiDigit(replacement[i])) {
                    // wxStrtoul: every digit; an index past the groups is eaten.
                    std::size_t n = 0;
                    for (; i < replacement.size() && isAsciiDigit(replacement[i]); ++i)
                        n = n > (SIZE_MAX - 9) / 10 ? SIZE_MAX : n * 10 + (replacement[i] - u'0');
                    --i;
                    index = n;
                }
            } else if (replacement[i] == u'&') {
                index = 0;
            }
            if (!index) {
                textNew += replacement[i];
            } else if (*index < m.size() && m[*index].matched) {
                const auto at = matchStart + static_cast<std::size_t>(m.position(*index));
                textNew += text.substr(at, static_cast<std::size_t>(m.length(*index)));
            }
        }
        result += text.substr(matchStart, static_cast<std::size_t>(m.position(0)));
        result += textNew;
        ++count;
        matchStart += static_cast<std::size_t>(m.position(0) + m.length(0));
        // An empty match is found again at the same place forever in legacy.
        if (m.length(0) == 0)
            break;
    }
    result += text.substr(matchStart);
    return {result, count};
}

// ---- find and replace in a Document

std::vector<ReplacerFind> findErrors(const EditSession &session, const std::vector<ReplacerRule> &rules,
                                     const ReplacerScope &scope, const ReplacerCase &cases)
{
    // SeekOnTab numbers each find with checkedRules[k], k counting only the
    // rules that compile: after an invalid checked rule the numbers shift.
    std::vector<int> checked;
    for (std::size_t i = 0; i < rules.size(); ++i)
        if (rules[i].checked)
            checked.push_back(static_cast<int>(i));
    std::vector<std::pair<ReplacerRegex, int>> compiled; // {expression, options}
    for (const int i : checked) {
        const auto &rule = rules[static_cast<std::size_t>(i)];
        if (auto re = ReplacerRegex::compile(core::toUtf16(rule.find), rule.options & kReplacerMatchCase, cases))
            compiled.emplace_back(std::move(*re), rule.options);
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
                    const auto match = re.search(u16sv(lineText).substr(textPos));
                    if (!match)
                        break;
                    const std::size_t length = std::max<std::size_t>(match->second, 1);
                    if (options < 16 || keepFinding(lineText, match->first + textPos, options))
                        out.push_back({row, shown + 1, lineText, match->first + textPos, length, checked[k]});
                    // Past the end legacy searches "" again (forever if that matches).
                    textPos += match->first + length;
                }
            }
        }
        ++shown;
    }
    return out;
}

std::expected<bool, CommandRefusal> replaceErrors(EditSession &session, const std::vector<ReplacerRule> &rules,
                                                  const ReplacerScope &scope, const ReplacerCase &cases)
{
    const auto compiled = compileChecked(rules, cases);
    if (compiled.empty())
        return false;
    const auto lines = session.document().lines();
    const Selection &selection = session.selection();
    std::set<core::LineId> touches;
    for (std::size_t row = startRow(lines, scope, selection); row < lines.size(); ++row) {
        const auto &line = *lines[row];
        if (line.visibility != core::LineVisibility::Hidden && inScope(line, scope, selection) &&
            replaceInText(searchedText(line), compiled, rules, cases))
            touches.insert(line.id);
    }
    if (touches.empty())
        return false;
    // The Lines are worked out again on the content the command sees (a
    // pending draft on one of them is committed first).
    const auto ran = session.run(Command{"Fixing minor errors", session.revision(), touches, [&](core::Document &d) {
                                             for (const auto id : touches) {
                                                 const auto *line = lineById(d, id);
                                                 if (!line)
                                                     return false;
                                                 const auto text = replaceInText(searchedText(*line), compiled, rules, cases);
                                                 if (text && !d.editLine(id, [&](core::LineRecord &l) { setSearchedText(l, *text); }))
                                                     return false;
                                             }
                                             return true;
                                         }});
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
    std::vector<ReplacerRegex> compiled;
    for (const auto &rule : rules)
        if (auto re = ReplacerRegex::compile(core::toUtf16(rule.find), rule.options & kReplacerMatchCase, cases))
            compiled.push_back(std::move(*re));

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
                    // Legacy reads past its expressions here (undefined); none replaces.
                    const ReplacerRegex *re = static_cast<std::size_t>(find.rule) < compiled.size() ? &compiled[static_cast<std::size_t>(find.rule)] : nullptr;
                    const auto replaced = re && rule ? re->replace(found, core::toUtf16(rule->replace))
                                                     : std::pair<std::u16string, int>{found, 0};
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
