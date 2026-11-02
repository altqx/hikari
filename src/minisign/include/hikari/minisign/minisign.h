#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hikari::minisign {

inline constexpr std::size_t kMaxSignatureBytes = 64 * 1024;

struct PublicKey {
    std::array<std::uint8_t, 8> id{};
    std::array<std::uint8_t, 32> key{};

    // As minisign shows it: 16 upper-case hex digits.
    std::string idText() const;
    // The base64 line of a `.pub` file.
    std::string text() const;
    friend bool operator==(const PublicKey &, const PublicKey &) = default;
};

// Takes a whole `.pub` file or just its base64 line.
std::optional<PublicKey> parsePublicKey(std::string_view text);

enum class Refusal {
    Malformed,
    TooLarge,
    UnknownAlgorithm,
    UntrustedKey, // no trusted key has the signature's key id
    BadSignature,
    BadTrustedComment, // the global signature does not cover the trusted comment
};
std::string_view refusalText(Refusal refusal);

struct Verified {
    PublicKey key;
    std::string trustedComment;
    bool prehashed = true; // "ED"; false for the legacy "Ed"
};

// `signature` is a `.minisig` file's text.
std::expected<Verified, Refusal> verify(std::span<const std::byte> message, std::string_view signature,
                                        std::span<const PublicKey> trusted);

}
