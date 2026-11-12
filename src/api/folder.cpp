#include "hikari/api/folder.h"

#include "hikari/api/file_names.h"
#include "hikari/api/json.h"

#include <algorithm>

namespace hikari::api {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxRelative = 1024;
constexpr std::string_view kForbidden = "\\:*?\"<>|";

bool goodName(std::string_view part)
{
    return !part.empty() && part != "." && part != ".." && part.size() <= 255 &&
           std::ranges::none_of(part, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F || kForbidden.contains(c); }) &&
           !part.ends_with('.') && !part.ends_with(' ') && !reservedOnWindows(part);
}

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

bool within(const fs::path &root, const fs::path &real)
{
    return std::mismatch(root.begin(), root.end(), real.begin(), real.end()).first == root.end();
}

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

}

std::expected<fs::path, std::string> pathInside(const fs::path &folder, std::string_view relative)
{
    if (relative.size() > kMaxRelative)
        return std::unexpected("the path is longer than " + std::to_string(kMaxRelative) + " bytes");
    if (!isUtf8(relative))
        return std::unexpected(std::string("the path is not UTF-8"));
    std::error_code error;
    const fs::path root = fs::canonical(folder, error);
    if (error)
        return std::unexpected(std::string("the folder isn't there"));
    fs::path target = root;
    for (std::string_view rest = relative; !rest.empty();) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (!goodName(part))
            return std::unexpected(inQuotes(relative) + " has a part HikariSub refuses: " + inQuotes(part));
        target /= fs::path(std::u8string(part.begin(), part.end()));
        if (slash == std::string_view::npos)
            break;
        rest.remove_prefix(slash + 1);
        if (rest.empty())
            return std::unexpected(inQuotes(relative) + " ends with a slash");
    }
    const fs::path real = fs::weakly_canonical(target, error);
    if (error)
        return std::unexpected(inQuotes(relative) + " can't be followed");
    if (!within(root, real))
        return std::unexpected(inQuotes(relative) + " leads outside the folder");
    return real;
}

std::optional<FolderEntries> folderEntries(const fs::path &root, const fs::path &real)
{
    std::error_code error;
    const fs::path canonicalRoot = fs::canonical(root, error);
    if (error)
        return std::nullopt;
    fs::directory_iterator it(real, fs::directory_options::skip_permission_denied, error);
    if (error)
        return std::nullopt;
    FolderEntries out;
    for (; it != fs::directory_iterator(); it.increment(error)) {
        if (error)
            break;
        std::string name = utf8(it->path().filename());
        if (!isUtf8(name))
            continue;
        std::error_code linkError;
        if (it->is_symlink(linkError)) {
            const fs::path target = fs::weakly_canonical(it->path(), linkError);
            if (linkError || !within(canonicalRoot, target))
                continue;
        }
        if (out.entries.size() == kMaxFolderEntries) {
            out.truncated = true;
            break;
        }
        out.entries.push_back(FolderEntry{std::move(name), it->is_directory(linkError)});
    }
    std::ranges::sort(out.entries, {}, &FolderEntry::name);
    return out;
}

}
