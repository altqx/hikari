#include "hikari/api/base64.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace hikari::api {

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 3; ++k)
            v = v << 8 | (k < n ? static_cast<unsigned char>(bytes[i + k]) : 0u);
        for (std::size_t k = 0; k < 4; ++k)
            out += k <= n ? kBase64[(v >> (18 - 6 * k)) & 63] : '=';
    }
    return out;
}

std::optional<std::string> fromBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;
    std::array<int, 256> value;
    value.fill(-1);
    for (std::size_t i = 0; i < kBase64.size(); ++i)
        value[static_cast<unsigned char>(kBase64[i])] = int(i);
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const int pad = last ? int(text[i + 3] == '=') + int(text[i + 2] == '=' && text[i + 3] == '=') : 0;
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const int digit = k >= 4 - std::size_t(pad) ? 0 : value[static_cast<unsigned char>(text[i + k])];
            if (digit < 0)
                return std::nullopt;
            v = v << 6 | std::uint32_t(digit);
        }
        for (int k = 0; k < 3 - pad; ++k)
            out += char((v >> (16 - 8 * k)) & 0xFF);
    }
    return out;
}

}
