#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::testing {

inline std::string canonicalTags(std::string_view text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '{') {
            out += text[i++];
            continue;
        }
        const std::size_t close = text.find('}', i);
        if (close == std::string_view::npos) {
            out += text.substr(i);
            break;
        }
        const std::string_view block = text.substr(i + 1, close - i - 1);
        std::vector<std::string> tags;
        int depth = 0;
        for (const char c : block) {
            if (c == '\\' && depth == 0)
                tags.emplace_back();
            if (tags.empty())
                tags.emplace_back();
            tags.back() += c;
            depth += c == '(' ? 1 : c == ')' && depth > 0 ? -1 : 0;
        }
        std::ranges::sort(tags);
        out += '{';
        for (const auto &t : tags)
            out += t;
        out += '}';
        i = close + 1;
    }
    return out;
}

}
