#include "hikari/application/update_check.h"

namespace hikari::application {

namespace {

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool isIdentifierChar(char c)
{
    return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}

bool isNumeric(std::string_view s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!isDigit(c))
            return false;
    return true;
}

bool hasLeadingZero(std::string_view s)
{
    return s.size() > 1 && s[0] == '0';
}

std::optional<long long> parseNumber(std::string_view s)
{
    if (!isNumeric(s) || hasLeadingZero(s) || s.size() > 18)
        return std::nullopt;
    long long value = 0;
    for (char c : s)
        value = value * 10 + (c - '0');
    return value;
}

bool validIdentifiers(std::string_view s, bool prerelease)
{
    while (true) {
        const std::size_t dot = s.find('.');
        const std::string_view id = s.substr(0, dot);
        if (id.empty())
            return false;
        for (char c : id)
            if (!isIdentifierChar(c))
                return false;
        if (prerelease && isNumeric(id) && hasLeadingZero(id))
            return false;
        if (dot == std::string_view::npos)
            return true;
        s.remove_prefix(dot + 1);
    }
}

int compareIdentifiers(std::string_view a, std::string_view b)
{
    const bool aNumeric = isNumeric(a), bNumeric = isNumeric(b);
    if (aNumeric != bNumeric)
        return aNumeric ? -1 : 1;
    // Without leading zeros, a longer number is a larger one.
    if (aNumeric && a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    const int result = a.compare(b);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

} // namespace

std::optional<SemVer> parseSemVer(std::string_view text)
{
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
        text.remove_prefix(1);
    if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
        if (!validIdentifiers(text.substr(plus + 1), false))
            return std::nullopt;
        text = text.substr(0, plus);
    }
    SemVer version;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        const std::string_view pre = text.substr(dash + 1);
        if (!validIdentifiers(pre, true))
            return std::nullopt;
        version.prerelease = std::string(pre);
        text = text.substr(0, dash);
    }
    long long *core[] = {&version.major, &version.minor, &version.patch};
    for (int i = 0; i < 3; ++i) {
        const std::size_t dot = text.find('.');
        if ((i < 2) == (dot == std::string_view::npos))
            return std::nullopt;
        const auto number = parseNumber(text.substr(0, dot));
        if (!number)
            return std::nullopt;
        *core[i] = *number;
        text = i < 2 ? text.substr(dot + 1) : std::string_view();
    }
    return version;
}

int compareSemVer(const SemVer &a, const SemVer &b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    if (a.prerelease.empty() || b.prerelease.empty())
        return int(a.prerelease.empty()) - int(b.prerelease.empty());
    std::string_view left = a.prerelease, right = b.prerelease;
    while (true) {
        const std::size_t leftDot = left.find('.'), rightDot = right.find('.');
        if (const int result = compareIdentifiers(left.substr(0, leftDot), right.substr(0, rightDot)))
            return result;
        const bool leftDone = leftDot == std::string_view::npos, rightDone = rightDot == std::string_view::npos;
        if (leftDone || rightDone)
            return int(rightDone) - int(leftDone);
        left.remove_prefix(leftDot + 1);
        right.remove_prefix(rightDot + 1);
    }
}

bool isNewerVersion(std::string_view tag, std::string_view current)
{
    const auto newer = parseSemVer(tag), installed = parseSemVer(current);
    return newer && installed && compareSemVer(*newer, *installed) > 0;
}

bool isPrereleaseTag(std::string_view tag)
{
    const auto version = parseSemVer(tag);
    return version && !version->prerelease.empty();
}

std::optional<ReleaseEntry> findNewerRelease(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                             bool stableOnly)
{
    for (const auto &release : releases) {
        if (release.draft)
            continue;
        if (stableOnly && (release.prerelease || isPrereleaseTag(release.tag)))
            continue;
        if (release.tag.empty() || !isNewerVersion(release.tag, current))
            continue;
        return release;
    }
    return std::nullopt;
}

} // namespace hikari::application
