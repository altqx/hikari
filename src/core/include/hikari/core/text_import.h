#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

inline constexpr std::size_t kMaxTextImportSize = std::size_t(8) << 20;

struct TextImportOptions {
    std::u8string actorSeparator = u8":"; // empty: no line takes a name
    std::u8string commentStarter = u8"#"; // empty: no line is a comment
    bool keepBlankLines = false;          // as empty comment lines
    bool namesCarryOver = true;           // a line without a name takes the last one
    bool operator==(const TextImportOptions &) const = default;
};

struct TextImportLine {
    std::u8string actor;
    std::u8string text;
    bool comment = false;
    bool operator==(const TextImportLine &) const = default;
};

enum class TextImportProblem { TimecodesFile, NoLines };

// Aegisub's plain text import: a line of UTF-8 `text` becomes a Line. A line starting with the comment
// starter is a comment without it. Otherwise, unless it starts with a space or tab, the text before the
// first actor separator is its name.
std::expected<std::vector<TextImportLine>, TextImportProblem> importText(std::u8string_view text,
                                                                         const TextImportOptions &options);

}
