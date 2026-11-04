#pragma once

#include "hikari/minisign/minisign.h"

#include <sodium.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace hikari::test {

// A minisign key pair made for one test run, and the .minisig files it signs.
class MinisignSigner {
public:
    MinisignSigner()
    {
        if (sodium_init() < 0)
            std::abort();
        crypto_sign_keypair(m_public.data(), m_secret.data());
        randombytes_buf(m_id.data(), m_id.size());
    }

    minisign::PublicKey publicKey() const
    {
        minisign::PublicKey key;
        key.id = m_id;
        key.key = m_public;
        return key;
    }

    // Prehashed ("ED") unless `legacy` ("Ed").
    std::string sign(std::string_view bytes, std::string_view trustedComment, bool legacy = false) const
    {
        std::string payload(bytes);
        if (!legacy) {
            std::array<unsigned char, crypto_generichash_BYTES_MAX> digest{};
            crypto_generichash(digest.data(), digest.size(), reinterpret_cast<const unsigned char *>(bytes.data()),
                               bytes.size(), nullptr, 0);
            payload.assign(reinterpret_cast<const char *>(digest.data()), digest.size());
        }
        const std::string signature = detached(payload);
        const std::string line = std::string(legacy ? "Ed" : "ED") +
                                 std::string(reinterpret_cast<const char *>(m_id.data()), m_id.size()) + signature;
        return "untrusted comment: test signature\n" + base64(line) + "\ntrusted comment: " +
               std::string(trustedComment) + "\n" + base64(detached(signature + std::string(trustedComment))) + "\n";
    }

private:
    std::string detached(std::string_view message) const
    {
        std::string out(crypto_sign_BYTES, '\0');
        crypto_sign_detached(reinterpret_cast<unsigned char *>(out.data()), nullptr,
                             reinterpret_cast<const unsigned char *>(message.data()), message.size(),
                             m_secret.data());
        return out;
    }

    static std::string base64(std::string_view bytes)
    {
        std::string out(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
        sodium_bin2base64(out.data(), out.size(), reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(),
                          sodium_base64_VARIANT_ORIGINAL);
        out.resize(out.size() - 1);
        return out;
    }

    std::array<std::uint8_t, 32> m_public{};
    std::array<std::uint8_t, 64> m_secret{};
    std::array<std::uint8_t, 8> m_id{};
};

}
