#pragma once

#include "hikari/plugins/manifest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace hikari::plugins {

inline constexpr std::size_t kMaxPackageBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kMaxPackageFiles = 4096;

struct PackageFile {
    std::string path; // '/'-separated, relative to the package root
    std::vector<std::byte> data;
};

struct PackageProblem {
    enum class Kind { TooLarge, BadPath, NoManifest, Manifest, MissingMain };
    Kind kind = Kind::BadPath;
    std::string path;
    ManifestProblem manifest;
};

struct Package {
    Manifest manifest;
    std::vector<PackageFile> files;
};

// Refuses more than kMaxPackageFiles files or kMaxPackageBytes unpacked, and any path that is
// absolute, climbs out, repeats another case-insensitively, or that Windows cannot hold.
std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion);

}
