#pragma once

#include "hikari/minisign/minisign.h"
#include "hikari/plugins/package.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hikari::plugins::test {

// A minisign secret key from a fixed seed, for signing fixtures in tests.
struct Signer {
    std::array<std::uint8_t, 64> secret{};
    minisign::PublicKey key;
};

Signer signer(std::uint8_t seed);
std::string sign(const Signer &signer, std::span<const std::byte> message);

std::vector<std::byte> bytes(std::string_view text);
// Stands in for the .hikariplugin file the signature covers.
std::vector<std::byte> packageBytes(const std::vector<PackageFile> &files);

}
