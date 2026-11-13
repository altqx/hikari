#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr std::size_t kFileDiffMaxBytes = 256 * 1024;

struct FileDiffLine {
    enum class Kind { Same, Removed, Added, Gap };
    Kind kind = Kind::Same;
    std::string text; // without its line break; empty for a Gap
};

// A line diff of two versions of a text file. `before` absent: a new file.
struct FileDiff {
    bool text = false; // false: not UTF-8 text, or over kFileDiffMaxBytes, so only sizes are known
    std::size_t added = 0;
    std::size_t removed = 0;
    std::size_t lines = 0; // the new version's line count
    std::size_t beforeBytes = 0;
    std::size_t afterBytes = 0;
    // The changed lines with `context` lines around them; a Gap stands for the unchanged lines left out.
    std::vector<FileDiffLine> shown;
};

FileDiff fileDiff(const std::optional<std::string> &before, std::string_view after, std::size_t context = 3);

}
