#include "hikari/application/recovery_store.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace hikari::application {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kFormat = "hikari-recovery 1";

std::uint64_t fnv1a(const std::vector<std::byte> &bytes)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::byte b : bytes) {
        h ^= static_cast<std::uint64_t>(b);
        h *= 1099511628211ULL;
    }
    return h;
}

bool readFile(const fs::path &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool writeFile(const fs::path &path, std::string_view data)
{
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out)
            return false;
    }
    std::string back;
    return readFile(path, back) && back == data; // verified
}

// Fields as "name length\n<bytes>\n", so any text (titles, drafts) survives.
void field(std::string &out, std::string_view name, std::string_view value)
{
    out += name;
    out += ' ';
    out += std::to_string(value.size());
    out += '\n';
    out += value;
    out += '\n';
}

std::string u8(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::u8string fromU8(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

std::map<std::string, std::string> parseFields(const std::string &data)
{
    std::map<std::string, std::string> fields;
    std::size_t pos = 0;
    while (pos < data.size()) {
        const std::size_t space = data.find(' ', pos);
        const std::size_t newline = data.find('\n', pos);
        if (space == std::string::npos || newline == std::string::npos || space > newline)
            break;
        const std::string name = data.substr(pos, space - pos);
        std::size_t length = 0;
        try {
            length = std::stoull(data.substr(space + 1, newline - space - 1));
        } catch (...) {
            break;
        }
        if (newline + 1 + length > data.size())
            break;
        fields[name] = data.substr(newline + 1, length);
        pos = newline + 1 + length + 1;
    }
    return fields;
}

std::string metaOf(const RecoveryContent &c)
{
    std::string meta;
    field(meta, "format", kFormat);
    field(meta, "extension", c.extension);
    field(meta, "title", c.title);
    field(meta, "original", c.originalPath);
    field(meta, "written", std::to_string(c.writtenMs));
    field(meta, "size", std::to_string(c.bytes.size()));
    field(meta, "hash", std::to_string(fnv1a(c.bytes)));
    if (c.draftRow) {
        field(meta, "draft.row", std::to_string(*c.draftRow));
        if (c.draft.text)
            field(meta, "draft.text", u8(*c.draft.text));
        if (c.draft.translation)
            field(meta, "draft.translation", u8(*c.draft.translation));
        if (c.draft.start)
            field(meta, "draft.start", std::to_string(c.draft.start->microseconds()));
        if (c.draft.end)
            field(meta, "draft.end", std::to_string(c.draft.end->microseconds()));
        if (c.draft.marginLeft)
            field(meta, "draft.marginLeft", std::to_string(*c.draft.marginLeft));
        if (c.draft.marginRight)
            field(meta, "draft.marginRight", std::to_string(*c.draft.marginRight));
        if (c.draft.marginVertical)
            field(meta, "draft.marginVertical", std::to_string(*c.draft.marginVertical));
    }
    if (c.frameRate) {
        field(meta, "fps.num", std::to_string(c.frameRate->first));
        field(meta, "fps.den", std::to_string(c.frameRate->second));
    }
    return meta;
}

std::optional<RecoveryContent> contentOf(const fs::path &generation)
{
    std::string meta, content;
    if (!readFile(generation / "meta", meta) || !readFile(generation / "content", content))
        return std::nullopt;
    auto f = parseFields(meta);
    if (f["format"] != kFormat)
        return std::nullopt;
    RecoveryContent c;
    c.bytes.resize(content.size());
    std::transform(content.begin(), content.end(), c.bytes.begin(), [](char ch) { return static_cast<std::byte>(ch); });
    try {
        // An incomplete or corrupt generation is rejected.
        if (std::stoull(f["size"]) != c.bytes.size() || std::stoull(f["hash"]) != fnv1a(c.bytes))
            return std::nullopt;
        c.extension = f["extension"];
        c.title = f["title"];
        c.originalPath = f["original"];
        c.writtenMs = std::stoll(f["written"]);
        if (f.contains("draft.row")) {
            c.draftRow = static_cast<std::size_t>(std::stoull(f["draft.row"]));
            if (f.contains("draft.text"))
                c.draft.text = fromU8(f["draft.text"]);
            if (f.contains("draft.translation"))
                c.draft.translation = fromU8(f["draft.translation"]);
            if (f.contains("draft.start"))
                c.draft.start = core::DocumentTime(std::stoll(f["draft.start"]));
            if (f.contains("draft.end"))
                c.draft.end = core::DocumentTime(std::stoll(f["draft.end"]));
            if (f.contains("draft.marginLeft"))
                c.draft.marginLeft = std::stoll(f["draft.marginLeft"]);
            if (f.contains("draft.marginRight"))
                c.draft.marginRight = std::stoll(f["draft.marginRight"]);
            if (f.contains("draft.marginVertical"))
                c.draft.marginVertical = std::stoll(f["draft.marginVertical"]);
        }
        if (f.contains("fps.num") && f.contains("fps.den"))
            c.frameRate = std::pair(std::stoll(f["fps.num"]), std::stoll(f["fps.den"]));
    } catch (...) {
        return std::nullopt;
    }
    return c;
}

} // namespace

RecoveryStore::RecoveryStore(fs::path root, std::string session, int capacity)
    : m_root(std::move(root)), m_session(std::move(session)), m_capacity(capacity)
{
}

std::optional<std::vector<std::uint64_t>> RecoveryStore::manifest(const fs::path &bundle, std::string *session) const
{
    std::string data;
    if (!readFile(bundle / "manifest", data))
        return std::nullopt;
    auto f = parseFields(data);
    if (f["format"] != kFormat)
        return std::nullopt;
    if (session)
        *session = f["session"];
    std::vector<std::uint64_t> generations;
    std::istringstream list(f["generations"]);
    for (std::uint64_t g; list >> g;)
        generations.push_back(g);
    return generations;
}

bool RecoveryStore::writeManifest(const fs::path &bundle, const std::vector<std::uint64_t> &generations) const
{
    std::string list;
    for (const auto g : generations)
        list += (list.empty() ? "" : " ") + std::to_string(g);
    std::string data;
    field(data, "format", kFormat);
    field(data, "session", m_session);
    field(data, "generations", list);
    const fs::path temporary = bundle / "manifest.new";
    if (!writeFile(temporary, data))
        return false;
    std::error_code ec;
    fs::rename(temporary, bundle / "manifest", ec); // replaces the previous manifest atomically
    return !ec;
}

bool RecoveryStore::write(const std::string &key, const RecoveryContent &content)
{
    if (!enabled())
        return false;
    const fs::path bundle = m_root / key;
    std::error_code ec;
    fs::create_directories(bundle, ec);
    if (ec)
        return false;
    auto generations = manifest(bundle, nullptr).value_or(std::vector<std::uint64_t>{});
    const std::uint64_t next = generations.empty() ? 1 : generations.back() + 1;
    const fs::path generation = bundle / std::to_string(next);
    fs::create_directories(generation, ec);
    if (ec)
        return false;
    const std::string bytes(reinterpret_cast<const char *>(content.bytes.data()), content.bytes.size());
    if (!writeFile(generation / "content", bytes) || !writeFile(generation / "meta", metaOf(content)) ||
        !contentOf(generation)) {
        fs::remove_all(generation, ec);
        return false; // the previous generation stays active
    }
    generations.push_back(next);
    std::vector<std::uint64_t> dropped;
    while (static_cast<int>(generations.size()) > m_capacity) {
        dropped.push_back(generations.front());
        generations.erase(generations.begin());
    }
    if (!writeManifest(bundle, generations)) {
        fs::remove_all(generation, ec);
        return false;
    }
    // Retention runs after activation; never the generation just activated.
    for (const auto g : dropped)
        fs::remove_all(bundle / std::to_string(g), ec);
    return true;
}

void RecoveryStore::discard(const std::string &key)
{
    if (m_root.empty())
        return;
    std::error_code ec;
    fs::remove_all(m_root / key, ec);
}

std::vector<RecoveryBundle> RecoveryStore::leftovers(const std::function<bool(const std::string &)> &sessionEnded) const
{
    std::vector<RecoveryBundle> out;
    std::error_code ec;
    if (m_root.empty() || !fs::is_directory(m_root, ec))
        return out;
    for (const auto &entry : fs::directory_iterator(m_root, ec)) {
        if (!entry.is_directory())
            continue;
        std::string session;
        const auto generations = manifest(entry.path(), &session);
        if (!generations || generations->empty() || session == m_session || !sessionEnded(session))
            continue;
        // The newest generation that reads back complete.
        for (auto it = generations->rbegin(); it != generations->rend(); ++it)
            if (auto content = contentOf(entry.path() / std::to_string(*it))) {
                out.push_back(RecoveryBundle{entry.path().filename().string(), session, *generations, std::move(*content)});
                break;
            }
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.latest.writtenMs > b.latest.writtenMs; });
    return out;
}

std::optional<RecoveryContent> RecoveryStore::read(const std::string &key, std::uint64_t generation) const
{
    return contentOf(m_root / key / std::to_string(generation));
}

void RecoveryStore::prune(std::chrono::system_clock::time_point now, std::chrono::hours maxAge)
{
    std::error_code ec;
    if (m_root.empty() || !fs::is_directory(m_root, ec))
        return;
    const auto cutoff =
        std::chrono::duration_cast<std::chrono::milliseconds>((now - maxAge).time_since_epoch()).count();
    for (const auto &entry : fs::directory_iterator(m_root, ec)) {
        if (!entry.is_directory())
            continue;
        std::string session;
        auto generations = manifest(entry.path(), &session);
        if (!generations)
            continue;
        std::vector<std::uint64_t> kept;
        for (const auto g : *generations) {
            const auto content = contentOf(entry.path() / std::to_string(g));
            if (content && content->writtenMs >= cutoff)
                kept.push_back(g);
        }
        if (kept.empty()) {
            fs::remove_all(entry.path(), ec);
            continue;
        }
        if (kept.size() != generations->size()) {
            // Keep the bundle's own session in its manifest.
            RecoveryStore owner(m_root, session, m_capacity);
            if (owner.writeManifest(entry.path(), kept))
                for (const auto g : *generations)
                    if (std::find(kept.begin(), kept.end(), g) == kept.end())
                        fs::remove_all(entry.path() / std::to_string(g), ec);
        }
    }
}

} // namespace hikari::application
