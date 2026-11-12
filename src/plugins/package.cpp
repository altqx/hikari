#include "hikari/plugins/package.h"

#include "media.h"
#include "hikari/api/file_names.h"

#include "hikari/plugins/qml_imports.h"

#include <algorithm>
#include <set>

namespace hikari::plugins {

namespace {

bool allowedChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
           c == '.' || c == '-' || c == '+';
}

bool goodPart(std::string_view part)
{
    return !part.empty() && part != "." && part != ".." && part.size() <= 255 &&
           std::all_of(part.begin(), part.end(), allowedChar) && !part.ends_with('.') && !part.ends_with(' ') &&
           !api::reservedOnWindows(part);
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

std::optional<PackageProblem> qmlProblem(const Manifest &manifest, const std::vector<PackageFile> &files)
{
    using Kind = PackageProblem::Kind;
    if (!manifest.hasQmlPanels())
        return std::nullopt;
    for (const Panel &panel : manifest.panels)
        if (!panel.qml.empty() && std::ranges::none_of(files, [&](const PackageFile &f) { return f.path == panel.qml; }))
            return PackageProblem{Kind::MissingPanel, panel.qml, {}, {}};
    for (const PackageFile &file : files)
        if (isQmlSource(file.path))
            if (auto refused = qmlImportProblem(file.path, {reinterpret_cast<const char *>(file.data.data()), file.data.size()}))
                return PackageProblem{Kind::QmlImport, file.path, {}, std::move(*refused)};
    return std::nullopt;
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
        if (!goodPath(file.path) || !seen.insert(api::lowered(file.path)).second)
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
    if (auto problem = mediaProblem(*manifest, files))
        return std::unexpected(std::move(*problem));
    if (auto problem = qmlProblem(*manifest, files))
        return std::unexpected(std::move(*problem));
    return Package{std::move(*manifest), std::move(files)};
}

}
