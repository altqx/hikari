#include "hikari/application/project.h"

#include "hikari/application/automation_services.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <system_error>

namespace hikari::application {

std::optional<std::filesystem::path> nearestProjectRoot(const std::filesystem::path &file)
{
    std::error_code error;
    std::filesystem::path folder = std::filesystem::absolute(file, error).parent_path();
    if (error)
        return std::nullopt;
    for (;;) {
        if (std::filesystem::is_directory(folder / kProjectFolder, error))
            return folder;
        const auto parent = folder.parent_path();
        if (parent == folder || parent.empty())
            return std::nullopt;
        folder = parent;
    }
}

std::string projectName(const std::filesystem::path &root)
{
    const auto trimmed = root.has_filename() ? root : root.parent_path();
    const auto name = trimmed.filename().u8string();
    const auto whole = root.u8string();
    const auto &shown = name.empty() ? whole : name;
    return {shown.begin(), shown.end()};
}

bool folderTrusted(const std::string &root, const std::vector<std::string> &trustedFolders, bool windows)
{
    const std::string marker = root + (windows ? '\\' : '/') + std::string(kProjectFolder);
    return std::ranges::any_of(trustedFolders,
                               [&](const std::string &folder) { return scriptInFolder(marker, folder, windows); });
}

std::string projectKey(const std::string &canonicalRoot)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char c : canonicalRoot) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(hash));
    return text.data();
}

FolderPlace placeFolder(bool openInAWindow, bool currentEmpty, WindowRequest request)
{
    if (openInAWindow)
        return FolderPlace::Focus;
    if (request == WindowRequest::Reuse || (request == WindowRequest::Default && currentEmpty))
        return FolderPlace::Current;
    return FolderPlace::NewWindow;
}

}
