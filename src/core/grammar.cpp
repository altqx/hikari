#include "hikari/core/grammar.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <iterator>

namespace hikari::core::grammar {

namespace {

#include "grammar_rules.inc"

static_assert(std::ranges::is_sorted(kRules, {}, &Rule::name));

} // namespace

CheckedText checkedText(std::u16string_view raw)
{
    CheckedText out;
    const Projection projection = project(raw);
    for (const auto &span : projection.spans) {
        switch (span.kind) {
        case SpanKind::HiddenOverride:
            break;
        case SpanKind::Text:
            if (span.rawEnd - span.rawStart == span.displayEnd - span.displayStart) {
                for (std::size_t i = span.rawStart; i < span.rawEnd; ++i) {
                    out.text.push_back(raw[i]);
                    out.raw.emplace_back(i, i + 1);
                }
                break;
            }
            [[fallthrough]];
        default: // breaks, hard spaces, drawings and malformed spans
            out.text.push_back(u' ');
            out.raw.emplace_back(span.rawStart, span.rawEnd);
            break;
        }
    }
    return out;
}

std::optional<std::pair<std::size_t, std::size_t>> rawRange(const CheckedText &text, std::size_t start,
                                                             std::size_t end)
{
    if (start >= end || end > text.raw.size())
        return std::nullopt;
    return std::pair{text.raw[start].first, text.raw[end - 1].second};
}

std::span<const std::string_view> groupIds()
{
    return kGroupIds;
}

std::span<const Rule> rules()
{
    return kRules;
}

std::optional<std::size_t> groupOfRule(std::string_view rule)
{
    const auto it = std::ranges::lower_bound(kRules, rule, {}, &Rule::name);
    if (it == std::end(kRules) || it->name != rule)
        return std::nullopt;
    return it->group;
}

std::vector<std::pair<std::string_view, bool>> linterSwitches(std::span<const bool> groupsOn, bool hunspellOn)
{
    std::vector<std::pair<std::string_view, bool>> out;
    out.reserve(std::size(kRules));
    for (const auto &rule : kRules) {
        bool on = rule.on && rule.group < groupsOn.size() && groupsOn[rule.group];
        if (rule.name == "SentenceCapitalization" || (hunspellOn && rule.name == "SpellCheck"))
            on = false;
        out.emplace_back(rule.name, on);
    }
    return out;
}

} // namespace hikari::core::grammar
