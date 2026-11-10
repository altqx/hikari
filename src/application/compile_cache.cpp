#include "hikari/application/compile_cache.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>

namespace hikari::application {
namespace {

constexpr std::string_view kMagic = "hikari compile cache 1\n";

std::uint64_t fnv1a(std::string_view a, std::string_view b = {})
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const std::string_view part : {a, std::string_view("\0", 1), b})
        for (const char c : part) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
    return hash;
}

class Out {
public:
    void u64(std::uint64_t v) { bytes(&v, sizeof v); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void str(std::string_view s)
    {
        u64(s.size());
        m_data.append(s);
    }
    std::string take() { return std::move(m_data); }

private:
    void bytes(const void *p, std::size_t n) { m_data.append(static_cast<const char *>(p), n); }
    std::string m_data;
};

class In {
public:
    explicit In(std::string_view data) : m_data(data) {}
    std::uint64_t u64()
    {
        std::uint64_t v = 0;
        if (m_data.size() < sizeof v) {
            m_ok = false;
            return 0;
        }
        std::memcpy(&v, m_data.data(), sizeof v);
        m_data.remove_prefix(sizeof v);
        return v;
    }
    double f64() { return std::bit_cast<double>(u64()); }
    std::string_view str()
    {
        const std::uint64_t n = u64();
        if (!m_ok || n > m_data.size()) {
            m_ok = false;
            return {};
        }
        const std::string_view s = m_data.substr(0, n);
        m_data.remove_prefix(n);
        return s;
    }
    bool prefix(std::string_view p)
    {
        m_ok = m_ok && m_data.starts_with(p);
        if (m_ok)
            m_data.remove_prefix(p.size());
        return m_ok;
    }
    bool done() const { return m_ok && m_data.empty(); }
    bool ok() const { return m_ok; }

private:
    std::string_view m_data;
    bool m_ok = true;
};

}

CompileCache::CompileCache(std::filesystem::path dir, std::string compiler)
    : m_dir(std::move(dir)), m_compiler(std::move(compiler))
{
}

std::string CompileCache::compilerId(std::string_view compilerSource)
{
    return std::format("{}-{:016x}", compilerSource.size(), fnv1a(compilerSource));
}

std::filesystem::path CompileCache::entry(std::string_view file) const
{
    return m_dir / std::format("{:016x}.compiled", fnv1a(m_compiler, file));
}

std::optional<CompiledSource> CompileCache::find(std::string_view file, std::string_view source) const
{
    std::ifstream stream(entry(file), std::ios::binary);
    if (!stream)
        return std::nullopt;
    const std::string data(std::istreambuf_iterator<char>(stream), {});
    In in(data);
    if (!in.prefix(kMagic) || in.str() != m_compiler || in.str() != file || in.str() != source)
        return std::nullopt;
    CompiledSource out;
    const std::uint64_t lines = in.u64();
    if (!in.ok() || lines > data.size() / 16)
        return std::nullopt;
    out.lines.reserve(lines);
    for (std::uint64_t i = 0; i < lines; ++i) {
        const double line = in.f64();
        out.lines.emplace_back(line, in.f64());
    }
    out.code = in.str();
    if (!in.done())
        return std::nullopt;
    return out;
}

void CompileCache::store(std::string_view file, std::string_view source, const CompiledSource &compiled) const
{
    Out out;
    out.str(m_compiler);
    out.str(file);
    out.str(source);
    out.u64(compiled.lines.size());
    for (const auto &[line, position] : compiled.lines) {
        out.f64(line);
        out.f64(position);
    }
    out.str(compiled.code);
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    const auto target = entry(file);
    auto temporary = target;
    temporary += std::format(".{:08x}", std::random_device{}());
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << kMagic << out.take();
        if (!stream.flush()) {
            stream.close();
            std::filesystem::remove(temporary, ec);
            return;
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec)
        std::filesystem::remove(temporary, ec);
}

}
