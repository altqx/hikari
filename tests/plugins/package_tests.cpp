#include "hikari/plugins/package.h"

#include "test_signer.h"

#include <gtest/gtest.h>

using namespace hikari::plugins;
using hikari::plugins::test::bytes;
using Kind = PackageProblem::Kind;

namespace {

constexpr std::string_view kManifest = R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0",
    "api": 1, "main": "lua/main.lua", "authors": ["Ana"]})";

std::vector<PackageFile> plugin(std::vector<PackageFile> extra = {})
{
    std::vector<PackageFile> files{{"hikari-plugin.json", bytes(kManifest)}, {"lua/main.lua", bytes("return {}")}};
    files.insert(files.end(), extra.begin(), extra.end());
    return files;
}

PackageProblem problem(std::vector<PackageFile> files)
{
    const auto result = openPackage(std::move(files), 1);
    EXPECT_FALSE(result);
    return result ? PackageProblem{} : result.error();
}

TEST(PluginPackage, AWellFormedPackageOpens)
{
    const auto package = openPackage(plugin({{"README.md", bytes("hi")}, {"lua/lib/util.lua", {}}}), 1);
    ASSERT_TRUE(package);
    EXPECT_EQ(package->manifest.main, "lua/main.lua");
    EXPECT_EQ(package->files.size(), 4u);
}

TEST(PluginPackage, PathsThatEscapeOrCannotExistAreRefused)
{
    for (const char *path : {"../evil.lua", "/etc/passwd", "lua/../../x", "a\\b.lua", "C:x", "lua//x", "lua/./x",
                             "CON", "nul.txt", "lpt1.lua", "trailing.", "trailing ", "lua/", "", "a\nb"}) {
        const PackageProblem p = problem(plugin({{path, bytes("x")}}));
        EXPECT_EQ(p.kind, Kind::BadPath) << path;
        EXPECT_EQ(p.path, path);
    }
}

TEST(PluginPackage, PathsThatCollideAreRefused)
{
    EXPECT_EQ(problem(plugin({{"LUA/MAIN.lua", {}}})).kind, Kind::BadPath);
    EXPECT_EQ(problem(plugin({{"lua", {}}})).path, "lua");
}

TEST(PluginPackage, AMissingManifestOrMainIsRefused)
{
    EXPECT_EQ(problem({{"lua/main.lua", {}}}).kind, Kind::NoManifest);
    EXPECT_EQ(problem({{"sub/hikari-plugin.json", bytes(kManifest)}, {"lua/main.lua", {}}}).kind, Kind::NoManifest);
    const PackageProblem missing = problem({{"hikari-plugin.json", bytes(kManifest)}});
    EXPECT_EQ(missing.kind, Kind::MissingMain);
    EXPECT_EQ(missing.path, "lua/main.lua");
}

TEST(PluginPackage, AManifestProblemCarriesThrough)
{
    const PackageProblem p = problem({{"hikari-plugin.json", bytes("[]")}, {"lua/main.lua", {}}});
    EXPECT_EQ(p.kind, Kind::Manifest);
    EXPECT_EQ(p.manifest.kind, ManifestProblem::Kind::Schema);
}

TEST(PluginPackage, OversizePackagesAreRefused)
{
    std::vector<PackageFile> many = plugin();
    for (std::size_t i = 0; i < kMaxPackageFiles; ++i)
        many.push_back({"f" + std::to_string(i), {}});
    EXPECT_EQ(problem(std::move(many)).kind, Kind::TooLarge);
    EXPECT_EQ(problem(plugin({{"big.bin", std::vector<std::byte>(kMaxPackageBytes)}})).kind, Kind::TooLarge);
}

}
