#pragma once

// P8: the notification-only update check (legacy UpdateChecker and SemVer at
// 20d647c4; docs/qt/distribution.md). It reads the GitHub release list and
// names a newer release; it never downloads or installs anything.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Semantic Versioning 2.0.0 with an optional leading "v"; build metadata is
// checked and dropped. Not a version: nullopt (the old four-part tags too).
struct SemVer {
    long long major = 0, minor = 0, patch = 0;
    std::string prerelease;
};
std::optional<SemVer> parseSemVer(std::string_view text);
// <0, 0, >0 as a has lower, equal or higher precedence than b.
int compareSemVer(const SemVer &a, const SemVer &b);
// False when either side is not a version.
bool isNewerVersion(std::string_view tag, std::string_view current);
// "v1.2.0-rc.1": a prerelease tag stays out of "stable only" even without
// GitHub's prerelease flag.
bool isPrereleaseTag(std::string_view tag);

struct ReleaseEntry {
    std::string tag, name, url, notes; // the reader gives a release without a name its tag
    bool draft = false, prerelease = false;
};
// The first entry of the list (GitHub's newest first) that is no draft,
// stable when asked, and newer than `current`.
std::optional<ReleaseEntry> findNewerRelease(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                             bool stableOnly);

// The legacy schedule: an automatic check runs only when it is on and the
// next-check time has passed; any finished check moves that time a day on,
// "Remind me in a week" a week.
inline constexpr std::int64_t kUpdateDay = 24 * 60 * 60;
inline constexpr std::int64_t kUpdateWeek = 7 * kUpdateDay;
inline bool automaticCheckDue(bool autoCheck, std::int64_t now, std::int64_t nextCheck)
{
    return autoCheck && now >= nextCheck;
}

} // namespace hikari::application
