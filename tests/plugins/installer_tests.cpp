#include "hikari/plugins/installer.h"

#include "test_signer.h"

#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <sstream>

using namespace hikari::plugins;
using namespace hikari::plugins::test;
namespace fs = std::filesystem;
using Refusal = InstallRefusal::Kind;

namespace {

std::string manifest(std::string_view version, const Signer *author = nullptr, std::string_view extra = "")
{
    std::string text = R"({"id": "com.example.hello", "name": "Hello", "api": 1, "main": "main.lua", "authors": ["Ana"], "version": ")" +
                       std::string(version) + "\"";
    if (author)
        text += R"(, "publicKey": ")" + author->key.text() + "\"";
    return text + std::string(extra) + "}";
}

std::vector<PackageFile> plugin(std::string_view version, const Signer *author = nullptr, std::string_view extra = "")
{
    return {{"hikari-plugin.json", bytes(manifest(version, author, extra))},
            {"main.lua", bytes("return '" + std::string(version) + "'")},
            {"v" + std::string(version) + ".txt", {}}};
}

std::string read(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void touch(const fs::path &file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

class PluginInstaller : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(fs::create_directories(m_root)); }
    void TearDown() override { fs::remove_all(m_root); }

    PluginFolders folders() const
    {
        return {m_root / "user/plugins", m_root / "user/plugin-data", m_root / "cache/plugins", m_root / "user/plugins.json"};
    }

    std::expected<InstallReview, InstallRefusal> review(std::vector<PackageFile> files, const Signer *signedBy = nullptr)
    {
        const std::vector<std::byte> package = packageBytes(files);
        std::optional<std::string> signature;
        if (signedBy)
            signature = sign(*signedBy, package);
        return installer.review(package, std::move(files), signature);
    }

    InstallReview reviewed(std::vector<PackageFile> files, const Signer *signedBy = nullptr)
    {
        auto result = review(std::move(files), signedBy);
        EXPECT_TRUE(result);
        return result ? std::move(*result) : InstallReview{};
    }

    InstalledPlugin installed()
    {
        const auto registry = installer.registry();
        EXPECT_TRUE(registry && registry->contains("com.example.hello"));
        return registry && registry->contains("com.example.hello") ? registry->at("com.example.hello") : InstalledPlugin{};
    }

    fs::path folder() const { return folders().plugins / "com.example.hello"; }

    fs::path m_root = fs::temp_directory_path() / ("hikari-plugins-" + std::to_string(std::random_device{}()));
    Installer installer{folders(), 1};
    Signer a = signer(1);
    Signer b = signer(2);
};

TEST_F(PluginInstaller, AnUnsignedPackageInstallsUnverified)
{
    const InstallReview r = reviewed(plugin("1.0.0"));
    EXPECT_EQ(r.change, Change::Install);
    EXPECT_FALSE(r.signer);
    EXPECT_FALSE(r.needsRetrust);
    ASSERT_TRUE(installer.install(r, false));

    EXPECT_EQ(read(folder() / "main.lua"), "return '1.0.0'");
    EXPECT_TRUE(fs::is_directory(folders().data / "com.example.hello"));
    EXPECT_FALSE(fs::exists(folders().plugins / ".staging-com.example.hello"));
    const InstalledPlugin entry = installed();
    EXPECT_EQ(entry.version, "1.0.0");
    EXPECT_TRUE(entry.enabled);
    EXPECT_FALSE(entry.key);
}

TEST_F(PluginInstaller, ASignedPackagePinsItsKey)
{
    const InstallReview r = reviewed(plugin("1.0.0", &a), &a);
    ASSERT_TRUE(r.signer);
    EXPECT_EQ(*r.signer, a.key);
    ASSERT_TRUE(installer.install(r, false));
    EXPECT_EQ(installed().key, a.key);
}

TEST_F(PluginInstaller, AnUpdateKeepsDataAndSettingsAndClearsTheCache)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    touch(folders().data / "com.example.hello/notes.txt");
    touch(folders().cache / "com.example.hello/index.bin");
    ASSERT_TRUE(installer.setEnabled("com.example.hello", false));

    const InstallReview r = reviewed(plugin("1.1.0", &a), &a);
    EXPECT_EQ(r.change, Change::Update);
    EXPECT_EQ(r.installedVersion, "1.0.0");
    EXPECT_FALSE(r.needsRetrust);
    ASSERT_TRUE(installer.install(r, false));

    EXPECT_EQ(read(folder() / "main.lua"), "return '1.1.0'");
    EXPECT_FALSE(fs::exists(folder() / "v1.0.0.txt"));
    EXPECT_TRUE(fs::exists(folders().data / "com.example.hello/notes.txt"));
    EXPECT_FALSE(fs::exists(folders().cache / "com.example.hello"));
    EXPECT_FALSE(fs::exists(folders().plugins / ".old-com.example.hello"));
    const InstalledPlugin entry = installed();
    EXPECT_EQ(entry.version, "1.1.0");
    EXPECT_FALSE(entry.enabled);
    EXPECT_EQ(entry.key, a.key);
}

TEST_F(PluginInstaller, ReinstallAndDowngradeAreNamed)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0")), false));
    EXPECT_EQ(reviewed(plugin("1.1.0")).change, Change::Reinstall);
    EXPECT_EQ(reviewed(plugin("1.0.0")).change, Change::Downgrade);
}

TEST_F(PluginInstaller, AKeyChangeOnUpdateIsRefusedUntilRetrusted)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    const InstallReview r = reviewed(plugin("2.0.0", &b), &b);
    EXPECT_TRUE(r.needsRetrust);
    EXPECT_EQ(r.pinned, a.key);
    EXPECT_EQ(r.signer, b.key);

    const auto refused = installer.install(r, false);
    ASSERT_FALSE(refused);
    EXPECT_EQ(read(folder() / "main.lua"), "return '1.0.0'");
    EXPECT_EQ(installed().key, a.key);

    ASSERT_TRUE(installer.install(r, true));
    EXPECT_EQ(installed().key, b.key);
    EXPECT_EQ(read(folder() / "main.lua"), "return '2.0.0'");
}

TEST_F(PluginInstaller, AnUpdateSignedByThePinnedKeyNeedsNoRetrustWhateverTheManifestSays)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    const InstallReview r = reviewed(plugin("1.1.0", &b), &a);
    EXPECT_FALSE(r.needsRetrust);
    EXPECT_EQ(r.signer, a.key);
}

TEST_F(PluginInstaller, AnUnsignedUpdateOverASignedInstallNeedsRetrust)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    const InstallReview r = reviewed(plugin("1.1.0"));
    EXPECT_TRUE(r.needsRetrust);
    EXPECT_FALSE(installer.install(r, false));
    ASSERT_TRUE(installer.install(r, true));
    EXPECT_FALSE(installed().key);
}

TEST_F(PluginInstaller, ATamperedPackageIsRefused)
{
    std::vector<PackageFile> files = plugin("1.0.0", &a);
    const std::string signature = sign(a, packageBytes(files));
    files[1].data = bytes("return 'evil'");
    const auto result = installer.review(packageBytes(files), files, signature);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Refusal::Signature);
    EXPECT_EQ(result.error().signature, hikari::minisign::Refusal::BadSignature);
}

TEST_F(PluginInstaller, ASignatureByAnotherKeyThanTheManifestNamesIsRefused)
{
    const auto result = review(plugin("1.0.0", &a), &b);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Refusal::Signature);
    EXPECT_EQ(result.error().signature, hikari::minisign::Refusal::UntrustedKey);
}

TEST_F(PluginInstaller, ASignatureWithNoKeyToCheckItIsRefused)
{
    const auto result = review(plugin("1.0.0"), &a);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Refusal::SignedWithoutKey);
}

TEST_F(PluginInstaller, ABadManifestIsRefusedBeforeAnythingIsWritten)
{
    std::vector<PackageFile> files = plugin("1.0.0");
    files[0].data = bytes(R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0", "api": 99,
        "main": "main.lua", "authors": ["Ana"]})");
    const auto result = review(files);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Refusal::Package);
    EXPECT_EQ(result.error().package.manifest.kind, hikari::plugins::ManifestProblem::Kind::NeedsNewerApi);
    EXPECT_EQ(result.error().package.manifest.neededApi, 99);
    EXPECT_FALSE(fs::exists(folders().plugins));
}

TEST_F(PluginInstaller, UninstallAsksWhetherDataGoes)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0")), false));
    touch(folders().data / "com.example.hello/notes.txt");
    touch(folders().cache / "com.example.hello/index.bin");
    ASSERT_TRUE(installer.uninstall("com.example.hello", false));
    EXPECT_FALSE(fs::exists(folder()));
    EXPECT_FALSE(fs::exists(folders().cache / "com.example.hello"));
    EXPECT_TRUE(fs::exists(folders().data / "com.example.hello/notes.txt"));
    EXPECT_FALSE(installer.registry()->contains("com.example.hello"));

    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0")), false));
    ASSERT_TRUE(installer.uninstall("com.example.hello", true));
    EXPECT_FALSE(fs::exists(folders().data / "com.example.hello"));
    EXPECT_FALSE(installer.uninstall("com.example.hello", true));
}

TEST_F(PluginInstaller, GrantsTheManifestStillAsksForSurviveAnUpdate)
{
    const std::string_view asks =
        R"(, "permissions": {"files": [{"id": "fonts", "title": "Fonts", "reason": "r"}, {"id": "out", "title": "Out", "reason": "r"}]})";
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", nullptr, asks)), false));
    Registry registry = *installer.registry();
    registry["com.example.hello"].grants = {{"fonts", {"/fonts", false}}, {"out", {"/out", true}}};
    ASSERT_TRUE(saveRegistry(folders().registry, registry));

    const std::string_view asksLess = R"(, "permissions": {"files": [{"id": "out", "title": "Out", "reason": "r"}]})";
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", nullptr, asksLess)), false));
    const InstalledPlugin entry = installed();
    ASSERT_EQ(entry.grants.size(), 1u);
    EXPECT_EQ(entry.grants.at("out").path, "/out");
    EXPECT_FALSE(entry.grants.at("out").writable);
}

TEST_F(PluginInstaller, SetGrantRemembersAndForgetsOnlyFoldersTheManifestAsksFor)
{
    const std::string_view asks = R"(, "permissions": {"files": [{"id": "fonts", "title": "Fonts", "reason": "r"},)"
                                  R"( {"id": "out", "title": "Out", "writable": true, "reason": "r"}]})";
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", nullptr, asks)), false));
    ASSERT_TRUE(installer.setGrant("com.example.hello", "fonts", FileGrant{"/fonts", true}));
    ASSERT_TRUE(installer.setGrant("com.example.hello", "out", FileGrant{"/out", true}));
    EXPECT_FALSE(installed().grants.at("fonts").writable);
    EXPECT_TRUE(installed().grants.at("out").writable);
    EXPECT_EQ(installer.setGrant("com.example.hello", "home", FileGrant{"/home", false}).error(),
              "\xE2\x80\x9Chome\xE2\x80\x9D isn't a folder com.example.hello asks for");
    EXPECT_FALSE(installer.setGrant("com.example.other", "fonts", FileGrant{"/fonts", false}));
    ASSERT_TRUE(installer.setGrant("com.example.hello", "fonts", std::nullopt));
    EXPECT_EQ(installed().grants.size(), 1u);
}

constexpr std::string_view kSettings =
    R"(, "contributes": {"settings": [{"id": "count", "title": "Count", "type": "integer", "default": 3, "minimum": 1, "maximum": 10},)"
    R"( {"id": "mode", "title": "Mode", "type": "choice", "options": ["fast", "slow"], "default": "fast"},)"
    R"( {"id": "loud", "title": "Loud", "type": "boolean", "default": false}]})";

TEST_F(PluginInstaller, SetSettingChecksTheValueAgainstTheInstalledManifest)
{
    using hikari::api::Json;
    EXPECT_EQ(installer.setSetting("com.example.hello", "count", Json(4)).error(),
              "cannot read hikari-plugin.json of com.example.hello");
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", nullptr, kSettings)), false));
    ASSERT_TRUE(installer.setSetting("com.example.hello", "count", Json(7)));
    ASSERT_TRUE(installer.setSetting("com.example.hello", "mode", Json("slow")));
    EXPECT_EQ(installer.setSetting("com.example.hello", "count", Json("7")).error(), "“count” takes a whole number");
    EXPECT_EQ(installer.setSetting("com.example.hello", "count", Json(0)).error(), "“count” must be at least 1");
    EXPECT_EQ(installer.setSetting("com.example.hello", "count", Json(11)).error(), "“count” must be at most 10");
    EXPECT_EQ(installer.setSetting("com.example.hello", "mode", Json("medium")).error(), "“mode” must be one of: fast, slow");
    EXPECT_EQ(installer.setSetting("com.example.hello", "loud", Json(1)).error(), "“loud” takes true or false");
    EXPECT_EQ(installer.setSetting("com.example.hello", "other", Json(1)).error(),
              "“other” isn't a setting of com.example.hello");
    EXPECT_EQ(installed().settings, Json::object({{"count", 7}, {"mode", "slow"}}));
    const auto manifest = installer.installedManifest("com.example.hello");
    ASSERT_TRUE(manifest);
    EXPECT_EQ(settingValues(*manifest, installed().settings),
              Json::object({{"count", 7}, {"mode", "slow"}, {"loud", false}}));
}

TEST_F(PluginInstaller, SettingsSurviveAnUpdateWhileTheyStillFit)
{
    using hikari::api::Json;
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", nullptr, kSettings)), false));
    ASSERT_TRUE(installer.setSetting("com.example.hello", "count", Json(7)));
    ASSERT_TRUE(installer.setSetting("com.example.hello", "mode", Json("slow")));
    ASSERT_TRUE(installer.setSetting("com.example.hello", "loud", Json(true)));
    const std::string_view narrower =
        R"(, "contributes": {"settings": [{"id": "count", "title": "Count", "type": "integer", "default": 3, "maximum": 5},)"
        R"( {"id": "mode", "title": "Mode", "type": "choice", "options": ["fast", "slow"], "default": "fast"}]})";
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", nullptr, narrower)), false));
    EXPECT_EQ(installed().settings, Json::object({{"mode", "slow"}}));
    EXPECT_EQ(settingValues(*installer.installedManifest("com.example.hello"), installed().settings),
              Json::object({{"count", 3}, {"mode", "slow"}}));
}

TEST_F(PluginInstaller, AnUnreadableRegistryStopsEverything)
{
    touch(folders().registry);
    const auto result = review(plugin("1.0.0"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Refusal::Registry);
    EXPECT_FALSE(installer.setEnabled("com.example.hello", true));
}

TEST_F(PluginInstaller, TheRegistryRoundTripsAndRefusesIdsThatAreNotIds)
{
    Registry registry;
    registry["com.example.hello"] = {"1.0.0", false, a.key, {{"fonts", {"/f", true}}}};
    fs::create_directories(folders().registry.parent_path());
    ASSERT_TRUE(saveRegistry(folders().registry, registry));
    const auto loaded = loadRegistry(folders().registry);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->at("com.example.hello").key, a.key);
    EXPECT_FALSE(loaded->at("com.example.hello").enabled);
    EXPECT_TRUE(loaded->at("com.example.hello").grants.at("fonts").writable);
    EXPECT_EQ(loaded->at("com.example.hello").settings, hikari::api::Json::object());

    registry["com.example.hello"].settings = hikari::api::Json::object({{"count", 2}});
    ASSERT_TRUE(saveRegistry(folders().registry, registry));
    EXPECT_EQ(loadRegistry(folders().registry)->at("com.example.hello").settings, hikari::api::Json::object({{"count", 2}}));

    std::ofstream(folders().registry) << R"({"format": 1, "plugins": {"..": {"version": "1.0.0", "enabled": true}}})";
    EXPECT_FALSE(loadRegistry(folders().registry));
    EXPECT_TRUE(loadRegistry(m_root / "missing.json"));

    std::ofstream(folders().registry) << R"({"format": 1, "plugins": {"com.example.hello": {"version": "1.0.0", "enabled": true, "source": "web"}}})";
    EXPECT_FALSE(loadRegistry(folders().registry));
}

Listing listing(const Signer &author, std::string_view version)
{
    Listing out;
    out.id = "com.example.hello";
    out.publicKey = author.key;
    ListedVersion listed;
    listed.version = version;
    out.versions.push_back(listed);
    return out;
}

TEST_F(PluginInstaller, AMarketplacePackageInstallsUnderItsListedKey)
{
    const Listing l = listing(a, "1.0.0");
    const auto r = installer.reviewListed(plugin("1.0.0", &a), l, l.versions.front());
    ASSERT_TRUE(r) << r.error().detail;
    EXPECT_EQ(r->signer, a.key);
    EXPECT_EQ(r->source, Source::Marketplace);
    EXPECT_FALSE(r->needsRetrust);
    ASSERT_TRUE(installer.install(*r, false));
    EXPECT_EQ(installed().key, a.key);
    EXPECT_EQ(installed().source, Source::Marketplace);

    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    EXPECT_EQ(installed().source, Source::File);
}

TEST_F(PluginInstaller, AMarketplacePackageMustBeTheListedOne)
{
    const auto refusal = [&](std::vector<PackageFile> files, const Listing &l) {
        const auto r = installer.reviewListed(std::move(files), l, l.versions.front());
        EXPECT_FALSE(r);
        return r ? Refusal::Package : r.error().kind;
    };
    EXPECT_EQ(refusal(plugin("1.0.0", &a), listing(b, "1.0.0")), Refusal::Listing);
    EXPECT_EQ(refusal(plugin("1.0.0"), listing(a, "1.0.0")), Refusal::Listing);
    EXPECT_EQ(refusal(plugin("1.0.1", &a), listing(a, "1.0.0")), Refusal::Listing);
    EXPECT_EQ(refusal(plugin("1.0.0", &a, R"(, "tier": "unrestricted")"), listing(a, "1.0.0")), Refusal::Listing);
    EXPECT_FALSE(fs::exists(folder()));
}

TEST_F(PluginInstaller, TheMarketplaceCannotMoveAPinnedKeyOrDowngrade)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    const Listing otherKey = listing(b, "1.2.0");
    const auto moved = installer.reviewListed(plugin("1.2.0", &b), otherKey, otherKey.versions.front());
    ASSERT_FALSE(moved);
    EXPECT_EQ(moved.error().kind, Refusal::Listing);

    const Listing older = listing(a, "1.0.0");
    const auto downgrade = installer.reviewListed(plugin("1.0.0", &a), older, older.versions.front());
    ASSERT_FALSE(downgrade);
    EXPECT_EQ(downgrade.error().kind, Refusal::Listing);
    EXPECT_EQ(installed().version, "1.1.0");
}

TEST_F(PluginInstaller, AnExplicitDowngradeFromTheListingInstalls)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    const Listing older = listing(a, "1.0.0");
    const auto downgrade = installer.reviewListed(plugin("1.0.0", &a), older, older.versions.front(), true);
    ASSERT_TRUE(downgrade) << downgrade.error().detail;
    EXPECT_EQ(downgrade->change, Change::Downgrade);
    ASSERT_TRUE(installer.install(*downgrade, false));
    EXPECT_EQ(installed().version, "1.0.0");
}

TEST_F(PluginInstaller, TheUpdateStateRoundTrips)
{
    Registry registry;
    InstalledPlugin entry{"1.4.0", false, a.key};
    entry.autoUpdate = false;
    entry.skip = "1.5.0";
    entry.previous = "1.3.2";
    entry.held = "1.6.0";
    entry.revoked = Revoked{"crashed", "1.3.2"};
    registry["com.example.hello"] = entry;
    fs::create_directories(folders().registry.parent_path());
    ASSERT_TRUE(saveRegistry(folders().registry, registry));
    const InstalledPlugin loaded = loadRegistry(folders().registry)->at("com.example.hello");
    EXPECT_FALSE(loaded.autoUpdate);
    EXPECT_EQ(loaded.skip, "1.5.0");
    EXPECT_EQ(loaded.previous, "1.3.2");
    EXPECT_EQ(loaded.held, "1.6.0");
    EXPECT_EQ(loaded.revoked, (Revoked{"crashed", "1.3.2"}));

    registry["com.example.hello"] = {"1.4.0"};
    ASSERT_TRUE(saveRegistry(folders().registry, registry));
    EXPECT_EQ(read(folders().registry).find("autoUpdate"), std::string::npos);
    EXPECT_TRUE(loadRegistry(folders().registry)->at("com.example.hello").autoUpdate);

    for (const char *bad : {R"("autoUpdate": "no")", R"("skip": 1)", R"("revoked": {"reason": "x"})"}) {
        std::ofstream(folders().registry) << R"({"format": 1, "plugins": {"com.example.hello": {"version": "1.0.0", "enabled": true, )"
                                          << bad << "}}}";
        EXPECT_FALSE(loadRegistry(folders().registry)) << bad;
    }
}

TEST_F(PluginInstaller, AnUpdateKeepsTheVersionItReplacesForRollBack)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    EXPECT_TRUE(installed().previous.empty());
    EXPECT_FALSE(fs::exists(folders().plugins / ".previous"));
    ASSERT_FALSE(installer.rollBack("com.example.hello", true));

    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    EXPECT_EQ(installed().previous, "1.0.0");
    EXPECT_EQ(read(folders().plugins / ".previous/com.example.hello/main.lua"), "return '1.0.0'");

    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    EXPECT_EQ(installed().previous, "1.0.0") << "a reinstall keeps the older copy";
    EXPECT_EQ(read(folders().plugins / ".previous/com.example.hello/main.lua"), "return '1.0.0'");

    ASSERT_TRUE(installer.install(reviewed(plugin("1.2.0", &a), &a), false));
    EXPECT_EQ(installed().previous, "1.1.0");
    EXPECT_EQ(read(folders().plugins / ".previous/com.example.hello/main.lua"), "return '1.1.0'");
}

TEST_F(PluginInstaller, RollBackSwapsOneStepAndSkipsTheVersionLeft)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    touch(folders().data / "com.example.hello/notes.txt");
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    touch(folders().cache / "com.example.hello/index.bin");

    const auto left = installer.rollBack("com.example.hello", true);
    ASSERT_TRUE(left) << left.error();
    EXPECT_EQ(*left, "1.1.0");
    EXPECT_EQ(read(folder() / "main.lua"), "return '1.0.0'");
    EXPECT_EQ(read(folders().plugins / ".previous/com.example.hello/main.lua"), "return '1.1.0'");
    EXPECT_TRUE(fs::exists(folders().data / "com.example.hello/notes.txt"));
    EXPECT_FALSE(fs::exists(folders().cache / "com.example.hello"));
    EXPECT_EQ(installed().version, "1.0.0");
    EXPECT_EQ(installed().previous, "1.1.0");
    EXPECT_EQ(installed().skip, "1.1.0");
    EXPECT_EQ(installed().key, a.key);

    const auto undone = installer.rollBack("com.example.hello", false);
    ASSERT_TRUE(undone) << undone.error();
    EXPECT_EQ(read(folder() / "main.lua"), "return '1.1.0'");
    EXPECT_EQ(installed().version, "1.1.0");
    EXPECT_EQ(installed().previous, "1.0.0");
    EXPECT_TRUE(installed().skip.empty());
}

TEST_F(PluginInstaller, InstallingTheSkippedOrHeldVersionClearsItAndUninstallDropsTheKeptCopy)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.0.0", &a), &a), false));
    ASSERT_TRUE(installer.edit("com.example.hello", [](InstalledPlugin &p) {
        p.skip = "1.1.0";
        p.held = "1.1.0";
        p.autoUpdate = false;
    }));
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    EXPECT_TRUE(installed().skip.empty());
    EXPECT_TRUE(installed().held.empty());
    EXPECT_FALSE(installed().autoUpdate);

    ASSERT_TRUE(installer.uninstall("com.example.hello", false));
    EXPECT_FALSE(fs::exists(folders().plugins / ".previous/com.example.hello"));
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    EXPECT_TRUE(installed().previous.empty());
    EXPECT_TRUE(installed().autoUpdate);
}

TEST_F(PluginInstaller, InstallingOverARevokedCopyTurnsItBackOn)
{
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", &a), &a), false));
    ASSERT_TRUE(installer.edit("com.example.hello", [](InstalledPlugin &p) {
        p.enabled = false;
        p.revoked = Revoked{"crashed", "1.0.0"};
    }));
    const Listing older = listing(a, "1.0.0");
    ASSERT_TRUE(installer.install(*installer.reviewListed(plugin("1.0.0", &a), older, older.versions.front(), true), false));
    EXPECT_TRUE(installed().enabled);
    EXPECT_FALSE(installed().revoked);

    ASSERT_TRUE(installer.setEnabled("com.example.hello", false));
    ASSERT_TRUE(installer.install(reviewed(plugin("1.2.0", &a), &a), false));
    EXPECT_FALSE(installed().enabled) << "a plugin you turned off stays off";
}

}
