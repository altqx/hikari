#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace hikari::plugins {

// The package path rules: relative, '/'-separated, no "." or "..", only portable characters, nothing Windows can't hold.
bool goodPackagePath(std::string_view path);

struct UnpackLimits {
    std::uint64_t bytes = 0;
    std::size_t files = 0;
};

inline constexpr UnpackLimits kProgramDownloadLimits{std::uint64_t(1) << 30, 16384};

struct UnpackProblem {
    enum class Kind { NotAnArchive, Truncated, BadPath, Link, TooLarge, Write };
    Kind kind = Kind::NotAnArchive;
    std::string path; // the entry, when one is at fault
};

// The entries of one archive so far: refuses a path that breaks goodPackagePath, repeats another case-insensitively
// or clashes with a folder, and files past the limits.
class UnpackClaims {
public:
    explicit UnpackClaims(UnpackLimits limits) : m_limits(limits) {}
    std::expected<void, UnpackProblem> folder(const std::string &path);
    std::expected<void, UnpackProblem> file(const std::string &path, std::uint64_t size);

private:
    std::expected<void, UnpackProblem> parents(const std::string &path);

    UnpackLimits m_limits;
    std::uint64_t m_total = 0;
    std::set<std::string> m_files, m_folders;
};

// Unpacks a gzip-compressed tar into `into`, which must exist and be empty. Refuses links, devices and any entry
// path that breaks goodPackagePath or repeats another case-insensitively. Keeps the owner's execute bit.
std::expected<void, UnpackProblem> unpackTarGz(const std::filesystem::path &archive, const std::filesystem::path &into,
                                               UnpackLimits limits);

}
