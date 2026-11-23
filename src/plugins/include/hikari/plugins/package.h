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
inline constexpr std::size_t kMaxPackageFiles = 4096;

struct PackageFile {
    std::string path; // '/'-separated, relative to the package root
    std::vector<std::byte> data;
};

struct PackageProblem {
    enum class Kind { TooLarge, BadPath, NoManifest, Manifest, MissingMain, Media, MissingPanel, QmlImport, MissingContent, BadContent };
    Kind kind = Kind::BadPath;
    std::string path;
    ManifestProblem manifest;
    std::string detail; // Media, BadContent: English, technical; QmlImport: the import as written
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
    std::vector<std::string> iconRoles; // every icon theme must hold <role>.svg for each
};

// Refuses more than kMaxPackageFiles files or kMaxPackageBytes unpacked, and any path that is
// absolute, climbs out, repeats another case-insensitively, or that Windows cannot hold, and an
// icon, screenshot or README.md that breaks the marketplace's media rules (docs/plugins.md), a QML panel's missing
// file, an import a QML panel may not make in any .qml or .js file of a package with QML panels, a content pack
// file that is missing or fails its check, a colour theme that fails themes::load or its entry's type, and an
// interface icon that is missing or breaks interfaceIconProblem's rules.
std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion,
                                                   const ContentFileChecks &checks = {});

// The media rules for one file, by its extension; nullopt when it passes. The detail is English and technical.
std::optional<std::string> iconProblem(const PackageFile &icon);
std::optional<std::string> screenshotProblem(const PackageFile &shot);
// The media rules for an SVG, plus a viewBox of 0 0 16 16 and drawing in currentColor only.
std::optional<std::string> interfaceIconProblem(const PackageFile &icon);

}
