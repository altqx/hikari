#include "hikari/plugins/package.h"

#include "media.h"
#include "hikari/api/file_names.h"

#include "hikari/plugins/archive.h"
#include "hikari/plugins/qml_imports.h"
#include "hikari/themes/theme.h"

#include <algorithm>
#include <set>

namespace hikari::plugins {

namespace {

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

std::optional<PackageProblem> contentProblem(const std::string &path, const ContentFileCheck &check,
                                            const std::vector<PackageFile> &files)
{
    using Kind = PackageProblem::Kind;
    const auto file = std::ranges::find(files, path, &PackageFile::path);
    if (file == files.end())
        return PackageProblem{Kind::MissingContent, path, {}, {}};
    if (check)
        if (auto refused = check(file->data))
            return PackageProblem{Kind::BadContent, path, {}, std::move(*refused)};
    return std::nullopt;
}

std::optional<PackageProblem> contentProblem(const std::vector<ContentFile> &content, const ContentFileCheck &check,
                                            const std::vector<PackageFile> &files)
{
    for (const ContentFile &item : content)
        if (auto problem = contentProblem(item.path, check, files))
            return problem;
    return std::nullopt;
}

std::optional<PackageProblem> dictionaryProblem(const std::vector<Dictionary> &dictionaries,
                                               const ContentFileChecks &checks, const std::vector<PackageFile> &files)
{
    for (const Dictionary &dictionary : dictionaries) {
        if (auto problem = contentProblem(dictionary.aff, checks.dictionaryAff, files))
            return problem;
        if (auto problem = contentProblem(dictionary.dic, checks.dictionaryDic, files))
            return problem;
    }
    return std::nullopt;
}


std::optional<PackageProblem> themeProblem(const std::vector<ColourTheme> &themes, const std::vector<PackageFile> &files)
{
    using Kind = PackageProblem::Kind;
    for (const ColourTheme &theme : themes) {
        const auto file = std::ranges::find(files, theme.path, &PackageFile::path);
        if (file == files.end())
            return PackageProblem{Kind::MissingContent, theme.path, {}, {}};
        const auto loaded = themes::load({reinterpret_cast<const char *>(file->data.data()), file->data.size()});
        if (!loaded)
            return PackageProblem{Kind::BadContent, theme.path, {},
                                  "fails the theme check at " + (loaded.error().path.empty() ? "/" : loaded.error().path) +
                                      ": " + loaded.error().message};
        if (const std::string_view type = themes::typeName(loaded->type); type != theme.type)
            return PackageProblem{Kind::BadContent, theme.path, {},
                                  "is a " + std::string(type) + " theme, and hikari-plugin.json lists it as " + theme.type};
    }
    return std::nullopt;
}

std::optional<PackageProblem> interfaceIconFileProblem(const std::string &path, const std::vector<PackageFile> &files)
{
    using Kind = PackageProblem::Kind;
    const auto file = std::ranges::find(files, path, &PackageFile::path);
    if (file == files.end())
        return PackageProblem{Kind::MissingContent, path, {}, {}};
    if (auto refused = interfaceIconProblem(*file))
        return PackageProblem{Kind::BadContent, path, {}, std::move(*refused)};
    return std::nullopt;
}

std::optional<PackageProblem> interfaceIconsProblem(const Manifest &manifest, const std::vector<std::string> &roles,
                                                   const std::vector<PackageFile> &files)
{
    for (const IconTheme &theme : manifest.iconThemes)
        for (const std::string &role : roles)
            if (auto problem = interfaceIconFileProblem(theme.folder + "/" + role + ".svg", files))
                return problem;
    for (const OwnIcon &icon : manifest.icons)
        if (auto problem = interfaceIconFileProblem(icon.path, files))
            return problem;
    return std::nullopt;
}
}

std::expected<Package, PackageProblem> openPackage(std::vector<PackageFile> files, std::int64_t apiVersion,
                                                   const ContentFileChecks &checks)
{
    using Kind = PackageProblem::Kind;
    if (files.size() > kMaxPackageFiles)
        return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}});
    std::size_t total = 0;
    std::set<std::string> seen;
    const PackageFile *manifestFile = nullptr;
    for (const PackageFile &file : files) {
        total += file.data.size();
        if (total > kMaxPlatformPackageBytes)
            return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}, {}, kMaxPlatformPackageBytes});
        if (!goodPackagePath(file.path) || !seen.insert(api::lowered(file.path)).second)
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
    const auto holds = [&](const std::string &path) {
        return std::ranges::any_of(files, [&](const PackageFile &f) { return f.path == path; });
    };
    if (!manifest->main.empty() && !holds(manifest->main))
        return std::unexpected(PackageProblem{Kind::MissingMain, manifest->main, {}});
    if (const Program *main = manifest->program(manifest->mainProgram);
        main && !main->platforms.contains(std::string(thisPlatform())))
        return std::unexpected(PackageProblem{Kind::MainProgramUnavailable, main->title, {}});
    bool forOnePlatform = false;
    for (const Program &program : manifest->programs)
        for (const auto &[platform, run] : program.platforms) {
            if (run.download || holds(run.path))
                continue;
            if (platform == thisPlatform())
                return std::unexpected(PackageProblem{Kind::MissingProgram, run.path, {}});
            forOnePlatform = true;
        }
    if (total > kMaxPackageBytes && !forOnePlatform)
        return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}});
    if (auto problem = mediaProblem(*manifest, files))
        return std::unexpected(std::move(*problem));
    if (auto problem = qmlProblem(*manifest, files))
        return std::unexpected(std::move(*problem));
    for (auto problem : {contentProblem(manifest->styles, checks.styleCatalogue, files),
                         contentProblem(manifest->karaokePresets, checks.karaokePreset, files),
                         dictionaryProblem(manifest->dictionaries, checks, files),
                         contentProblem(manifest->graphs, checks.graph, files),
                         contentProblem(manifest->layouts, checks.layout, files),
                         themeProblem(manifest->themes, files), interfaceIconsProblem(*manifest, checks.iconRoles, files)})
        if (problem)
            return std::unexpected(std::move(*problem));
    return Package{std::move(*manifest), std::move(files)};
}

}
