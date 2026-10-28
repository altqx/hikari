#include "hikari/backends/helper_protocol.h"

#include <bit>
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

Writer &Writer::u8(std::uint8_t v)
{
    put<std::uint8_t>(m_out, v);
    return *this;
}
Writer &Writer::i32(std::int32_t v)
{
    put<std::uint32_t>(m_out, static_cast<std::uint32_t>(v));
    return *this;
}
Writer &Writer::i64(std::int64_t v)
{
    put<std::uint64_t>(m_out, static_cast<std::uint64_t>(v));
    return *this;
}
Writer &Writer::f64(double v)
{
    return i64(std::bit_cast<std::int64_t>(v));
}
Writer &Writer::raw(const void *data, std::size_t size)
{
    put<std::uint32_t>(m_out, static_cast<std::uint32_t>(size));
    const auto *b = static_cast<const std::byte *>(data);
    m_out.insert(m_out.end(), b, b + size);
    return *this;
}
Writer &Writer::str(const std::string &v)
{
    return raw(v.data(), v.size());
}
Writer &Writer::bytes(const std::vector<std::byte> &v)
{
    return raw(v.data(), v.size());
}

bool Reader::need(std::size_t n)
{
    if (!m_ok || m_in.size() - m_pos < n) {
        m_ok = false;
        return false;
    }
    return true;
}
std::uint8_t Reader::u8()
{
    if (!need(1))
        return 0;
    return get<std::uint8_t>(m_in.data() + m_pos++);
}
std::int32_t Reader::i32()
{
    if (!need(4))
        return 0;
    const auto v = static_cast<std::int32_t>(get<std::uint32_t>(m_in.data() + m_pos));
    m_pos += 4;
    return v;
}
std::int64_t Reader::i64()
{
    if (!need(8))
        return 0;
    const auto v = static_cast<std::int64_t>(get<std::uint64_t>(m_in.data() + m_pos));
    m_pos += 8;
    return v;
}
double Reader::f64()
{
    return std::bit_cast<double>(i64());
}
std::vector<std::byte> Reader::bytes()
{
    const auto n = static_cast<std::uint32_t>(i32());
    if (!need(n))
        return {};
    std::vector<std::byte> out(m_in.begin() + static_cast<std::ptrdiff_t>(m_pos),
                               m_in.begin() + static_cast<std::ptrdiff_t>(m_pos + n));
    m_pos += n;
    return out;
}
std::string Reader::str()
{
    return textOf(bytes());
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
