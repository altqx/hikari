#include "hikari/application/word_diff.h"

#include <algorithm>

namespace hikari::application {

namespace {

constexpr std::size_t kMaxCells = 1u << 20;

bool wordChar(char16_t c)
{
    return c > 0x7f || (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') ||
           c == u'\'' || c == u'_';
}

struct Token {
    std::size_t start = 0, length = 0;
};

std::vector<Token> tokens(std::u16string_view text)
{
    std::vector<Token> out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t j = i + 1;
        if (wordChar(text[i]))
            while (j < text.size() && wordChar(text[j]))
                ++j;
        out.push_back({i, j - i});
        i = j;
    }
    return out;
}

void push(std::vector<DiffPart> &parts, DiffPart::Kind kind, const Token &token)
{
    if (!parts.empty() && parts.back().kind == kind && parts.back().start + parts.back().length == token.start) {
        parts.back().length += token.length;
        return;
    }
    parts.push_back({kind, token.start, token.length});
}

}

std::vector<DiffPart> wordDiff(std::u16string_view before, std::u16string_view after)
{
    const std::vector<Token> a = tokens(before), b = tokens(after);
    std::vector<DiffPart> parts;
    if ((a.size() + 1) * (b.size() + 1) > kMaxCells) {
        if (!before.empty())
            parts.push_back({DiffPart::Kind::Removed, 0, before.size()});
        if (!after.empty())
            parts.push_back({DiffPart::Kind::Added, 0, after.size()});
        return parts;
    }
    const auto same = [&](std::size_t i, std::size_t j) {
        return before.substr(a[i].start, a[i].length) == after.substr(b[j].start, b[j].length);
    };
    const std::size_t width = b.size() + 1;
    std::vector<std::size_t> common((a.size() + 1) * width, 0);
    for (std::size_t i = a.size(); i-- > 0;)
        for (std::size_t j = b.size(); j-- > 0;)
            common[i * width + j] = same(i, j) ? common[(i + 1) * width + j + 1] + 1
                                               : std::max(common[(i + 1) * width + j], common[i * width + j + 1]);
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && same(i, j)) {
            push(parts, DiffPart::Kind::Same, b[j]);
            ++i;
            ++j;
        } else if (i < a.size() && (j == b.size() || common[(i + 1) * width + j] >= common[i * width + j + 1])) {
            push(parts, DiffPart::Kind::Removed, a[i++]);
        } else {
            push(parts, DiffPart::Kind::Added, b[j++]);
        }
    }
    return parts;
}

}
