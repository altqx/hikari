#pragma once

// Framing for helper processes (H1; docs/qt/backends.md, docs/qt/automation.md).
// Shared by the host and the helpers; plain C++ so helpers need no Qt.
//
// Frame: "HKRI" | u32 payload length | u16 kind | u16 code | u64 session |
//        u64 run | u64 request | payload. Little-endian.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::backends::helper {

inline constexpr std::uint32_t kMaxPayload = 64u << 20; // a larger frame is a protocol violation

enum class Kind : std::uint16_t {
    Hello = 1,     // helper -> host: code = protocol version, payload = helper name
    Welcome = 2,   // host -> helper: session = process-session generation
    Refuse = 3,    // host -> helper: incompatible version
    Request = 10,  // host -> helper
    Reply = 11,    // helper -> host: intermediate data for a request
    Progress = 12, // helper -> host
    Cancel = 13,   // host -> helper
    Terminal = 14, // helper -> host: code = outcome, ends the request
};

// Terminal outcome codes.
enum class Outcome : std::uint16_t { Ok = 0, Failed = 1, Cancelled = 2, Unsupported = 3, InvalidInput = 4 };

struct Frame {
    Kind kind = Kind::Request;
    std::uint16_t code = 0;
    std::uint64_t session = 0;
    std::uint64_t run = 0;
    std::uint64_t request = 0;
    std::vector<std::byte> payload;
};

std::vector<std::byte> encode(const Frame &frame);

// Incremental decoder: feed bytes, take complete frames. A malformed stream
// (bad magic, oversized payload) sets failed() and yields nothing more.
class Decoder {
public:
    void feed(const std::byte *data, std::size_t size);
    std::optional<Frame> next();
    bool failed() const { return m_failed; }

private:
    std::vector<std::byte> m_buffer;
    std::size_t m_offset = 0;
    bool m_failed = false;
};

// Payload fields: little-endian integers, length-prefixed strings and bytes.
class Writer {
public:
    Writer &u8(std::uint8_t v);
    Writer &i32(std::int32_t v);
    Writer &i64(std::int64_t v);
    Writer &str(const std::string &v);
    Writer &bytes(const std::vector<std::byte> &v);
    Writer &raw(const void *data, std::size_t size); // u32 length + bytes
    std::vector<std::byte> take() { return std::move(m_out); }

private:
    std::vector<std::byte> m_out;
};

// Reads fields in order; a short or malformed payload sets ok() to false and
// yields zero values from then on.
class Reader {
public:
    explicit Reader(const std::vector<std::byte> &payload) : m_in(payload) {}
    std::uint8_t u8();
    std::int32_t i32();
    std::int64_t i64();
    std::string str();
    std::vector<std::byte> bytes();
    bool ok() const { return m_ok; }

private:
    bool need(std::size_t n);
    const std::vector<std::byte> &m_in;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

std::vector<std::byte> bytesOf(const std::string &text);
std::string textOf(const std::vector<std::byte> &bytes);

} // namespace hikari::backends::helper
