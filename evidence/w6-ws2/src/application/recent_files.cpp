#include "hikari/application/recent_files.h"

#include <algorithm>
#include <cctype>

namespace hikari::application {

void RecentFiles::add(const std::string &path)
{
    if (path.empty())
        return;
    std::erase(m_paths, path);
    m_paths.insert(m_paths.begin(), path);
    if (m_paths.size() > kCapacity)
        m_paths.resize(kCapacity);
}

void RecentFiles::set(std::vector<std::string> paths)
{
    m_paths.clear();
    for (auto it = paths.rbegin(); it != paths.rend(); ++it)
        add(*it);
}

bool RecentFiles::prune(const std::function<bool(const std::string &)> &missing)
{
    const auto before = m_paths.size();
    std::erase_if(m_paths, missing);
    return m_paths.size() != before;
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
