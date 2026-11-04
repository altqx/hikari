#include "hikari/application/update_check.h"
#include "hikari/application/update_signature.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

// tests/minisign/fixtures: message.txt signed by key a with "hikarisub v1.0.0 hikarisub-update.json".
std::string fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_MINISIGN_FIXTURES) + "/" + name, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

minisign::PublicKey key(const std::string &name)
{
    return minisign::parsePublicKey(fixture(name + ".pub")).value();
}

std::optional<SignatureProblem> check(const std::string &signature, const std::vector<minisign::PublicKey> &trusted,
                                      const std::string &bytes = fixture("message.txt"),
                                      const std::string &tag = "v1.0.0",
                                      std::string_view file = kManifestAsset)
{
    return releaseSignatureProblem(bytes, signature, tag, file, trusted);
}

}

TEST(ReleaseSignature, PrehashedAndLegacySignaturesOfThisReleasePass)
{
    EXPECT_EQ(check(fixture("prehashed.minisig"), {key("a")}), std::nullopt);
    EXPECT_EQ(check(fixture("legacy.minisig"), {key("a")}), std::nullopt);
    EXPECT_EQ(check(fixture("prehashed.minisig"), {key("b"), key("a")}), std::nullopt);
}

TEST(ReleaseSignature, EachTamperingIsRefused)
{
    for (const char *signature : {"prehashed.minisig", "legacy.minisig"}) {
        SCOPED_TRACE(signature);
        const std::string text = fixture(signature);
        EXPECT_EQ(check(text, {key("a")}, fixture("message.txt") + " "), SignatureProblem::BadSignature);
        EXPECT_EQ(check(text, {key("a")}, fixture("message.txt"), "v1.0.1"), SignatureProblem::OtherRelease);
        EXPECT_EQ(check(text, {key("a")}, fixture("message.txt"), "v1.0.0", kSumsName), SignatureProblem::OtherRelease);
        EXPECT_EQ(check(text, {key("b")}), SignatureProblem::UntrustedKey);
        EXPECT_EQ(check(text, {}), SignatureProblem::UntrustedKey);
    }
    EXPECT_EQ(check(fixture("changed-comment.minisig"), {key("a")}), SignatureProblem::BadTrustedComment);
    EXPECT_EQ(check(fixture("by-b.minisig"), {key("a")}), SignatureProblem::UntrustedKey);
    EXPECT_EQ(check(fixture("b-claims-a.minisig"), {key("a")}), SignatureProblem::BadSignature);
    auto otherId = key("a");
    otherId.id[0] ^= 1;
    EXPECT_EQ(check(fixture("prehashed.minisig"), {otherId}), SignatureProblem::UntrustedKey);
}

TEST(ReleaseSignature, AMissingOrOversizeSignatureIsRefused)
{
    const std::vector<minisign::PublicKey> trusted{key("a")};
    EXPECT_EQ(releaseSignatureProblem(fixture("message.txt"), std::nullopt, "v1.0.0", kManifestAsset, trusted),
              SignatureProblem::Missing);
    std::string oversize = fixture("prehashed.minisig");
    oversize.resize(minisign::kMaxSignatureBytes + 1, '\n');
    EXPECT_EQ(check(oversize, {key("a")}), SignatureProblem::TooLarge);
    EXPECT_EQ(check("", {key("a")}), SignatureProblem::Malformed);
}

TEST(ReleaseSignature, TheTrustedCommentNamesTheTagAndTheFile)
{
    EXPECT_EQ(releaseTrustedComment("v0.1.0-beta.4", kManifestAsset), "hikarisub v0.1.0-beta.4 hikarisub-update.json");
    EXPECT_EQ(releaseTrustedComment("v0.1.0", kSumsName), "hikarisub v0.1.0 SHA256SUMS");
}

TEST(ReleaseSignature, EveryShippedKeyParses)
{
    const auto keys = releaseKeys();
    ASSERT_EQ(keys.size(), kReleaseKeys.size());
    EXPECT_EQ(keys.front().idText(), "A97C7A20EFD98999");
}
