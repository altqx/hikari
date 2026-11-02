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

    const std::string_view asksLess = R"(, "permissions": {"files": [{"id": "fonts", "title": "Fonts", "reason": "r"}]})";
    ASSERT_TRUE(installer.install(reviewed(plugin("1.1.0", nullptr, asksLess)), false));
    const InstalledPlugin entry = installed();
    ASSERT_EQ(entry.grants.size(), 1u);
    EXPECT_EQ(entry.grants.at("fonts").path, "/fonts");
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
}

}
