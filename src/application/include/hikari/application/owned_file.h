#pragma once

// CF1 (#488): the files of the user folder that HikariSub owns and converted
// from a legacy file (settings.toml from hikari.ini; CF2's list files, CF3's
// sessions). Each is converted once: the legacy file becomes <name>.old and
// is never written again. A file that does not read (bad syntax, no or a
// newer `version`) is never overwritten: it is kept as <name>.broken, the
// caller uses its defaults and names the file and line in a notice.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::application {

// Where a file's text fails; line and column are 1-based, 0 when the failure
// has no place (a newer version).
struct ParseFailure {
    int line = 0;
    int column = 0;
    std::string message;
};

struct FileProblem {
    std::filesystem::path file;
    // Where the file was kept (<name>.broken); empty when it could not be
    // moved, and then it must not be written either.
    std::filesystem::path keptAs;
    ParseFailure failure;
};

// The 1-based line and column of byte `offset` in `text` (for parsers that
// report an offset, such as QJsonParseError).
std::pair<int, int> lineColumnAt(std::string_view text, std::size_t offset);

// Nothing when 1 <= version <= supported, else the failure to show.
std::optional<ParseFailure> checkFileVersion(std::optional<std::int64_t> version, std::int64_t supported);

enum class FileState { Missing, Read, Broken };
struct FileLoad {
    FileState state = FileState::Missing;
    std::optional<FileProblem> problem; // set when Broken
};

// Reads `path` and hands its bytes to `parse`, which keeps what it read and
// returns where it fails. A file that fails becomes <name>.broken (or
// <name>.broken-2 and on when one is there already).
FileLoad loadOwnedFile(const std::filesystem::path &path,
                       const std::function<std::optional<ParseFailure>(std::string_view)> &parse);

enum class Conversion { NotNeeded, Converted, Failed };
// When `target` does not exist and `legacy` does, `convert` writes the new
// file or files from it; when it returns true, `legacy` becomes <legacy>.old
// (an older .old is replaced). `legacy` is never written.
Conversion convertOnce(const std::filesystem::path &target, const std::filesystem::path &legacy,
                       const std::function<bool()> &convert);

// The file's bytes, nothing when it cannot be read (or is not a file).
std::optional<std::string> readOwnedFile(const std::filesystem::path &path);
// Writes `bytes` to a temporary file beside `path` and renames it over `path`;
// an existing file's permissions carry over, and a new file takes those of
// `permissionsOf` when given (the file it is converted from).
bool writeFileAtomically(const std::filesystem::path &path, std::string_view bytes,
                         const std::filesystem::path &permissionsOf = {});

// A path's text for messages (UTF-8, native separators).
std::string displayPath(const std::filesystem::path &path);

} // namespace hikari::application
