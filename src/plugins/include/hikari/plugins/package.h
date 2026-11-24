#pragma once

#include "hikari/plugins/manifest.h"

#include <cstddef>
#include <expected>
#include <filesystem>
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
    enum class Kind { TooLarge, BadPath, NoManifest, Manifest, MissingMain, Media, MissingPanel, QmlImport, MissingContent, BadContent, MissingProgram,
                      MainProgramUnavailable };
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
    ContentFileCheck dictionaryAff;
    ContentFileCheck dictionaryDic;
    ContentFileCheck graph;
    ContentFileCheck layout;
    std::vector<std::string> iconRoles; // every icon theme must hold <role>.svg for each
};

// Refuses more than kMaxPackageFiles files or kMaxPackageBytes unpacked (kMaxPlatformPackageBytes for a package built
// for one platform), a bundled program missing for this platform, and any path that is
// absolute, climbs out, repeats another case-insensitively, or that Windows cannot hold, and an
// icon, screenshot or README.md that breaks the marketplace's media rules (docs/plugins.md), a QML panel's missing
// file, an import a QML panel may not make in any .qml or .js file of a package with QML panels, a content pack
// file that is missing or fails its check, a colour theme that fails themes::load or its entry's type, and an
// interface icon that is missing or breaks interfaceIconProblem's rules.
std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion,
                                                   const ContentFileChecks &checks = {});

// A dev plugin's folder as package files. Entries whose name starts with '.' are left out; a symbolic link is refused
// as BadPath. Reading stops as TooLarge past kMaxPackageFiles files or kMaxPlatformPackageBytes.
std::expected<std::vector<PackageFile>, PackageProblem> readPluginFolder(const std::filesystem::path &folder);

// The media rules for one file, by its extension; nullopt when it passes. The detail is English and technical.
std::optional<std::string> iconProblem(const PackageFile &icon);
std::optional<std::string> screenshotProblem(const PackageFile &shot);
// The media rules for an SVG, plus a viewBox of 0 0 16 16 and drawing in currentColor only.
std::optional<std::string> interfaceIconProblem(const PackageFile &icon);

}
