#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

struct TextEncoding {
    std::string_view name; // ICU's
    std::string_view label;
};

// What Reopen with encoding offers, in its order.
std::span<const TextEncoding> reopenEncodings();

// The label for an ICU or uchardet name: the offered encoding's, else the name itself.
std::string encodingLabel(std::string_view name);

// A UTF-16 byte order mark's encoding, or uchardet's for bytes that are not UTF-8; nullopt for
// UTF-8, and when uchardet names nothing ICU can read.
std::optional<std::string> suggestedEncoding(std::span<const std::byte> bytes);

// UTF-8 without a BOM; nullopt when the bytes are not text in `encoding`.
std::optional<std::vector<std::byte>> utf8From(std::span<const std::byte> bytes, std::string_view encoding);

struct Encoded {
    std::vector<std::byte> bytes;
    std::u32string unencodable; // each character written as the encoding's substitute, once, in order
};

// UTF-8 `utf8` in `encoding`: a UTF-16 encoding starts with a BOM, any other drops one. nullopt when `utf8` is not
// UTF-8 or ICU has no such encoding.
std::optional<Encoded> encodeUtf8(std::span<const std::byte> utf8, std::string_view encoding);

}
