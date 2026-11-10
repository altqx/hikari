#include "test_signer.h"

#include <sodium.h>

namespace hikari::plugins::test {

namespace {

std::string base64(std::span<const std::uint8_t> data)
{
    std::string out(sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), data.data(), data.size(), sodium_base64_VARIANT_ORIGINAL);
    out.pop_back();
    return out;
}

}

Signer signer(std::uint8_t seed)
{
    if (sodium_init() < 0)
        return {};
    std::array<std::uint8_t, crypto_sign_SEEDBYTES> seedBytes;
    seedBytes.fill(seed);
    Signer s;
    crypto_sign_seed_keypair(s.key.key.data(), s.secret.data(), seedBytes.data());
    s.key.id.fill(seed);
    return s;
}

std::string sign(const Signer &signer, std::span<const std::byte> message, std::string_view trustedComment)
{
    std::array<std::uint8_t, crypto_generichash_BYTES_MAX> digest{};
    crypto_generichash(digest.data(), digest.size(), reinterpret_cast<const std::uint8_t *>(message.data()),
                       message.size(), nullptr, 0);
    std::array<std::uint8_t, 74> body{'E', 'D'};
    std::copy(signer.key.id.begin(), signer.key.id.end(), body.begin() + 2);
    crypto_sign_detached(body.data() + 10, nullptr, digest.data(), digest.size(), signer.secret.data());
    const std::string comment(trustedComment);
    std::vector<std::uint8_t> covered(body.begin() + 10, body.end());
    covered.insert(covered.end(), comment.begin(), comment.end());
    std::array<std::uint8_t, crypto_sign_BYTES> global{};
    crypto_sign_detached(global.data(), nullptr, covered.data(), covered.size(), signer.secret.data());
    return "untrusted comment: signature from minisign secret key\n" + base64(body) + "\ntrusted comment: " + comment +
           "\n" + base64(global) + "\n";
}

std::vector<std::byte> bytes(std::string_view text)
{
    const auto *data = reinterpret_cast<const std::byte *>(text.data());
    return {data, data + text.size()};
}

std::vector<std::byte> packageBytes(const std::vector<PackageFile> &files)
{
    std::vector<std::byte> out;
    for (const PackageFile &file : files) {
        const auto path = bytes(file.path);
        out.insert(out.end(), path.begin(), path.end());
        out.insert(out.end(), file.data.begin(), file.data.end());
    }
    return out;
}

}
