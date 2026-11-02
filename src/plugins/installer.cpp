#include "hikari/plugins/installer.h"

#include <fstream>
#include <system_error>

namespace hikari::plugins {

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path &path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path fromUtf8(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::unexpected<std::string> failed(std::string_view what, const fs::path &path, const std::error_code &error = {})
{
    std::string message = std::string(what) + " " + utf8(path);
    if (error)
        message += ": " + error.message();
    return std::unexpected(std::move(message));
}

bool needsRetrust(const std::optional<minisign::PublicKey> &signer, const std::optional<minisign::PublicKey> &pinned)
{
    return pinned && signer != pinned;
}

Change changeFrom(const std::optional<InstalledPlugin> &installed, const std::string &version)
{
    if (!installed)
        return Change::Install;
    const int order = compareVersions(version, installed->version);
    return order > 0 ? Change::Update : order == 0 ? Change::Reinstall : Change::Downgrade;
}

std::expected<void, std::string> writeFiles(const fs::path &root, const std::vector<PackageFile> &files)
{
    std::error_code error;
    for (const PackageFile &file : files) {
        const fs::path target = root / fromUtf8(file.path);
        fs::create_directories(target.parent_path(), error);
        if (error)
            return failed("cannot create", target.parent_path(), error);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(file.data.data()), std::streamsize(file.data.size()));
        if (!out.flush())
            return failed("cannot write", target);
    }
    return {};
}

std::expected<void, std::string> swapIn(const fs::path &staging, const fs::path &target, const fs::path &old)
{
    std::error_code error;
    fs::remove_all(old, error);
    const bool replacing = fs::exists(target, error);
    if (replacing) {
        fs::rename(target, old, error);
        if (error)
            return failed("cannot move aside", target, error);
    }
    fs::rename(staging, target, error);
    if (error) {
        std::error_code ignored;
        if (replacing)
            fs::rename(old, target, ignored);
        return failed("cannot move into place", target, error);
    }
    fs::remove_all(old, error);
    return {};
}

}

Installer::Installer(PluginFolders folders, std::int64_t apiVersion)
    : m_folders(std::move(folders)), m_apiVersion(apiVersion)
{
}

std::expected<Registry, std::string> Installer::registry() const
{
    return loadRegistry(m_folders.registry);
}

std::expected<InstallReview, InstallRefusal> Installer::review(std::span<const std::byte> packageBytes,
                                                               std::vector<PackageFile> files,
                                                               std::optional<std::string_view> signature) const
{
    using Kind = InstallRefusal::Kind;
    auto package = openPackage(std::move(files), m_apiVersion);
    if (!package)
        return std::unexpected(InstallRefusal{Kind::Package, package.error(), {}, {}});
    auto registry = loadRegistry(m_folders.registry);
    if (!registry)
        return std::unexpected(InstallRefusal{Kind::Registry, {}, {}, registry.error()});

    InstallReview review{std::move(*package), Change::Install, {}, std::nullopt, std::nullopt, false};
    const Manifest &manifest = review.package.manifest;
    std::optional<InstalledPlugin> installed;
    if (const auto it = registry->find(manifest.id); it != registry->end()) {
        installed = it->second;
        review.installedVersion = installed->version;
        review.pinned = installed->key;
    }
    review.change = changeFrom(installed, manifest.version);

    if (signature) {
        std::optional<minisign::Refusal> refusal;
        for (const auto &candidate : {review.pinned, manifest.publicKey}) {
            if (!candidate)
                continue;
            const auto verified = minisign::verify(packageBytes, *signature, std::span(&*candidate, 1));
            if (verified) {
                review.signer = verified->key;
                break;
            }
            refusal = verified.error();
            if (refusal != minisign::Refusal::UntrustedKey)
                break;
        }
        if (!review.signer) {
            if (!refusal)
                return std::unexpected(InstallRefusal{Kind::SignedWithoutKey, {}, {}, {}});
            return std::unexpected(InstallRefusal{Kind::Signature, {}, *refusal, {}});
        }
    }
    review.needsRetrust = needsRetrust(review.signer, review.pinned);
    return review;
}

std::expected<void, std::string> Installer::install(const InstallReview &review, bool retrusted)
{
    auto registry = loadRegistry(m_folders.registry);
    if (!registry)
        return std::unexpected(registry.error());
    const Manifest &manifest = review.package.manifest;
    const auto existing = registry->find(manifest.id);
    const std::optional<minisign::PublicKey> pinned =
        existing == registry->end() ? std::nullopt : existing->second.key;
    if (needsRetrust(review.signer, pinned) && !retrusted)
        return std::unexpected("the signing key changed; trust the new key to install");

    std::error_code error;
    fs::create_directories(m_folders.plugins, error);
    if (error)
        return failed("cannot create", m_folders.plugins, error);
    const fs::path staging = m_folders.plugins / (".staging-" + manifest.id);
    fs::remove_all(staging, error);
    if (auto written = writeFiles(staging, review.package.files); !written) {
        fs::remove_all(staging, error);
        return written;
    }
    if (auto swapped = swapIn(staging, m_folders.plugins / manifest.id, m_folders.plugins / (".old-" + manifest.id));
        !swapped) {
        fs::remove_all(staging, error);
        return swapped;
    }
    fs::remove_all(m_folders.cache / manifest.id, error);
    fs::create_directories(m_folders.data / manifest.id, error);
    if (error)
        return failed("cannot create", m_folders.data / manifest.id, error);

    InstalledPlugin entry{manifest.version, true, review.signer, {}};
    if (existing != registry->end()) {
        entry.enabled = existing->second.enabled;
        for (const FileGrantRequest &request : manifest.permissions.files)
            if (const auto grant = existing->second.grants.find(request.id); grant != existing->second.grants.end())
                entry.grants.insert(*grant);
    }
    (*registry)[manifest.id] = std::move(entry);
    if (!saveRegistry(m_folders.registry, *registry))
        return failed("cannot write", m_folders.registry);
    return {};
}

std::expected<void, std::string> Installer::setEnabled(std::string_view id, bool enabled)
{
    auto registry = loadRegistry(m_folders.registry);
    if (!registry)
        return std::unexpected(registry.error());
    const auto it = registry->find(std::string(id));
    if (it == registry->end())
        return std::unexpected("not installed: " + std::string(id));
    it->second.enabled = enabled;
    if (!saveRegistry(m_folders.registry, *registry))
        return failed("cannot write", m_folders.registry);
    return {};
}

std::expected<void, std::string> Installer::uninstall(std::string_view id, bool removeData)
{
    auto registry = loadRegistry(m_folders.registry);
    if (!registry)
        return std::unexpected(registry.error());
    const auto it = registry->find(std::string(id));
    if (it == registry->end())
        return std::unexpected("not installed: " + std::string(id));
    std::error_code error;
    const fs::path folder = m_folders.plugins / fromUtf8(id);
    fs::remove_all(folder, error);
    if (error)
        return failed("cannot remove", folder, error);
    fs::remove_all(m_folders.cache / fromUtf8(id), error);
    if (removeData) {
        fs::remove_all(m_folders.data / fromUtf8(id), error);
        if (error)
            return failed("cannot remove", m_folders.data / fromUtf8(id), error);
    }
    registry->erase(it);
    if (!saveRegistry(m_folders.registry, *registry))
        return failed("cannot write", m_folders.registry);
    return {};
}

}
