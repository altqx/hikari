#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Lua 5.1 string patterns, byte for byte as LuaJIT's string.find and string.gsub run them, for
// ports of bundled Automation scripts that must give the same text. Classes are the C locale's.
// A malformed pattern throws std::invalid_argument.
namespace hikari::core::luapattern {

// A position capture "()" holds its 1-based position as decimal text.
using Captures = std::vector<std::string>;

struct Match {
    std::size_t start = 0, end = 0; // [start, end), 0-based
    Captures captures;
};

std::optional<Match> find(std::string_view subject, std::string_view pattern, std::size_t from = 0);

struct Replaced {
    std::string text;
    int count = 0;
};

// `replacement` reads %0-%9 and %%; with no captures %1 is the whole match.
Replaced gsub(std::string_view subject, std::string_view pattern, std::string_view replacement);
// `replacement` gets the captures (the whole match when there are none); nullopt keeps the match.
Replaced gsub(std::string_view subject, std::string_view pattern,
              const std::function<std::optional<std::string>(const Captures &)> &replacement);

}
