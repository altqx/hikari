#pragma once

#include "hikari/plugins/manifest.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::plugins {

inline constexpr std::size_t kMaxPackageBytes = 64 * 1024 * 1024;
// A package built for one platform: it lacks another platform's bundled programs.
inline constexpr std::size_t kMaxPlatformPackageBytes = 256 * 1024 * 1024;
inline constexpr std::size_t kMaxPackageFiles = 4096;

struct PackageFile {
    std::string path; // '/'-separated, relative to the package root
    std::vector<std::byte> data;
};

struct PackageProblem {
    enum class Kind { TooLarge, BadPath, NoManifest, Manifest, MissingMain, Media, MissingPanel, QmlImport, MissingContent, BadContent, MissingProgram };
    Kind kind = Kind::BadPath;
    std::string path;
    ManifestProblem manifest;
    std::string detail; // Media, BadContent: English, technical; QmlImport: the import as written
    std::size_t limit = kMaxPackageBytes; // TooLarge: the unpacked bytes allowed
};

struct Package {
    Manifest manifest;
    std::vector<PackageFile> files;
};

// Reads a content pack file as its built-in counterpart does: why it can't, in English, or nothing.
using ContentFileCheck = std::function<std::optional<std::string>(std::span<const std::byte>)>;

// The checks for each kind of content pack file; an unset one only checks that the file is there.
struct ContentFileChecks {
    ContentFileCheck styleCatalogue;
    ContentFileCheck karaokePreset;
};

// Refuses more than kMaxPackageFiles files or kMaxPackageBytes unpacked (kMaxPlatformPackageBytes for a package built
// for one platform), a bundled program missing for this platform, and any path that is
// absolute, climbs out, repeats another case-insensitively, or that Windows cannot hold, and an
// icon, screenshot or README.md that breaks the marketplace's media rules (docs/plugins.md), a QML panel's missing
// file, an import a QML panel may not make in any .qml or .js file of a package with QML panels, and a content pack
// file that is missing or fails its check.
std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion,
                                                   const ContentFileChecks &checks = {});

// The media rules for one file, by its extension; nullopt when it passes. The detail is English and technical.
std::optional<std::string> iconProblem(const PackageFile &icon);
std::optional<std::string> screenshotProblem(const PackageFile &shot);

}
