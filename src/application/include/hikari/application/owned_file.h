#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::application {

// 1-based line and column; 0 when the failure has no place.
struct ParseFailure {
    int line = 0;
    int column = 0;
    std::string message;
};

struct FileProblem {
    std::filesystem::path file;
    // <name>.broken; empty when the file could not be moved, and then it must not be written.
    std::filesystem::path keptAs;
    ParseFailure failure;
};

// 1-based; the column counts UTF-8 characters, not bytes.
std::pair<int, int> lineColumnAt(std::string_view text, std::size_t offset);

// Nothing when 1 <= version <= supported, else the failure to show.
std::optional<ParseFailure> checkFileVersion(std::optional<std::int64_t> version, std::int64_t supported);

enum class FileState { Missing, Read, Broken };
struct FileLoad {
    FileState state = FileState::Missing;
    std::optional<FileProblem> problem; // set when Broken
};

// `parse` returns where it fails; a failing file is renamed to <name>.broken (or .broken-2 and on).
FileLoad loadOwnedFile(const std::filesystem::path &path,
                       const std::function<std::optional<ParseFailure>(std::string_view)> &parse);

enum class Conversion { NotNeeded, Converted, Failed };
// Runs `convert` only when `target` is missing and `legacy` exists; on true `legacy` becomes <legacy>.old
// (replacing an older one). `legacy` is never written.
Conversion convertOnce(const std::filesystem::path &target, const std::filesystem::path &legacy,
                       const std::function<bool()> &convert);

// nullopt when unreadable or not a regular file.
std::optional<std::string> readOwnedFile(const std::filesystem::path &path);
// Keeps an existing file's permissions; a new file takes those of `permissionsOf` when given.
bool writeFileAtomically(const std::filesystem::path &path, std::string_view bytes,
                         const std::filesystem::path &permissionsOf = {});

// UTF-8, native separators.
std::string displayPath(const std::filesystem::path &path);

}
