#include "hikari/plugins/marketplace.h"

#include "test_signer.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace hikari::plugins;
using namespace hikari::plugins::test;
namespace fs = std::filesystem;
namespace minisign = hikari::minisign;
using Kind = IndexRefusal::Kind;

namespace {

const Signer kMarket = signer(9);
const Signer kAuthor = signer(7);
const std::vector<std::byte> kPackage = bytes("a .hikariplugin file");

std::string version(std::string_view number, int api, std::string_view extra = "")
{
    return R"({"version": ")" + std::string(number) + R"(", "api": )" + std::to_string(api) +
           R"(, "tier": "restricted", "name": "Hello", "summary": "Says hello", "license": "MIT", "authors": ["Ana"],)"
           R"( "permissions": {}, "url": "https://marketplace.hikarisub.org/p/com.example.hello/)" +
           std::string(number) + R"(.hikariplugin", "sha256": ")" + minisign::sha256Hex(kPackage) + R"(", "size": )" +
           std::to_string(kPackage.size()) +
           R"(, "published": "2026-11-01T10:00:00Z", "readme": "https://example.com/readme.md",)"
           R"( "screenshots": [{"url": "https://example.com/a.png", "caption": "Main", "width": 640, "height": 360}])" +
           std::string(extra) + "}";
}

std::string plugin(std::string_view versions, std::string_view extra = "")
{
    return R"({"id": "com.example.hello", "author": "ana", "authorName": "Ana", "repo": "https://github.com/ana/hello",)"
           R"( "publicKey": ")" +
           kAuthor.key.text() + R"(", "categories": ["Timing"], "versions": [)" + std::string(versions) + "]" +
           std::string(extra) + "}";
}

std::string index(std::int64_t serial, std::string_view plugins, std::string_view format = "1")
{
    return R"({"format": )" + std::string(format) + R"(, "serial": )" + std::to_string(serial) +
           R"(, "generated": "2026-11-02T10:00:00Z", "apiVersions": {"1": "1.0.0", "4": "1.4.0"},)"
           R"( "featured": ["com.example.hello"], "plugins": [)" +
           std::string(plugins) + "]}";
}

std::string comment(std::int64_t serial)
{
    return "file:index.json serial:" + std::to_string(serial) + " generated:2026-11-02T10:00:00Z";
}

std::expected<MarketIndex, IndexRefusal> read(const std::string &json, const std::string &trustedComment,
                                              std::int64_t highestSeen = 0, const Signer &by = kMarket)
{
    const auto data = bytes(json);
    const std::array trusted{kMarket.key};
    return readIndex(data, sign(by, data, trustedComment), trusted, highestSeen);
}

const std::string kIndex = index(5, plugin(version("1.1.0", 4) + "," + version("1.0.0", 1)));

Kind refusal(const std::expected<MarketIndex, IndexRefusal> &result)
{
    EXPECT_FALSE(result);
    return result ? Kind::Malformed : result.error().kind;
}

Manifest manifest(std::string_view id, std::string_view number, const Signer &by)
{
    Manifest m;
    m.id = id;
    m.version = number;
    m.api = 4;
    m.publicKey = by.key;
    return m;
}

}

TEST(Marketplace, PinsTheMarketplaceKeyNotTheReleaseKey)
{
    const auto key = minisign::parsePublicKey(kMarketplaceKey);
    ASSERT_TRUE(key);
    EXPECT_EQ(key->idText(), "EF622DD4D472E463");
}

TEST(Marketplace, ReadsASignedIndex)
{
    const auto result = read(kIndex, comment(5));
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_EQ(result->serial, 5);
    EXPECT_EQ(result->generated, "2026-11-02T10:00:00Z");
    EXPECT_EQ(result->apiVersions.at(4), "1.4.0");
    EXPECT_EQ(result->featured, std::vector<std::string>{"com.example.hello"});
    const Listing *listing = result->find("com.example.hello");
    ASSERT_NE(listing, nullptr);
    EXPECT_EQ(listing->publicKey, kAuthor.key);
    EXPECT_EQ(listing->categories, std::vector<std::string>{"Timing"});
    ASSERT_EQ(listing->versions.size(), 2u);
    const ListedVersion &newest = listing->versions.front();
    EXPECT_EQ(newest.version, "1.1.0");
    EXPECT_EQ(newest.size, static_cast<std::int64_t>(kPackage.size()));
    ASSERT_EQ(newest.screenshots.size(), 1u);
    EXPECT_EQ(newest.screenshots.front().caption, "Main");
    EXPECT_EQ(result->find("com.example.other"), nullptr);
}

TEST(Marketplace, RefusesAnIndexNotSignedByTheMarketplaceKey)
{
    EXPECT_EQ(refusal(read(kIndex, comment(5), 0, kAuthor)), Kind::Signature);

    auto data = bytes(kIndex);
    const std::string signature = sign(kMarket, data, comment(5));
    data.back() = std::byte{' '};
    const std::array trusted{kMarket.key};
    EXPECT_EQ(refusal(readIndex(data, signature, trusted, 0)), Kind::Signature);
}

TEST(Marketplace, RefusesASignatureForAnotherFileOrSerial)
{
    EXPECT_EQ(refusal(read(kIndex, "timestamp:1 file:test.hikariplugin")), Kind::OtherFile);
    EXPECT_EQ(refusal(read(kIndex, comment(6))), Kind::OtherFile);
    EXPECT_EQ(refusal(read(kIndex, "file:index.json serial:5 generated:2026-11-03T10:00:00Z")), Kind::OtherFile);
}

TEST(Marketplace, RefusesAnIndexOlderThanOneAlreadySeen)
{
    EXPECT_EQ(refusal(read(kIndex, comment(5), 6)), Kind::Older);
    EXPECT_TRUE(read(kIndex, comment(5), 5));
}

TEST(Marketplace, RefusesAnIndexThatFailsItsSchema)
{
    EXPECT_EQ(refusal(read(index(5, "", "2"), comment(5))), Kind::Malformed);
    EXPECT_EQ(refusal(read(index(5, plugin(version("1.0.0", 1, R"(, "extra": 1)"))), comment(5))), Kind::Malformed);
    const std::string badApi = R"({"format": 1, "serial": 5, "generated": "2026-11-02T10:00:00Z",)"
                               R"( "apiVersions": {"x": "1.0.0"}, "featured": [], "plugins": []})";
    EXPECT_EQ(refusal(read(badApi, comment(5))), Kind::Malformed);
    EXPECT_EQ(refusal(read("not json", comment(5))), Kind::Malformed);
}

TEST(Marketplace, NewestSkipsRevokedVersionsAndOnesThisApiCannotRun)
{
    const std::string revoked = R"(, "revoked": {"reason": "Leaked a token"})";
    auto result = read(index(5, plugin(version("1.2.0", 1, revoked) + "," + version("1.1.0", 4) + "," + version("1.0.0", 1))),
                       comment(5));
    ASSERT_TRUE(result) << result.error().detail;
    const Listing &listing = result->plugins.front();
    ASSERT_NE(listing.newest(4), nullptr);
    EXPECT_EQ(listing.newest(4)->version, "1.1.0");
    EXPECT_EQ(listing.newest(1)->version, "1.0.0");
    ASSERT_TRUE(listing.versions.front().revoked);
    EXPECT_EQ(listing.versions.front().revoked->reason, "Leaked a token");

    result = read(index(5, plugin(version("1.0.0", 1), revoked)), comment(5));
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->plugins.front().revoked);
    EXPECT_EQ(result->plugins.front().newest(4), nullptr);
}

TEST(Marketplace, ARevocationNamesTheVersionToOfferInstead)
{
    const std::string revoked = R"(, "revoked": {"reason": "Leaked a token"})";
    auto result = read(index(5, plugin(version("1.2.0", 1, revoked) + "," + version("1.1.0", 4) + "," + version("1.0.0", 1))),
                       comment(5));
    ASSERT_TRUE(result) << result.error().detail;
    const Listing &listing = result->plugins.front();
    EXPECT_EQ(revocation(listing, "1.2.0", 4), (Revoked{"Leaked a token", "1.1.0"}));
    EXPECT_EQ(revocation(listing, "1.2.0", 1), (Revoked{"Leaked a token", "1.0.0"}));
    EXPECT_EQ(revocation(listing, "1.1.0", 4), std::nullopt);
    EXPECT_EQ(revocation(listing, "0.9.0", 4), std::nullopt);

    result = read(index(5, plugin(version("1.0.0", 1, revoked))), comment(5));
    ASSERT_TRUE(result);
    EXPECT_EQ(revocation(result->plugins.front(), "1.0.0", 4), (Revoked{"Leaked a token", ""}));

    result = read(index(5, plugin(version("1.0.0", 1), R"(, "revoked": {"reason": "Malware"})")), comment(5));
    ASSERT_TRUE(result);
    EXPECT_EQ(revocation(result->plugins.front(), "1.0.0", 4), (Revoked{"Malware", ""}));
}

TEST(Marketplace, ADownloadMustMatchTheListedSizeAndHash)
{
    const auto result = read(kIndex, comment(5));
    ASSERT_TRUE(result);
    const ListedVersion &listed = result->plugins.front().versions.front();
    EXPECT_FALSE(listedPackageProblem(listed, kPackage));
    EXPECT_TRUE(listedPackageProblem(listed, bytes("a .hikariplugin fil")));
    EXPECT_TRUE(listedPackageProblem(listed, bytes("a .hikariplugin filE")));
}

TEST(Marketplace, APackageMustBeTheListedPluginSignedByTheListedKey)
{
    const auto result = read(kIndex, comment(5));
    ASSERT_TRUE(result);
    const Listing &listing = result->plugins.front();
    const ListedVersion &listed = listing.versions.front();
    EXPECT_FALSE(listedManifestProblem(listing, listed, manifest("com.example.hello", "1.1.0", kAuthor), std::nullopt));
    EXPECT_FALSE(listedManifestProblem(listing, listed, manifest("com.example.hello", "1.1.0", kAuthor), kAuthor.key));
    EXPECT_TRUE(listedManifestProblem(listing, listed, manifest("com.example.other", "1.1.0", kAuthor), std::nullopt));
    EXPECT_TRUE(listedManifestProblem(listing, listed, manifest("com.example.hello", "1.0.0", kAuthor), std::nullopt));
    EXPECT_TRUE(listedManifestProblem(listing, listed, manifest("com.example.hello", "1.1.0", kMarket), std::nullopt));
    Manifest unsigned_ = manifest("com.example.hello", "1.1.0", kAuthor);
    unsigned_.publicKey.reset();
    EXPECT_TRUE(listedManifestProblem(listing, listed, unsigned_, std::nullopt));
    Manifest otherApi = manifest("com.example.hello", "1.1.0", kAuthor);
    otherApi.api = 1;
    EXPECT_TRUE(listedManifestProblem(listing, listed, otherApi, std::nullopt));
    Manifest unrestricted = manifest("com.example.hello", "1.1.0", kAuthor);
    unrestricted.tier = Tier::Unrestricted;
    EXPECT_TRUE(listedManifestProblem(listing, listed, unrestricted, std::nullopt));
    EXPECT_TRUE(listedManifestProblem(listing, listed, manifest("com.example.hello", "1.1.0", kAuthor), kMarket.key));
}

TEST(Marketplace, KeepsTheHighestSerialSeen)
{
    const fs::path dir = fs::temp_directory_path() / ("hikari-marketplace-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::remove_all(dir);
    const fs::path file = dir / "user/marketplace.json";
    EXPECT_EQ(loadHighestSerial(file), 0);
    ASSERT_TRUE(saveHighestSerial(file, 12));
    EXPECT_EQ(loadHighestSerial(file), 12);
    EXPECT_FALSE(fs::exists(dir / "user/marketplace.json.partial"));
    std::ofstream(file) << "{\"serial\": \"12\"}";
    EXPECT_EQ(loadHighestSerial(file), 0);
    fs::remove_all(dir);
}
