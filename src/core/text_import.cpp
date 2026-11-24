#include "hikari/core/text_import.h"

#include <algorithm>

namespace hikari::core {

namespace {

using u8sv = std::u8string_view;

bool isSpace(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\f' || c == u8'\v';
}

u8sv trim(u8sv s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

}

std::expected<std::vector<TextImportLine>, TextImportProblem> importText(u8sv text, const TextImportOptions &options)
{
    std::vector<TextImportLine> lines;
    std::u8string actor;
    for (std::size_t from = 0; from < text.size();) {
        const std::size_t end = std::min(text.find(u8'\n', from), text.size());
        u8sv value = text.substr(from, end - from);
        from = end + 1;
        if (!value.empty() && value.back() == u8'\r')
            value.remove_suffix(1);
        if (trim(value).empty() && !options.keepBlankLines)
            continue;
        if (value.starts_with(u8"# timecode"))
            return std::unexpected(TextImportProblem::TimecodesFile);
        TextImportLine line;
        if (!options.commentStarter.empty() && value.starts_with(options.commentStarter)) {
            line.comment = true;
            value.remove_prefix(options.commentStarter.size());
        }
        if (!options.namesCarryOver)
            actor.clear();
        if (!line.comment && !options.actorSeparator.empty() && !value.empty() && value.front() != u8' ' &&
            value.front() != u8'\t') {
            if (const std::size_t at = value.find(options.actorSeparator); at != u8sv::npos) {
                actor = trim(value.substr(0, at));
                value.remove_prefix(at + options.actorSeparator.size());
            }
        }
        line.text = trim(value);
        line.comment = line.comment || line.text.empty();
        if (!line.comment)
            line.actor = actor;
        lines.push_back(std::move(line));
    }
    if (lines.empty())
        return std::unexpected(TextImportProblem::NoLines);
    return lines;
}

}
