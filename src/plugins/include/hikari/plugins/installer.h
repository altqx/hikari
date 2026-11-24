#pragma once

#include "hikari/plugins/marketplace.h"
#include "hikari/plugins/package.h"
#include "hikari/plugins/registry.h"

#include <filesystem>
#include <functional>
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
    // How each content pack file is read at review; unset, a file is only checked to be there.
    void checkContentWith(ContentFileChecks checks) { m_contentChecks = std::move(checks); }

    const PluginFolders &folders() const { return m_folders; }
    // The plugins as they run, by id: a dev plugin stands in for an installed plugin of the same id.
    std::expected<Registry, std::string> registry() const;
    // The installed plugins a dev plugin of the same id stands in for, by id.
    std::expected<Registry, std::string> overridden() const;

    // Where a plugin's files, data and cache are. A dev plugin's are its own folder and plugin-data/.dev/<id>,
    // <cache>/plugins/.dev/<id>.
    std::filesystem::path codeFolder(std::string_view id) const;
    std::filesystem::path dataFolder(std::string_view id) const;
    std::filesystem::path cacheFolder(std::string_view id) const;
    // Where a plugin's secrets go in the system keychain, before the setting id: "plugin/<id>/", or "plugin-dev/<id>/"
    // for a dev plugin.
    std::string secretPrefix(std::string_view id) const;

    // What installing would do; writes nothing. `packageBytes` is the .hikariplugin file as
    // signed, `files` its unpacked entries, `signature` the text of the .minisig beside it.
    // A signature is checked against the pinned key, else the manifest's publicKey; one that
    // does not verify is refused outright.
    std::expected<InstallReview, InstallRefusal> review(std::span<const std::byte> packageBytes,
                                                        std::vector<PackageFile> files,
                                                        std::optional<std::string_view> signature) const;
    // review() for a marketplace download, whose bytes already passed listedPackageProblem: the
    // marketplace's signed index vouches for the listing's author key, so no .minisig is needed.
    // Refuses a package that isn't the listed one, and a downgrade unless `allowDowngrade`.
    std::expected<InstallReview, InstallRefusal> reviewListed(std::vector<PackageFile> files, const Listing &listing,
                                                              const ListedVersion &listed, bool allowDowngrade = false) const;

    // A folder run in place as a dev plugin, under the package rules; writes nothing. Reinstall when that id's dev
    // plugin is loaded already.
    std::expected<InstallReview, InstallRefusal> reviewDev(const std::filesystem::path &folder) const;
    // Loads the reviewed folder as a dev plugin without copying it. A reload keeps its enabled state, settings,
    // grants, data and cache; the manifest reviewed becomes the one the user said yes to.
    std::expected<void, std::string> installDev(const InstallReview &review, const std::filesystem::path &folder);
    // The manifest the user last said yes to for a dev plugin.
    std::expected<Manifest, std::string> acceptedManifest(std::string_view id) const;

    // Stages the files and swaps them in; pins the signer, keeps the enabled state and the
    // grants the manifest still asks for, keeps plugin data and clears the cache. Another
    // version's copy is kept in plugins/.previous/<id> for rollBack(). Installing over a
    // revoked copy turns the plugin back on. A review that needs re-trust is refused unless `retrusted`.
    std::expected<void, std::string> install(const InstallReview &review, bool retrusted);
    // Swaps the installed copy with the one kept by the last install; `skipLeft` makes automatic
    // updates pass over the version left, otherwise the skip is cleared. Returns the version left.
    std::expected<std::string, std::string> rollBack(std::string_view id, bool skipLeft);
    // Changes the plugins.json entry only.
    std::expected<void, std::string> edit(std::string_view id, const std::function<void(InstalledPlugin &)> &change);
    std::expected<void, std::string> setEnabled(std::string_view id, bool enabled);
    // The manifest of the installed copy.
    std::expected<Manifest, std::string> installedManifest(std::string_view id) const;
    // The manifest of the copy kept for rollBack().
    std::expected<Manifest, std::string> previousManifest(std::string_view id) const;
    // Checks the value against the installed manifest, then keeps it in plugins.json.
    std::expected<void, std::string> setSetting(std::string_view id, std::string_view setting, const api::Json &value);
    // Remembers the folder for one of the manifest's file requests in plugins.json; nullopt forgets it.
    std::expected<void, std::string> setGrant(std::string_view id, std::string_view request, std::optional<FileGrant> grant);
    // A dev plugin is only unloaded: its folder stays, and an installed plugin of the same id runs again.
    std::expected<void, std::string> uninstall(std::string_view id, bool removeData);

private:
    std::expected<InstallReview, InstallRefusal> open(std::vector<PackageFile> files) const;
    std::filesystem::path previousFolder(std::string_view id) const;
    std::expected<Manifest, std::string> manifestIn(const std::filesystem::path &folder, std::string_view id) const;
    std::string keyOf(std::string_view id) const;
    std::expected<void, std::string> change(std::string_view id, const std::function<void(InstalledPlugin &)> &change);

    PluginFolders m_folders;
    std::int64_t m_apiVersion;
    ContentFileChecks m_contentChecks;
};

}
