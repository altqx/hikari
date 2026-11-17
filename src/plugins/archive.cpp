#include "hikari/plugins/archive.h"

#include "hikari/api/file_names.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hikari::plugins {

namespace fs = std::filesystem;

namespace {

bool allowedChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
           c == '.' || c == '-' || c == '+';
}

bool goodPart(std::string_view part)
{
    return !part.empty() && part != "." && part != ".." && part.size() <= 255 &&
           std::all_of(part.begin(), part.end(), allowedChar) && !part.ends_with('.') && !part.ends_with(' ') &&
           !api::reservedOnWindows(part);
}

constexpr std::size_t kBlock = 512;

class Gunzip {
public:
    explicit Gunzip(const fs::path &file) : m_file(file, std::ios::binary)
    {
        m_ok = m_file.is_open() && inflateInit2(&m_stream, 16 + MAX_WBITS) == Z_OK;
    }
    ~Gunzip()
    {
        if (m_ok)
            inflateEnd(&m_stream);
    }
    Gunzip(const Gunzip &) = delete;
    Gunzip &operator=(const Gunzip &) = delete;

    bool ok() const { return m_ok; }
    bool broken() const { return m_broken; }

    // Fills `out` with exactly `size` bytes; false at the end of the data or on a broken stream.
    bool read(char *out, std::size_t size)
    {
        while (size > 0) {
            if (m_ready == m_used && !refill())
                return false;
            const std::size_t take = std::min(size, m_ready - m_used);
            std::copy_n(m_out.data() + m_used, take, out);
            m_used += take;
            out += take;
            size -= take;
        }
        return true;
    }

private:
    bool refill()
    {
        m_ready = m_used = 0;
        while (m_ready == 0) {
            if (m_ended)
                return false;
            if (m_stream.avail_in == 0) {
                m_file.read(m_in.data(), std::streamsize(m_in.size()));
                const std::streamsize got = m_file.gcount();
                if (got <= 0) {
                    m_broken = true;
                    return false;
                }
                m_stream.next_in = reinterpret_cast<Bytef *>(m_in.data());
                m_stream.avail_in = uInt(got);
            }
            m_stream.next_out = reinterpret_cast<Bytef *>(m_out.data());
            m_stream.avail_out = uInt(m_out.size());
            const int status = inflate(&m_stream, Z_NO_FLUSH);
            if (status == Z_STREAM_END)
                m_ended = true;
            else if (status != Z_OK && status != Z_BUF_ERROR) {
                m_broken = true;
                return false;
            }
            m_ready = m_out.size() - m_stream.avail_out;
        }
        return true;
    }

    std::ifstream m_file;
    z_stream m_stream{};
    std::array<char, 1 << 16> m_in{};
    std::array<char, 1 << 16> m_out{};
    std::size_t m_ready = 0, m_used = 0;
    bool m_ok = false, m_ended = false, m_broken = false;
};

std::string field(const char *header, std::size_t at, std::size_t size)
{
    const char *begin = header + at;
    return std::string(begin, std::find(begin, begin + size, '\0'));
}

std::optional<std::uint64_t> octal(const char *header, std::size_t at, std::size_t size)
{
    std::uint64_t value = 0;
    bool digits = false;
    for (std::size_t i = at; i < at + size; ++i) {
        const char c = header[i];
        if (c >= '0' && c <= '7') {
            if (value > (std::uint64_t(1) << 60))
                return std::nullopt;
            value = value * 8 + std::uint64_t(c - '0');
            digits = true;
        } else if (c == ' ' || c == '\0') {
            if (digits)
                break;
        } else {
            return std::nullopt;
        }
    }
    return value;
}

bool checksumHolds(const char *header)
{
    const std::optional<std::uint64_t> stored = octal(header, 148, 8);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i)
        sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
    return stored && *stored == sum;
}

std::optional<std::string> paxPath(std::string_view records)
{
    std::optional<std::string> path;
    while (!records.empty()) {
        const std::size_t space = records.find(' ');
        if (space == std::string_view::npos)
            return path;
        std::size_t length = 0;
        for (char c : records.substr(0, space))
            length = length * 10 + std::size_t(c - '0');
        if (length <= space + 1 || length > records.size())
            return path;
        const std::string_view record = records.substr(space + 1, length - space - 2);
        if (record.starts_with("path="))
            path = std::string(record.substr(5));
        records.remove_prefix(length);
    }
    return path;
}

std::string normalised(std::string path)
{
    while (path.starts_with("./"))
        path.erase(0, 2);
    while (path.ends_with('/'))
        path.pop_back();
    return path;
}

class TarReader {
public:
    TarReader(Gunzip &in, const fs::path &into, UnpackLimits limits) : m_in(in), m_into(into), m_claims(limits) {}

    std::expected<void, UnpackProblem> run()
    {
        std::array<char, kBlock> header{};
        std::optional<std::string> longPath;
        bool any = false;
        while (true) {
            if (!m_in.read(header.data(), kBlock))
                return !any        ? fail(UnpackProblem::Kind::NotAnArchive)
                       : m_in.broken() ? fail(UnpackProblem::Kind::Truncated)
                                       : std::expected<void, UnpackProblem>();
            if (std::all_of(header.begin(), header.end(), [](char c) { return c == '\0'; }))
                return {};
            if (!checksumHolds(header.data()))
                return fail(any ? UnpackProblem::Kind::Truncated : UnpackProblem::Kind::NotAnArchive);
            any = true;
            const std::optional<std::uint64_t> size = octal(header.data(), 124, 12);
            if (!size)
                return fail(UnpackProblem::Kind::TooLarge);
            std::string path = field(header.data(), 0, 100);
            if (field(header.data(), 257, 5) == "ustar")
                if (const std::string prefix = field(header.data(), 345, 155); !prefix.empty())
                    path = prefix + "/" + path;
            if (longPath)
                path = *std::exchange(longPath, std::nullopt);
            path = normalised(std::move(path));
            const char type = header[156];
            switch (type) {
            case 'x':
            case 'L': {
                if (*size > 65536)
                    return fail(UnpackProblem::Kind::TooLarge, path);
                std::string data(*size, '\0');
                if (!m_in.read(data.data(), data.size()) || !skipPadding(*size))
                    return fail(UnpackProblem::Kind::Truncated);
                longPath = type == 'L' ? std::optional(std::string(data.c_str())) : paxPath(data);
                continue;
            }
            case 'g':
                if (!skip(*size))
                    return fail(UnpackProblem::Kind::Truncated);
                continue;
            case '5':
                if (auto problem = directory(path); !problem)
                    return problem;
                continue;
            case '0':
            case '7':
            case '\0': {
                const std::optional<std::uint64_t> mode = octal(header.data(), 100, 8);
                if (auto written = file(path, *size, mode && (*mode & 0100)); !written)
                    return written;
                continue;
            }
            default:
                return fail(UnpackProblem::Kind::Link, path);
            }
        }
    }

private:
    static std::unexpected<UnpackProblem> fail(UnpackProblem::Kind kind, std::string path = {})
    {
        return std::unexpected(UnpackProblem{kind, std::move(path)});
    }

    bool skip(std::uint64_t size)
    {
        std::array<char, kBlock> sink{};
        for (std::uint64_t left = (size + kBlock - 1) / kBlock * kBlock; left > 0; left -= kBlock)
            if (!m_in.read(sink.data(), kBlock))
                return false;
        return true;
    }

    bool skipPadding(std::uint64_t size)
    {
        std::array<char, kBlock> sink{};
        const std::size_t pad = std::size_t((kBlock - size % kBlock) % kBlock);
        return pad == 0 || m_in.read(sink.data(), pad);
    }

    std::expected<void, UnpackProblem> directory(const std::string &path)
    {
        if (path.empty())
            return {};
        if (auto claimed = m_claims.folder(path); !claimed)
            return claimed;
        std::error_code error;
        fs::create_directories(m_into / fs::path(std::u8string(path.begin(), path.end())), error);
        return error ? fail(UnpackProblem::Kind::Write, path) : std::expected<void, UnpackProblem>();
    }

    std::expected<void, UnpackProblem> file(const std::string &path, std::uint64_t size, bool executable)
    {
        if (auto claimed = m_claims.file(path, size); !claimed)
            return claimed;
        const fs::path target = m_into / fs::path(std::u8string(path.begin(), path.end()));
        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (error || !out)
            return fail(UnpackProblem::Kind::Write, path);
        std::array<char, 1 << 16> chunk{};
        for (std::uint64_t left = size; left > 0;) {
            const std::size_t take = std::size_t(std::min<std::uint64_t>(left, chunk.size()));
            if (!m_in.read(chunk.data(), take))
                return fail(UnpackProblem::Kind::Truncated, path);
            if (!out.write(chunk.data(), std::streamsize(take)))
                return fail(UnpackProblem::Kind::Write, path);
            left -= take;
        }
        out.close();
        if (!out || !skipPadding(size))
            return fail(out ? UnpackProblem::Kind::Truncated : UnpackProblem::Kind::Write, path);
        if (executable)
            fs::permissions(target, fs::perms::owner_exec, fs::perm_options::add, error);
        return {};
    }

    Gunzip &m_in;
    fs::path m_into;
    UnpackClaims m_claims;
};

}

bool goodPackagePath(std::string_view path)
{
    if (path.empty() || path.size() > 400)
        return false;
    while (true) {
        const std::size_t slash = path.find('/');
        if (!goodPart(path.substr(0, slash)))
            return false;
        if (slash == std::string_view::npos)
            return true;
        path.remove_prefix(slash + 1);
    }
}

std::expected<void, UnpackProblem> UnpackClaims::parents(const std::string &path)
{
    for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        const std::string folder = api::lowered(path.substr(0, slash));
        if (m_files.contains(folder))
            return std::unexpected(UnpackProblem{UnpackProblem::Kind::BadPath, path});
        m_folders.insert(folder);
    }
    return {};
}

std::expected<void, UnpackProblem> UnpackClaims::folder(const std::string &path)
{
    if (!goodPackagePath(path) || m_files.contains(api::lowered(path)))
        return std::unexpected(UnpackProblem{UnpackProblem::Kind::BadPath, path});
    return parents(path + "/");
}

std::expected<void, UnpackProblem> UnpackClaims::file(const std::string &path, std::uint64_t size)
{
    const std::string key = api::lowered(path);
    if (!goodPackagePath(path) || m_folders.contains(key) || !m_files.insert(key).second)
        return std::unexpected(UnpackProblem{UnpackProblem::Kind::BadPath, path});
    if (auto claimed = parents(path); !claimed)
        return claimed;
    if (m_files.size() > m_limits.files || size > m_limits.bytes - m_total)
        return std::unexpected(UnpackProblem{UnpackProblem::Kind::TooLarge, path});
    m_total += size;
    return {};
}

std::expected<void, UnpackProblem> unpackTarGz(const fs::path &archive, const fs::path &into, UnpackLimits limits)
{
    Gunzip in(archive);
    if (!in.ok())
        return std::unexpected(UnpackProblem{UnpackProblem::Kind::NotAnArchive, {}});
    return TarReader(in, into, limits).run();
}

}
