#include "hikari/backends/helper_protocol.h"

#include <cstring>

namespace hikari::backends::helper {

namespace {

constexpr std::size_t kHeader = 4 + 4 + 2 + 2 + 8 + 8 + 8;

template <typename T> void put(std::vector<std::byte> &out, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i)
        out.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xff));
}

template <typename T> T get(const std::byte *p)
{
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return static_cast<T>(v);
}

} // namespace

std::vector<std::byte> encode(const Frame &f)
{
    std::vector<std::byte> out;
    out.reserve(kHeader + f.payload.size());
    for (char c : {'H', 'K', 'R', 'I'})
        out.push_back(static_cast<std::byte>(c));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(f.payload.size()));
    put<std::uint16_t>(out, static_cast<std::uint16_t>(f.kind));
    put<std::uint16_t>(out, f.code);
    put<std::uint64_t>(out, f.session);
    put<std::uint64_t>(out, f.run);
    put<std::uint64_t>(out, f.request);
    out.insert(out.end(), f.payload.begin(), f.payload.end());
    return out;
}

void Decoder::feed(const std::byte *data, std::size_t size)
{
    if (m_failed)
        return;
    if (m_offset > 0 && m_offset == m_buffer.size()) {
        m_buffer.clear();
        m_offset = 0;
    }
    m_buffer.insert(m_buffer.end(), data, data + size);
}

std::optional<Frame> Decoder::next()
{
    if (m_failed || m_buffer.size() - m_offset < kHeader)
        return std::nullopt;
    const std::byte *p = m_buffer.data() + m_offset;
    if (std::memcmp(p, "HKRI", 4) != 0) {
        m_failed = true;
        return std::nullopt;
    }
    const auto length = get<std::uint32_t>(p + 4);
    if (length > kMaxPayload) {
        m_failed = true;
        return std::nullopt;
    }
    if (m_buffer.size() - m_offset < kHeader + length)
        return std::nullopt;
    Frame f;
    f.kind = static_cast<Kind>(get<std::uint16_t>(p + 8));
    f.code = get<std::uint16_t>(p + 10);
    f.session = get<std::uint64_t>(p + 12);
    f.run = get<std::uint64_t>(p + 20);
    f.request = get<std::uint64_t>(p + 28);
    f.payload.assign(p + kHeader, p + kHeader + length);
    m_offset += kHeader + length;
    return f;
}

std::vector<std::byte> bytesOf(const std::string &text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string textOf(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

} // namespace hikari::backends::helper
