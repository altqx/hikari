// The fold pairing ports arch1t3cht's Aegisub FoldController, under its licence:
//
// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "hikari/core/aegisub_folds.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>

namespace hikari::core {

namespace {

struct Delimiter {
    LineId line;
    std::size_t row = 0;
    bool ender = false;
    bool collapsed = false;
    int id = 0;
};

std::optional<int> integer(std::u8string_view text)
{
    if (!text.empty() && text.front() == u8'+')
        text.remove_prefix(1);
    const auto *begin = reinterpret_cast<const char *>(text.data());
    const auto *end = begin + text.size();
    int value = 0;
    const auto [at, error] = std::from_chars(begin, end, value);
    if (text.empty() || error != std::errc() || at != end)
        return std::nullopt;
    return value;
}

std::optional<Delimiter> delimiter(const LineRecord &line, std::size_t row)
{
    const auto entry = std::ranges::find(line.extra, kFoldKey, &Extradata::key);
    if (entry == line.extra.end())
        return std::nullopt;
    std::vector<std::u8string_view> fields;
    for (std::u8string_view rest = entry->value;;) {
        const auto semicolon = rest.find(u8';');
        fields.push_back(rest.substr(0, semicolon));
        if (semicolon == std::u8string_view::npos)
            break;
        rest.remove_prefix(semicolon + 1);
    }
    if (fields.size() != 3)
        return std::nullopt;
    const auto side = integer(fields[0]);
    const auto collapsed = integer(fields[1]);
    const auto id = integer(fields[2]);
    if (!side || !collapsed || !id)
        return std::nullopt;
    return Delimiter{line.id, row, *side != 0, *collapsed != 0, *id};
}

}

AegisubFolds aegisubFolds(const Document &document)
{
    std::vector<Delimiter> delimiters;
    std::size_t row = 0;
    for (const auto *line : document.lines()) {
        if (auto found = delimiter(*line, row))
            delimiters.push_back(*found);
        ++row;
    }
    int maxId = 0;
    for (const auto &d : delimiters)
        maxId = std::max(maxId, d.id);

    AegisubFolds out;
    std::vector<std::pair<AegisubFold, std::size_t>> folds;
    std::vector<const Delimiter *> stack;
    std::set<int> heads;
    std::set<int> completed;
    std::map<int, int> remap;
    for (auto &d : delimiters) {
        for (auto it = remap.find(d.id); it != remap.end(); it = remap.find(d.id))
            d.id = it->second;
        if (completed.contains(d.id))
            d.id = remap[d.id] = ++maxId;
        if (!d.ender) {
            if (heads.contains(d.id)) {
                out.dropped.push_back(d.line);
            } else {
                heads.insert(d.id);
                stack.push_back(&d);
            }
            continue;
        }
        completed.insert(d.id);
        if (!heads.contains(d.id)) {
            out.dropped.push_back(d.line);
            continue;
        }
        const auto opener = std::ranges::find(stack.rbegin(), stack.rend(), d.id, &Delimiter::id);
        if (opener == stack.rend()) {
            out.dropped.push_back(d.line);
            continue;
        }
        const auto at = static_cast<std::size_t>(stack.rend() - opener) - 1;
        for (std::size_t inner = at + 1; inner < stack.size(); ++inner) {
            completed.insert(stack[inner]->id);
            out.dropped.push_back(stack[inner]->line);
        }
        folds.push_back({AegisubFold{stack[at]->line, d.line, stack[at]->collapsed}, stack[at]->row});
        stack.resize(at);
    }
    for (const auto *d : stack)
        out.dropped.push_back(d->line);
    std::ranges::sort(folds, {}, &std::pair<AegisubFold, std::size_t>::second);
    for (auto &[fold, openerRow] : folds)
        out.folds.push_back(fold);
    return out;
}

}
