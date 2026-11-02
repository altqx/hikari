#include "hikari/plugins/package.h"

#include <algorithm>
#include <array>
#include <set>

namespace hikari::plugins {

namespace {

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

bool allowedChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
           c == '.' || c == '-' || c == '+';
}

bool reservedOnWindows(std::string_view part)
{
    static constexpr std::array kDevices{"con", "prn", "aux", "nul"};
    const std::string stem = lowered(part.substr(0, part.find('.')));
    if (std::find(kDevices.begin(), kDevices.end(), stem) != kDevices.end())
        return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

bool goodPart(std::string_view part)
{
    return !part.empty() && part != "." && part != ".." && part.size() <= 255 &&
           std::all_of(part.begin(), part.end(), allowedChar) && !part.ends_with('.') && !part.ends_with(' ') &&
           !reservedOnWindows(part);
}

bool goodPath(std::string_view path)
{
    if (path.empty() || path.size() > 400)
        return false;
    while (true) {
        const std::size_t slash = path.find('/');
        if (!goodPart(path.substr(0, slash)))
            return false;
        if (slash == std::string_view::npos)
            return true;
        path.remove_prefix(slash + 1);
    }
}

}

std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion)
{
    using Kind = PackageProblem::Kind;
    if (files.size() > kMaxPackageFiles)
        return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}});
    std::size_t total = 0;
    std::set<std::string> seen;
    const PackageFile *manifestFile = nullptr;
    for (const PackageFile &file : files) {
        total += file.data.size();
        if (total > kMaxPackageBytes)
            return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}});
        if (!goodPath(file.path) || !seen.insert(lowered(file.path)).second)
            return std::unexpected(PackageProblem{Kind::BadPath, file.path, {}});
        if (file.path == kManifestName)
            manifestFile = &file;
    }
    std::set<std::string> folders;
    for (const std::string &path : seen)
        for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
            folders.insert(path.substr(0, slash));
    for (const std::string &path : seen)
        if (folders.contains(path))
            return std::unexpected(PackageProblem{Kind::BadPath, path, {}});
    if (!manifestFile)
        return std::unexpected(PackageProblem{Kind::NoManifest, std::string(kManifestName), {}});

    const auto &bytes = manifestFile->data;
    auto manifest = parseManifest(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()), apiVersion);
    if (!manifest)
        return std::unexpected(PackageProblem{Kind::Manifest, std::string(kManifestName), manifest.error()});
    if (std::none_of(files.begin(), files.end(), [&](const PackageFile &f) { return f.path == manifest->main; }))
        return std::unexpected(PackageProblem{Kind::MissingMain, manifest->main, {}});
    return Package{std::move(*manifest), std::move(files)};
}

}
