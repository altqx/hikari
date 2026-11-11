#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::ai {

struct SearchSuggestion {
    std::string text;
    std::string url;
};

/// Google's search suggestions from a grounded Gemini answer, as the snippet sends them: each chip's text and https
/// link unchanged, and the snippet's light and dark logos as SVG text.
struct SearchSuggestions {
    std::vector<SearchSuggestion> chips;
    std::string lightLogo;
    std::string darkLogo;
};

inline constexpr std::size_t kMaxSearchSuggestions = 5;

/// The chips and logos in `searchEntryPoint.renderedContent`, or nullopt when it has no chip.
std::optional<SearchSuggestions> parseSearchSuggestions(std::string_view html);

}
