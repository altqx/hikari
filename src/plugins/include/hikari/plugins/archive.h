#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace hikari::plugins {

// The package path rules: relative, '/'-separated, no "." or "..", only portable characters, nothing Windows can't hold.
bool goodPackagePath(std::string_view path);

struct UnpackLimits {
    std::uint64_t bytes = 0;
    std::size_t files = 0;
};

struct UnpackProblem {
    enum class Kind { NotAnArchive, Truncated, BadPath, Link, TooLarge, Write };
    Kind kind = Kind::NotAnArchive;
    std::string path; // the entry, when one is at fault
};

// Unpacks a gzip-compressed tar into `into`, which must exist and be empty. Refuses links, devices and any entry
// path that breaks goodPackagePath or repeats another case-insensitively. Keeps the owner's execute bit.
std::expected<void, UnpackProblem> unpackTarGz(const std::filesystem::path &archive, const std::filesystem::path &into,
                                               UnpackLimits limits);

}
