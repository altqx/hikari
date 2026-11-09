#include "hikari/minisign/minisign.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace hikari::minisign;

namespace {

std::string fixture(const char *name)
{
    std::ifstream in(std::filesystem::path(HIKARI_MINISIGN_FIXTURES) / name, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<std::byte> bytes(std::string_view text)
{
    const auto *data = reinterpret_cast<const std::byte *>(text.data());
    return {data, data + text.size()};
}

class Minisign : public ::testing::Test {
protected:
    std::vector<PublicKey> trusted{*parsePublicKey(fixture("a.pub"))};
    std::vector<std::byte> message = bytes(fixture("message.txt"));

    Refusal refusal(const std::string &signature, std::span<const std::byte> what)
    {
        const auto result = verify(what, signature, trusted);
        EXPECT_FALSE(result);
        return result ? Refusal::Malformed : result.error();
    }
};

TEST_F(Minisign, APublicKeyReadsFromItsFileOrItsLine)
{
    const auto key = parsePublicKey(fixture("a.pub"));
    ASSERT_TRUE(key);
    EXPECT_EQ(key->idText(), "EFCDAB8967452301");
    EXPECT_EQ(parsePublicKey(key->text()), key);
    EXPECT_EQ(key->text(), "RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4");
    EXPECT_FALSE(parsePublicKey("RWQ"));
    EXPECT_FALSE(parsePublicKey(""));
    EXPECT_FALSE(parsePublicKey("untrusted comment: x\nnot base64!"));
}

TEST_F(Minisign, APrehashedSignatureVerifies)
{
    const auto result = verify(message, fixture("prehashed.minisig"), trusted);
    ASSERT_TRUE(result) << refusalText(result.error());
    EXPECT_TRUE(result->prehashed);
    EXPECT_EQ(result->trustedComment, "hikarisub v1.0.0 hikarisub-update.json");
    EXPECT_EQ(result->key, trusted[0]);
}

TEST_F(Minisign, ALegacySignatureVerifies)
{
    const auto result = verify(message, fixture("legacy.minisig"), trusted);
    ASSERT_TRUE(result) << refusalText(result.error());
    EXPECT_FALSE(result->prehashed);
    EXPECT_EQ(result->trustedComment, "hikarisub v1.0.0 hikarisub-update.json");
}

TEST_F(Minisign, ASignatureMinisignMadeVerifies)
{
    const auto key = parsePublicKey(fixture("real.pub"));
    ASSERT_TRUE(key);
    EXPECT_EQ(key->idText(), "9CA70DD5B29BD45D");
    const auto result = verify(message, fixture("real.minisig"), std::span(&*key, 1));
    ASSERT_TRUE(result) << refusalText(result.error());
    EXPECT_EQ(result->trustedComment, "made by minisign 0.12");
}

TEST_F(Minisign, CarriageReturnsAreAllowed)
{
    std::string signature = fixture("prehashed.minisig");
    for (std::size_t at = signature.find('\n'); at != std::string::npos; at = signature.find('\n', at + 2))
        signature.insert(at, 1, '\r');
    EXPECT_TRUE(verify(message, signature, trusted));
}

TEST_F(Minisign, AChangedMessageIsRefused)
{
    std::vector<std::byte> changed = message;
    changed[0] = std::byte('H');
    EXPECT_EQ(refusal(fixture("prehashed.minisig"), changed), Refusal::BadSignature);
    EXPECT_EQ(refusal(fixture("legacy.minisig"), changed), Refusal::BadSignature);
}

TEST_F(Minisign, AChangedTrustedCommentIsRefused)
{
    EXPECT_EQ(refusal(fixture("changed-comment.minisig"), message), Refusal::BadTrustedComment);
}

TEST_F(Minisign, AnUntrustedKeyIsRefused)
{
    EXPECT_EQ(refusal(fixture("by-b.minisig"), message), Refusal::UntrustedKey);
    trusted.push_back(*parsePublicKey(fixture("b.pub")));
    EXPECT_TRUE(verify(message, fixture("by-b.minisig"), trusted));
}

TEST_F(Minisign, AWrongKeyIdIsRefused)
{
    EXPECT_EQ(refusal(fixture("b-claims-a.minisig"), message), Refusal::BadSignature);
}

TEST_F(Minisign, AnUnknownAlgorithmIsRefused)
{
    std::string signature = fixture("prehashed.minisig");
    const std::size_t line = signature.find('\n') + 1;
    ASSERT_EQ(signature.substr(line, 3), "RUQ");
    signature[line + 1] = 'X';
    EXPECT_EQ(refusal(signature, message), Refusal::UnknownAlgorithm);
}

TEST_F(Minisign, AnOversizeSignatureIsRefused)
{
    std::string signature = fixture("prehashed.minisig");
    signature.append(kMaxSignatureBytes, '\n');
    EXPECT_EQ(refusal(signature, message), Refusal::TooLarge);
}

TEST_F(Minisign, AMissingOrMalformedSignatureIsRefused)
{
    EXPECT_EQ(refusal("", message), Refusal::Malformed);
    EXPECT_EQ(refusal("untrusted comment: x\nRUQ\ntrusted comment: y\nAAAA\n", message), Refusal::Malformed);
    std::string noTrustedComment = fixture("prehashed.minisig");
    noTrustedComment.replace(noTrustedComment.find("trusted comment: hikarisub"), 8, "trusted:");
    EXPECT_EQ(refusal(noTrustedComment, message), Refusal::Malformed);
}

TEST_F(Minisign, EveryRefusalHasText)
{
    for (const Refusal r : {Refusal::Malformed, Refusal::TooLarge, Refusal::UnknownAlgorithm, Refusal::UntrustedKey,
                            Refusal::BadSignature, Refusal::BadTrustedComment})
        EXPECT_FALSE(refusalText(r).empty());
}

}

TEST(Sha256, MatchesSha256sum)
{
    EXPECT_EQ(hikari::minisign::sha256Hex({}), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    EXPECT_EQ(hikari::minisign::sha256Hex(std::as_bytes(std::span(abc))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
