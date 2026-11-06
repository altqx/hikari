#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace hikari::application {

struct DiffPart {
    enum class Kind { Same, Removed, Added };
    Kind kind = Kind::Same;
    // In `before` for Removed, in `after` otherwise.
    std::size_t start = 0, length = 0;
};

// Word by word, in order; a whole replacement when the texts are too long to compare.
std::vector<DiffPart> wordDiff(std::u16string_view before, std::u16string_view after);

}
