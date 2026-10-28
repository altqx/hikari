#include "hikari/application/recent_files.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace hikari::application {

void RecentFiles::add(const std::string &path, std::optional<std::int64_t> openedMs)
{
    if (path.empty())
        return;
    std::erase(m_paths, path);
    m_paths.insert(m_paths.begin(), path);
    if (openedMs)
        m_opened[path] = *openedMs;
    else
        m_opened.erase(path);
    if (m_paths.size() > kCapacity) {
        m_paths.resize(kCapacity);
        forgetUnlisted();
    }
}

void RecentFiles::set(std::vector<std::string> paths)
{
    m_paths.clear();
    m_opened.clear();
    for (auto it = paths.rbegin(); it != paths.rend(); ++it)
        add(*it);
}

bool RecentFiles::prune(const std::function<bool(const std::string &)> &missing)
{
    const auto before = m_paths.size();
    std::erase_if(m_paths, missing);
    forgetUnlisted();
    return m_paths.size() != before;
}

bool RecentFiles::remove(const std::string &path)
{
    m_opened.erase(path);
    return std::erase(m_paths, path) > 0;
}

std::optional<std::int64_t> RecentFiles::opened(const std::string &path) const
{
    const auto it = m_opened.find(path);
    return it == m_opened.end() ? std::nullopt : std::optional(it->second);
}

std::vector<std::string> RecentFiles::openedRecords() const
{
    std::vector<std::string> records;
    for (const auto &path : m_paths)
        if (const auto it = m_opened.find(path); it != m_opened.end())
            records.push_back(std::to_string(it->second) + ' ' + path);
    return records;
}

void RecentFiles::setOpenedRecords(const std::vector<std::string> &records)
{
    m_opened.clear();
    for (const auto &record : records) {
        const auto space = record.find(' ');
        if (space == std::string::npos || space == 0)
            continue;
        std::int64_t ms = 0;
        const auto [end, error] = std::from_chars(record.data(), record.data() + space, ms);
        if (error != std::errc() || end != record.data() + space)
            continue;
        const std::string path = record.substr(space + 1);
        if (std::ranges::find(m_paths, path) != m_paths.end())
            m_opened[path] = ms;
    }
}

void RecentFiles::forgetUnlisted()
{
    std::erase_if(m_opened, [&](const auto &entry) { return std::ranges::find(m_paths, entry.first) == m_paths.end(); });
}

bool isMissingLocalFile(const std::string &path, const std::function<bool(const std::string &)> &exists,
                        const std::function<bool(char drive)> &isRemoteDrive)
{
    if (path.starts_with("\\\\"))
        return false;
    if (path.size() > 2 && path[1] == ':' && isRemoteDrive(path[0]))
        return false;
    return !exists(path);
}

namespace {

std::string lowerExtension(std::string_view path)
{
    // wxString::AfterLast('.'): the whole name when there is no dot.
    const auto dot = path.rfind('.');
    std::string ext(dot == std::string_view::npos ? path : path.substr(dot + 1));
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

OpenKind openKindOf(std::string_view path, bool single)
{
    const std::string ext = lowerExtension(path);
    if (ext == "exe" || ext == "zip" || ext == "rar" || ext == "7z")
        return OpenKind::Refused;
    if (ext == "lua" || ext == "moon")
        return OpenKind::Script;
    if (single) {
        // Case-sensitive in legacy (EndsWith), the extension test is not.
        if (ext == "pass" || (ext == "txt" && path.ends_with("_keyframes.txt")) || ext == "stats" || ext == "log")
            return OpenKind::Keyframes;
    }
    if (ext == "ass" || ext == "ssa" || ext == "txt" || ext == "srt" || ext == "sub")
        return OpenKind::Subtitles;
    return OpenKind::Video;
}

} // namespace hikari::application
