#pragma once

// CF1 (#488) / CF3 (#490): the files the program owns in the user folder,
// converted once from their legacy form and never overwritten when broken.
// CF1's shared helper; this copy has its interface, and the merge keeps one.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::application {

struct ParseFailure {
    int line = 0;   // 1-based; 0: no place in the file
    int column = 0; // 1-based; 0: no place
    std::string message;
};

struct FileProblem {
    std::filesystem::path file;   // the broken file, as it was named
    std::filesystem::path keptAs; // where it is kept (<name>.broken); empty when it could not be renamed
    ParseFailure failure;
};

// The 1-based line and column of byte `offset` in `text` (for
// QJsonParseError::offset); an offset past the end is the end's.
std::pair<int, int> lineColumnAt(std::string_view text, std::size_t offset);

// Nothing when 1 <= version <= supported, else the failure a notice shows: a
// missing version, or one this program does not know (a newer program's).
std::optional<ParseFailure> checkFileVersion(std::optional<std::int64_t> version, std::int64_t supported);

enum class FileState { Missing, Read, Broken };

struct FileLoad {
    FileState state = FileState::Missing;
    std::optional<FileProblem> problem; // with Broken
};

// Reads `path` and hands its bytes to `parse`, which returns the failure of
// a broken file. A broken file is never overwritten: it becomes
// <name>.broken (<name>.broken-N when that is taken) and the caller uses
// defaults.
FileLoad loadOwnedFile(const std::filesystem::path &path,
                       const std::function<std::optional<ParseFailure>(std::string_view)> &parse);

enum class Conversion { NotNeeded, Converted, Failed };

// When `target` is missing and `legacy` exists: `convert` writes the new
// file(s); when it returns true, `legacy` becomes <legacy>.old. Nothing
// writes to `legacy`; after a failure it stays for the next start.
Conversion convertOnce(const std::filesystem::path &target, const std::filesystem::path &legacy,
                       const std::function<bool()> &convert);

// `bytes` into a temporary sibling, then renamed over `path` (its folder made
// first).
bool writeFileAtomically(const std::filesystem::path &path, std::string_view bytes);

} // namespace hikari::application
