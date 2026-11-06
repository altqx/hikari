#include "hikari/minisign/minisign.h"

#include <sodium.h>

#include <algorithm>
#include <vector>

namespace hikari::minisign {

namespace {

constexpr std::string_view kUntrustedPrefix = "untrusted comment: ";
constexpr std::string_view kTrustedPrefix = "trusted comment: ";

bool ready()
{
    static const bool ok = sodium_init() >= 0;
    return ok;
}

std::optional<std::vector<std::uint8_t>> fromBase64(std::string_view text)
{
    std::vector<std::uint8_t> out(text.size() / 4 * 3 + 3);
    std::size_t length = 0;
    const char *end = nullptr;
    if (sodium_base642bin(out.data(), out.size(), text.data(), text.size(), nullptr, &length, &end,
                          sodium_base64_VARIANT_ORIGINAL) != 0 ||
        end != text.data() + text.size())
        return std::nullopt;
    out.resize(length);
    return out;
}

std::string toBase64(std::span<const std::uint8_t> bytes)
{
    std::string out(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), bytes.data(), bytes.size(), sodium_base64_VARIANT_ORIGINAL);
    out.resize(out.size() - 1);
    return out;
}

std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        out.push_back(line);
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

bool verifyDetached(std::span<const std::uint8_t> signature, std::span<const std::uint8_t> message,
                    const PublicKey &key)
{
    return crypto_sign_verify_detached(signature.data(), message.data(), message.size(), key.key.data()) == 0;
}

}

std::string PublicKey::idText() const
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (auto it = id.rbegin(); it != id.rend(); ++it) {
        out += kHex[*it >> 4];
        out += kHex[*it & 15];
    }
    return out;
}

std::string PublicKey::text() const
{
    std::array<std::uint8_t, 42> bytes{'E', 'd'};
    std::copy(id.begin(), id.end(), bytes.begin() + 2);
    std::copy(key.begin(), key.end(), bytes.begin() + 10);
    return toBase64(bytes);
}

std::optional<PublicKey> parsePublicKey(std::string_view text)
{
    if (!ready())
        return std::nullopt;
    std::string_view line;
    for (const std::string_view candidate : lines(text))
        if (!candidate.empty() && !candidate.starts_with(kUntrustedPrefix)) {
            line = candidate;
            break;
        }
    const auto bytes = fromBase64(line);
    if (!bytes || bytes->size() != 42 || (*bytes)[0] != 'E' || (*bytes)[1] != 'd')
        return std::nullopt;
    PublicKey key;
    std::copy_n(bytes->begin() + 2, 8, key.id.begin());
    std::copy_n(bytes->begin() + 10, 32, key.key.begin());
    return key;
}

std::string_view refusalText(Refusal refusal)
{
    switch (refusal) {
    case Refusal::Malformed: return "the signature file is malformed";
    case Refusal::TooLarge: return "the signature file is too large";
    case Refusal::UnknownAlgorithm: return "the signature uses an unknown algorithm";
    case Refusal::UntrustedKey: return "the signature is from a key that is not trusted";
    case Refusal::BadSignature: return "the signature does not match the file";
    case Refusal::BadTrustedComment: return "the trusted comment was changed";
    }
    return "the signature was refused";
}

std::expected<Verified, Refusal> verify(std::span<const std::byte> message, std::string_view signature,
                                        std::span<const PublicKey> trusted)
{
    if (signature.size() > kMaxSignatureBytes)
        return std::unexpected(Refusal::TooLarge);
    if (!ready())
        return std::unexpected(Refusal::Malformed);
    const std::vector<std::string_view> text = lines(signature);
    if (text.size() < 4 || !text[0].starts_with(kUntrustedPrefix) || !text[2].starts_with(kTrustedPrefix))
        return std::unexpected(Refusal::Malformed);
    const auto body = fromBase64(text[1]);
    const auto global = fromBase64(text[3]);
    if (!body || body->size() != 74 || !global || global->size() != crypto_sign_BYTES)
        return std::unexpected(Refusal::Malformed);
    const bool prehashed = (*body)[0] == 'E' && (*body)[1] == 'D';
    if (!prehashed && !((*body)[0] == 'E' && (*body)[1] == 'd'))
        return std::unexpected(Refusal::UnknownAlgorithm);
    const auto key = std::find_if(trusted.begin(), trusted.end(), [&](const PublicKey &k) {
        return std::equal(k.id.begin(), k.id.end(), body->begin() + 2);
    });
    if (key == trusted.end())
        return std::unexpected(Refusal::UntrustedKey);
    const std::span<const std::uint8_t> sig(body->data() + 10, crypto_sign_BYTES);
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t *>(message.data()), message.size());
    if (prehashed) {
        std::array<std::uint8_t, crypto_generichash_BYTES_MAX> digest{};
        crypto_generichash(digest.data(), digest.size(), bytes.data(), bytes.size(), nullptr, 0);
        if (!verifyDetached(sig, digest, *key))
            return std::unexpected(Refusal::BadSignature);
    } else if (!verifyDetached(sig, bytes, *key)) {
        return std::unexpected(Refusal::BadSignature);
    }
    const std::string_view comment = text[2].substr(kTrustedPrefix.size());
    std::vector<std::uint8_t> covered(sig.begin(), sig.end());
    covered.insert(covered.end(), comment.begin(), comment.end());
    if (!verifyDetached(*global, covered, *key))
        return std::unexpected(Refusal::BadTrustedComment);
    return Verified{*key, std::string(comment), prehashed};
}

std::string sha256Hex(std::span<const std::byte> bytes)
{
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    if (!ready())
        return {};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
    std::string hex(digest.size() * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    hex.pop_back();
    return hex;
}

}
