#pragma once

#include "hikari/plugins/marketplace.h"
#include "hikari/plugins/package.h"
#include "hikari/plugins/registry.h"

#include <filesystem>
#include <span>

namespace hikari::plugins {

struct PluginFolders {
    std::filesystem::path plugins;  // <user folder>/plugins, one folder per id
    std::filesystem::path data;     // <user folder>/plugin-data, kept across updates
    std::filesystem::path cache;    // <cache>/plugins, cleared on every install
    std::filesystem::path registry; // <user folder>/plugins.json
};

enum class Change { Install, Update, Reinstall, Downgrade };

struct InstallRefusal {
    enum class Kind { Package, Signature, SignedWithoutKey, Registry, Listing };
    Kind kind = Kind::Package;
    PackageProblem package;
    minisign::Refusal signature = minisign::Refusal::Malformed;
    std::string detail;
};

struct InstallReview {
    Package package;
    Change change = Change::Install;
    std::string installedVersion;
    std::optional<minisign::PublicKey> signer;
    std::optional<minisign::PublicKey> pinned;
    // Signed by another key than the pinned one, or unsigned over a signed install.
    bool needsRetrust = false;
    Source source = Source::File;
};

class Installer {
public:
    Installer(PluginFolders folders, std::int64_t apiVersion);

    const PluginFolders &folders() const { return m_folders; }
    std::expected<Registry, std::string> registry() const;

    // What installing would do; writes nothing. `packageBytes` is the .hikariplugin file as
    // signed, `files` its unpacked entries, `signature` the text of the .minisig beside it.
    // A signature is checked against the pinned key, else the manifest's publicKey; one that
    // does not verify is refused outright.
    std::expected<InstallReview, InstallRefusal> review(std::span<const std::byte> packageBytes,
                                                        std::vector<PackageFile> files,
                                                        std::optional<std::string_view> signature) const;
    // review() for a marketplace download, whose bytes already passed listedPackageProblem: the
    // marketplace's signed index vouches for the listing's author key, so no .minisig is needed.
    // Refuses a package that isn't the listed one and any downgrade.
    std::expected<InstallReview, InstallRefusal> reviewListed(std::vector<PackageFile> files, const Listing &listing,
                                                              const ListedVersion &listed) const;

    // Stages the files and swaps them in; pins the signer, keeps the enabled state and the
    // grants the manifest still asks for, keeps plugin data and clears the cache. A review
    // that needs re-trust is refused unless `retrusted`.
    std::expected<void, std::string> install(const InstallReview &review, bool retrusted);
    std::expected<void, std::string> setEnabled(std::string_view id, bool enabled);
    // The manifest of the installed copy.
    std::expected<Manifest, std::string> installedManifest(std::string_view id) const;
    // Checks the value against the installed manifest, then keeps it in plugins.json.
    std::expected<void, std::string> setSetting(std::string_view id, std::string_view setting, const api::Json &value);
    // Remembers the folder for one of the manifest's file requests in plugins.json; nullopt forgets it.
    std::expected<void, std::string> setGrant(std::string_view id, std::string_view request, std::optional<FileGrant> grant);
    std::expected<void, std::string> uninstall(std::string_view id, bool removeData);

private:
    std::expected<InstallReview, InstallRefusal> open(std::vector<PackageFile> files) const;

    PluginFolders m_folders;
    std::int64_t m_apiVersion;
};

}
