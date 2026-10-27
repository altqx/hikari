#include "hikari/core/grammar.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <iterator>

namespace hikari::core::grammar {

namespace {

#include "grammar_rules.inc"

static_assert(std::ranges::is_sorted(kRules, {}, &Rule::name));

bool projected(SubtitleFormat format)
{
    return format == SubtitleFormat::Ass || format == SubtitleFormat::PlainText;
}

// SRT, TMPlayer, MicroDVD and MPL2 (spelling's syntaxOf): a tag is left out,
// a break is a space over it, an unclosed tag one space to the end.
CheckedText scanned(std::u16string_view raw, SubtitleFormat format)
{
    const bool srt = format == SubtitleFormat::Srt;
    const char16_t open = srt ? u'<' : u'{';
    const char16_t close = srt ? u'>' : u'}';
    CheckedText out;
    auto space = [&](std::size_t from, std::size_t to) {
        out.text += u' ';
        out.raw.emplace_back(from, to);
    };
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char16_t c = raw[i];
        if (c == open) {
            const auto to = raw.find(close, i + 1);
            if (to == std::u16string_view::npos) {
                space(i, raw.size());
                break;
            }
            i = to;
        } else if (!srt && c == u'|') {
            space(i, i + 1);
        } else if (srt && c == u'\\' && i + 1 < raw.size() &&
                   (raw[i + 1] == u'N' || raw[i + 1] == u'n' || raw[i + 1] == u'h')) {
            space(i, i + 2);
            ++i;
        } else {
            out.text += c;
            out.raw.emplace_back(i, i + 1);
        }
    }
    return out;
}

} // namespace

CheckedText checkedText(std::u16string_view raw, SubtitleFormat format)
{
    if (!projected(format))
        return scanned(raw, format);
    const Projection projection = project(raw);
    CheckedText out{projection.text, std::vector<std::pair<std::size_t, std::size_t>>(projection.text.size())};
    for (const auto &span : projection.spans) {
        const bool text = span.kind == SpanKind::Text && span.rawEnd - span.rawStart == span.displayEnd - span.displayStart;
        for (std::size_t at = span.displayStart; at < span.displayEnd; ++at) {
            if (text) {
                out.raw[at] = {span.rawStart + (at - span.displayStart), span.rawStart + (at - span.displayStart) + 1};
            } else { // breaks, hard spaces, drawings and malformed spans
                out.text[at] = u' ';
                out.raw[at] = {span.rawStart, span.rawEnd};
            }
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

std::optional<Replaced> replace(std::u16string_view raw, SubtitleFormat format, std::size_t start, std::size_t end,
                                std::u16string_view replacement)
{
    if (start >= end)
        return std::nullopt;
    if (projected(format)) {
        auto replaced = mappedReplace(raw, start, end, replacement);
        if (!replaced)
            return std::nullopt;
        const std::size_t caret = rawOffset(project(*replaced), start + replacement.size(), false);
        return Replaced{std::move(*replaced), caret};
    }
    const CheckedText text = scanned(raw, format);
    const auto range = rawRange(text, start, end);
    const bool srt = format == SubtitleFormat::Srt;
    const std::u16string_view syntax = srt ? u"<>\\\n\r" : u"{}|\n\r";
    if (!range || replacement.find_first_of(syntax) != std::u16string_view::npos)
        return std::nullopt;
    // An unclosed tag is never touched.
    for (std::size_t at = start; at < end; ++at)
        if (raw[text.raw[at].first] == (srt ? u'<' : u'{'))
            return std::nullopt;
    // The tags between the range's units, in order, after the replacement.
    std::u16string out(raw.substr(0, range->first));
    out += replacement;
    for (std::size_t at = start + 1; at < end; ++at)
        out += raw.substr(text.raw[at - 1].second, text.raw[at].first - text.raw[at - 1].second);
    const std::size_t caret = range->first + replacement.size();
    out += raw.substr(range->second);
    return Replaced{std::move(out), caret};
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
