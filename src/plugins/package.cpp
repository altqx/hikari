#include "hikari/plugins/package.h"

#include "media.h"
#include "hikari/api/file_names.h"

#include "hikari/plugins/archive.h"
#include "hikari/plugins/qml_imports.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

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
                         contentProblem(manifest->layouts, checks.layout, files)})
        if (problem)
            return std::unexpected(std::move(*problem));
    return Package{std::move(*manifest), std::move(files)};
}

std::expected<std::vector<PackageFile>, PackageProblem> readPluginFolder(const std::filesystem::path &folder)
{
    namespace fs = std::filesystem;
    using Kind = PackageProblem::Kind;
    std::vector<PackageFile> files;
    std::size_t total = 0;
    std::error_code error;
    fs::recursive_directory_iterator it(folder, fs::directory_options::none, error);
    if (error)
        return std::unexpected(PackageProblem{Kind::NoManifest, std::string(kManifestName), {}});
    for (const fs::recursive_directory_iterator end; it != end; it.increment(error)) {
        if (error)
            return std::unexpected(PackageProblem{Kind::BadPath, {}, {}});
        const fs::path relative = it->path().lexically_relative(folder);
        const std::u8string generic = relative.generic_u8string();
        const std::string path(generic.begin(), generic.end());
        if (it->path().filename().u8string().starts_with(u8".")) {
            if (it->is_directory(error))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(error))
            return std::unexpected(PackageProblem{Kind::BadPath, path, {}});
        if (!it->is_regular_file(error))
            continue;
        if (files.size() == kMaxPackageFiles)
            return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}});
        total += std::size_t(it->file_size(error));
        if (total > kMaxPlatformPackageBytes)
            return std::unexpected(PackageProblem{Kind::TooLarge, {}, {}, {}, kMaxPlatformPackageBytes});
        std::ifstream in(it->path(), std::ios::binary);
        std::vector<char> data{std::istreambuf_iterator<char>(in), {}};
        if (in.bad())
            return std::unexpected(PackageProblem{Kind::BadPath, path, {}});
        PackageFile file{path, {}};
        file.data.resize(data.size());
        std::ranges::transform(data, file.data.begin(), [](char c) { return std::byte(c); });
        files.push_back(std::move(file));
    }
    return files;
}

}
