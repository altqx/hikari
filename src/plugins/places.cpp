#include "hikari/plugins/places.h"

#include "hikari/api/json.h"
#include "names.h"

#include <algorithm>

namespace hikari::plugins {

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

}

std::expected<fs::path, std::string> pathInside(const fs::path &folder, std::string_view relative)
{
    if (relative.size() > kMaxRelative)
        return std::unexpected("the path is longer than " + std::to_string(kMaxRelative) + " bytes");
    if (!api::isUtf8(relative))
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
    if (std::mismatch(root.begin(), root.end(), real.begin(), real.end()).first != root.end())
        return std::unexpected(inQuotes(relative) + " leads outside the folder");
    return real;
}

}
