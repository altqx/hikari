#include "hikari/application/update_check.h"

#include <algorithm>
#include <iterator>

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

namespace {

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

std::string lowered(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        c = lower(c);
    return out;
}

bool isAlphanumeric(char c)
{
    return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// The tag a candidate needs: "v" and a version without build metadata.
std::optional<SemVer> candidateVersion(std::string_view tag)
{
    if (tag.size() < 2 || tag[0] != 'v' || tag.find('+') != std::string_view::npos || !isValidUrlTag(tag))
        return std::nullopt;
    return parseSemVer(tag);
}

std::string_view withoutV(std::string_view tag)
{
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        tag.remove_prefix(1);
    return tag;
}

// The package keys the client knows, the manifest's and 2.1's.
constexpr std::string_view kPackageKeys[] = {"nsis", "portable-windows", "appimage", "deb", "rpm", "portable-linux"};

const ReleaseAsset *assetNamed(const ReleaseEntry &release, std::string_view name)
{
    for (const auto &asset : release.assets)
        if (asset.name == name)
            return &asset;
    return nullptr;
}

// "sha256:<hex>" as GitHub gives an asset's digest; empty otherwise.
std::string digestHash(std::string_view digest)
{
    constexpr std::string_view prefix = "sha256:";
    if (!equalsIgnoringCase(digest.substr(0, prefix.size()), prefix))
        return {};
    const std::string_view hex = digest.substr(prefix.size());
    return isSha256Hex(hex) ? lowered(hex) : std::string();
}

std::optional<std::vector<long long>> dottedNumbers(std::string_view text)
{
    std::vector<long long> out;
    while (true) {
        const std::size_t dot = text.find('.');
        // Not parseNumber: a part of these may have leading zeros.
        const std::string_view part = text.substr(0, dot);
        if (!isNumeric(part) || part.size() > 9)
            return std::nullopt;
        long long value = 0;
        for (char c : part)
            value = value * 10 + (c - '0');
        out.push_back(value);
        if (dot == std::string_view::npos)
            return out;
        text.remove_prefix(dot + 1);
    }
}

bool shellSafe(char c)
{
    return isAlphanumeric(c) || c == '/' || c == '.' || c == '_' || c == '-' || c == '+' || c == '~' || c == ',' ||
           c == ':' || c == '@' || c == '%' || c == '=';
}

} // namespace

std::optional<Codebase> codebaseOfVersion(std::string_view version)
{
    // By the version's core alone: 0.1.0-beta.2 is the rewrite's, though
    // SemVer sorts it below 0.1.0.
    const auto v = parseSemVer(version);
    if (!v)
        return std::nullopt;
    return v->major == 0 && v->minor == 0 ? Codebase::Legacy : Codebase::Qt;
}

std::optional<Codebase> parseCodebase(std::string_view name)
{
    if (name == "legacy")
        return Codebase::Legacy;
    if (name == "qt")
        return Codebase::Qt;
    return std::nullopt;
}

std::optional<PackageKind> parsePackageKind(std::string_view marker)
{
    if (marker.ends_with("\r\n"))
        marker.remove_suffix(2);
    else if (marker.ends_with('\n'))
        marker.remove_suffix(1);
    if (marker == "nsis-user")
        return PackageKind::NsisUser;
    if (marker == "nsis-machine")
        return PackageKind::NsisMachine;
    if (marker == "appimage")
        return PackageKind::AppImage;
    if (marker == "deb")
        return PackageKind::Deb;
    if (marker == "rpm")
        return PackageKind::Rpm;
    if (marker == "arch")
        return PackageKind::Arch;
    return std::nullopt;
}

std::string_view packageKindToken(PackageKind kind, bool windows)
{
    switch (kind) {
    case PackageKind::NsisUser:
        return "nsis-user";
    case PackageKind::NsisMachine:
        return "nsis-machine";
    case PackageKind::AppImage:
        return "appimage";
    case PackageKind::Deb:
        return "deb";
    case PackageKind::Rpm:
        return "rpm";
    case PackageKind::Arch:
        return "arch";
    case PackageKind::Portable:
        break;
    }
    return windows ? "portable-windows" : "portable-linux";
}

std::string_view packageKey(PackageKind kind, bool windows)
{
    switch (kind) {
    case PackageKind::NsisUser:
    case PackageKind::NsisMachine:
        return "nsis";
    case PackageKind::Arch:
        return {};
    default:
        return packageKindToken(kind, windows);
    }
}

bool installsUpdates(PackageKind kind)
{
    return kind != PackageKind::Portable && kind != PackageKind::Arch;
}

std::optional<AurChannel> parseAurChannel(std::string_view content)
{
    if (content.ends_with("\r\n"))
        content.remove_suffix(2);
    else if (content.ends_with('\n'))
        content.remove_suffix(1);
    if (content == "stable")
        return AurChannel::Stable;
    if (content == "beta")
        return AurChannel::Beta;
    return std::nullopt;
}

bool stableOnlyFor(bool option, PackageKind kind, std::optional<AurChannel> channel)
{
    if (kind == PackageKind::Arch && channel)
        return *channel == AurChannel::Stable;
    return option;
}

bool hasStableOnlyOption(PackageKind kind, std::optional<AurChannel> channel)
{
    return !(kind == PackageKind::Arch && channel);
}

bool aurCarries(bool prerelease, std::optional<AurChannel> channel)
{
    return !prerelease || channel == AurChannel::Beta;
}

std::string aurUpdateCommand(std::string_view helper, std::optional<AurChannel> channel)
{
    std::string command(helper.empty() ? std::string_view("yay") : helper);
    command += " -Syu";
    if (channel)
        command += *channel == AurChannel::Beta ? " hikarisub-beta-bin" : " hikarisub-stable-bin";
    return command;
}

bool isValidAssetName(std::string_view name)
{
    if (name.empty() || !isAlphanumeric(name[0]))
        return false;
    for (char c : name)
        if (!isAlphanumeric(c) && c != '.' && c != '_' && c != '-')
            return false;
    return true;
}

bool isValidUrlTag(std::string_view tag)
{
    if (tag.empty() || tag == "." || tag == "..")
        return false;
    for (char c : tag)
        if (!isAlphanumeric(c) && c != '-' && c != '.' && c != '+')
            return false;
    return true;
}

std::string assetNameFor(std::string_view key, std::string_view version)
{
    const std::string v(version);
    if (key == "nsis")
        return "HikariSub-" + v + "-windows-x64-setup.exe";
    if (key == "portable-windows")
        return "HikariSub-" + v + "-windows-x64.zip";
    if (key == "appimage")
        return "HikariSub-" + v + "-linux-x86_64.AppImage";
    if (key == "deb")
        return "hikarisub-" + v + "-linux-amd64.deb";
    if (key == "rpm")
        return "hikarisub-" + v + "-linux-x86_64.rpm";
    if (key == "portable-linux")
        return "hikarisub-" + v + "-linux-x86_64.tar.gz";
    return {};
}

bool isAllowedDownloadHost(std::string_view host)
{
    for (std::string_view allowed : {"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"})
        if (equalsIgnoringCase(host, allowed))
            return true;
    return false;
}

std::vector<ReleaseEntry> whatsNewReleases(const std::vector<ReleaseEntry> &releases, std::string_view since,
                                           std::string_view installed)
{
    const auto top = parseSemVer(installed);
    if (!top)
        return {};
    auto after = parseSemVer(since);
    if (after && (codebaseOfVersion(since) != Codebase::Qt || compareSemVer(*after, *top) >= 0))
        after.reset(); // a first start, a downgrade, the legacy app: the installed version alone
    std::vector<std::pair<SemVer, const ReleaseEntry *>> found;
    for (const auto &release : releases) {
        if (release.draft)
            continue;
        const auto version = candidateVersion(release.tag);
        if (!version || codebaseOfVersion(withoutV(release.tag)) != Codebase::Qt)
            continue;
        const int toTop = compareSemVer(*version, *top);
        if (toTop > 0 || (after ? compareSemVer(*version, *after) <= 0 : toTop != 0))
            continue;
        const bool seen = std::any_of(found.begin(), found.end(),
                                      [&](const auto &f) { return compareSemVer(f.first, *version) == 0; });
        if (!seen)
            found.emplace_back(*version, &release);
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) { return compareSemVer(a.first, b.first) > 0; });
    std::vector<ReleaseEntry> out;
    for (const auto &f : found)
        out.push_back(*f.second);
    return out;
}

WhatsNewShown whatsNewShown(const std::vector<ReleaseEntry> &releases, std::string_view installed, bool stableOnly)
{
    // As updateCandidates: "stable only" holds on a stable build.
    const bool skipPrereleases = stableOnly && !isPrereleaseTag(installed);
    WhatsNewShown shown;
    for (const auto &release : releases) {
        if (skipPrereleases && isPrerelease(release))
            continue;
        if (shown.releases.size() < kMaxWhatsNew)
            shown.releases.push_back(release);
        else
            ++shown.more;
    }
    return shown;
}

std::string whatsNewSince(std::string_view lastRun, std::string_view storedSince, std::string_view installed)
{
    const std::string_view since = lastRun == installed ? storedSince : lastRun;
    // As whatsNewReleases: a Qt version below the installed one, else none.
    const auto after = parseSemVer(since);
    const auto top = parseSemVer(installed);
    if (!after || !top || codebaseOfVersion(since) != Codebase::Qt || compareSemVer(*after, *top) >= 0)
        return {};
    return std::string(since);
}

bool isPrerelease(const ReleaseEntry &release)
{
    return release.prerelease || isPrereleaseTag(release.tag);
}

std::vector<ReleaseEntry> updateCandidates(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                           bool stableOnly)
{
    const auto installed = parseSemVer(current);
    std::vector<std::pair<SemVer, const ReleaseEntry *>> found;
    if (!installed)
        return {};
    // Installers spec 2.4, as legacy's SkipsPrereleases: "Stable versions
    // only" holds on a stable build; a prerelease build always takes
    // prereleases, so a beta is offered the next beta.
    const bool skipPrereleases = stableOnly && installed->prerelease.empty();
    for (const auto &release : releases) {
        if (release.draft || (skipPrereleases && isPrerelease(release)))
            continue;
        const auto version = candidateVersion(release.tag);
        if (!version || compareSemVer(*version, *installed) <= 0)
            continue;
        const bool seen = std::any_of(found.begin(), found.end(),
                                      [&](const auto &f) { return compareSemVer(f.first, *version) == 0; });
        if (!seen)
            found.emplace_back(*version, &release);
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) { return compareSemVer(a.first, b.first) > 0; });
    std::vector<ReleaseEntry> out;
    for (const auto &f : found)
        out.push_back(*f.second);
    return out;
}

std::optional<ReleaseEntry> findNewerRelease(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                             bool stableOnly)
{
    auto candidates = updateCandidates(releases, current, stableOnly);
    if (candidates.empty())
        return std::nullopt;
    return std::move(candidates.front());
}

ManifestResult readManifest(const ManifestFields &fields, const ReleaseEntry &release)
{
    ManifestResult result;
    if (!fields.schema)
        return result;
    if (*fields.schema != 1) {
        result.status = ManifestStatus::UnknownSchema;
        return result;
    }
    const std::string_view version = withoutV(release.tag);
    const auto codebase = fields.codebase ? parseCodebase(*fields.codebase) : std::nullopt;
    if (!fields.version || *fields.version != version || !codebase || codebaseOfVersion(version) != codebase)
        return result;
    if (!fields.offerTo || fields.offerTo->empty() || !fields.packagesIsObject)
        return result;
    ReleasePackages &out = result.packages;
    out.codebase = *codebase;
    out.fromManifest = true;
    for (const auto &name : *fields.offerTo) {
        const auto to = parseCodebase(name);
        if (!to)
            return result;
        if (std::find(out.offerTo.begin(), out.offerTo.end(), *to) == out.offerTo.end())
            out.offerTo.push_back(*to);
    }
    for (const std::string_view key : kPackageKeys) {
        const auto entry = fields.packages.find(key);
        if (entry == fields.packages.end())
            continue;
        const auto &p = entry->second;
        // The name is the one 2.1 gives the key, so that a manifest cannot
        // have one kind download another kind's file.
        if (!p.name || *p.name != assetNameFor(key, version) || !p.sha256 || !isSha256Hex(*p.sha256) || !p.size ||
            *p.size <= 0)
            continue;
        UpdatePackage package{*p.name, *p.size, lowered(*p.sha256), p.minOs.value_or(std::string()),
                              p.minGlibc.value_or(std::string())};
        const ReleaseAsset *asset = assetNamed(release, package.name);
        if (!asset)
            continue;
        if (const std::string digest = digestHash(asset->digest); !digest.empty() && digest != package.sha256)
            continue;
        out.packages.emplace(std::string(key), std::move(package));
    }
    result.status = ManifestStatus::Valid;
    return result;
}

ReleasePackages fallbackPackages(const ReleaseEntry &release)
{
    ReleasePackages out;
    const std::string_view version = withoutV(release.tag);
    out.codebase = codebaseOfVersion(version).value_or(Codebase::Qt);
    out.offerTo = {out.codebase};
    for (const std::string_view key : kPackageKeys) {
        const ReleaseAsset *asset = assetNamed(release, assetNameFor(key, version));
        if (!asset || asset->size <= 0)
            continue;
        std::string hash = digestHash(asset->digest);
        if (hash.empty())
            continue;
        out.packages.emplace(std::string(key), UpdatePackage{asset->name, asset->size, std::move(hash), {}, {}});
    }
    return out;
}

bool offeredTo(const ReleasePackages &packages, Codebase codebase)
{
    return std::find(packages.offerTo.begin(), packages.offerTo.end(), codebase) != packages.offerTo.end();
}

UpdateOffer chooseOffer(PackageKind kind, bool windows, const ReleasePackages &packages, std::string_view osVersion,
                        std::string_view glibcVersion)
{
    UpdateOffer offer;
    if (kind == PackageKind::Arch) {
        offer.kind = OfferKind::Aur;
        return offer;
    }
    if (!installsUpdates(kind))
        return offer;
    const auto found = packages.packages.find(packageKey(kind, windows));
    if (found == packages.packages.end())
        return offer;
    const UpdatePackage &package = found->second;
    if (!isSha256Hex(package.sha256) || package.size <= 0 || package.size > kMaxPackageBytes ||
        !isValidAssetName(package.name))
        return offer;
    offer.package = package;
    const std::string &need = windows ? package.minOs : package.minGlibc;
    if (!dottedVersionAtLeast(windows ? osVersion : glibcVersion, need)) {
        offer.kind = OfferKind::Requirement;
        offer.requirement = need;
        offer.requirementIsOs = windows;
        return offer;
    }
    offer.kind = OfferKind::Install;
    return offer;
}

bool dottedVersionAtLeast(std::string_view have, std::string_view need)
{
    if (need.empty())
        return true;
    const auto a = dottedNumbers(have), b = dottedNumbers(need);
    if (!a || !b)
        return false;
    for (std::size_t i = 0; i < std::max(a->size(), b->size()); ++i) {
        const long long x = i < a->size() ? (*a)[i] : 0, y = i < b->size() ? (*b)[i] : 0;
        if (x != y)
            return x > y;
    }
    return true;
}

std::int64_t downloadDeadlineMs(std::int64_t size)
{
    constexpr std::int64_t kMinimum = 10 * 60 * 1000;
    const std::int64_t atRate = size > 0 ? size / (100 * 1024) * 1000 + 1000 : 0;
    return std::max(kMinimum, atRate);
}

bool isSha256Hex(std::string_view text)
{
    if (text.size() != 64)
        return false;
    for (char c : text)
        if (!isDigit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
            return false;
    return true;
}

std::string nsisUpdateArguments(bool machine, bool relaunch, long long waitPid, std::string_view installDir)
{
    std::string out = "/S /UPDATE /WAITPID=" + std::to_string(waitPid);
    if (relaunch)
        out += " /RELAUNCH";
    out += machine ? " /ALLUSERS" : " /CURRENTUSER";
    out += " /D=";
    out += installDir;
    return out;
}

bool isReleaseVersion(std::string_view version)
{
    if (version.empty() || version.size() > 64 || version[0] < '0' || version[0] > '9' ||
        version.find('+') != std::string_view::npos || !isValidUrlTag(version))
        return false;
    return parseSemVer(version).has_value();
}

std::string sumsUrl(std::string_view version)
{
    if (!isReleaseVersion(version))
        return {};
    return "https://github.com/altqx/hikari/releases/download/v" + std::string(version) + "/" + std::string(kSumsName);
}

std::optional<std::string> listedSha256(std::string_view sums, std::string_view name)
{
    if (sums.size() > kMaxSumsBytes || name.empty())
        return std::nullopt;
    std::optional<std::string> found;
    int count = 0;
    while (!sums.empty()) {
        const std::size_t end = sums.find('\n');
        std::string_view line = sums.substr(0, end);
        sums = end == std::string_view::npos ? std::string_view() : sums.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.size() < 66 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*') || line.substr(66) != name)
            continue;
        ++count;
        found = std::string(line.substr(0, 64));
    }
    if (count != 1)
        return std::nullopt;
    for (char c : *found)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return std::nullopt;
    return found;
}

InstallCheck checkInstallVersion(std::string_view version, std::string_view installed)
{
    if (!isReleaseVersion(version) || !isNewerVersion(version, installed))
        return InstallCheck::BadVersion;
    return InstallCheck::Ok;
}

InstallCheck checkListedSha256(std::string_view sums, std::string_view name, std::string_view actual)
{
    const auto expected = listedSha256(sums, name);
    if (!expected)
        return InstallCheck::NotListed;
    return *expected == actual ? InstallCheck::Ok : InstallCheck::Mismatch;
}

bool isPlainWindowsPath(std::string_view path)
{
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (path.size() < 4 || path.size() > 32000 || !letter(path[0]) || path[1] != ':' || path[2] != '\\')
        return false;
    // No name that ends in a dot or a space: Windows drops those, so "." and
    // "..", and "a\.. \b", which is "b".
    for (std::size_t i = 4; i <= path.size(); ++i) {
        if ((i == path.size() || path[i] == '\\') && (path[i - 1] == '.' || path[i - 1] == ' '))
            return false;
    }
    for (std::size_t i = 2; i < path.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 32 || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == ':' || c == '/')
            return false;
    }
    return true;
}

std::optional<ApplyRequest> parseApplyArguments(const std::vector<std::string> &arguments)
{
    if (arguments.size() != 4)
        return std::nullopt;
    ApplyRequest request;
    request.version = arguments[0];
    request.file = arguments[1];
    if (!isReleaseVersion(request.version) || !isPlainWindowsPath(request.file))
        return std::nullopt;
    const std::string &pid = arguments[2];
    if (pid.empty() || pid.size() > 10 || !std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    const unsigned long long value = std::stoull(pid);
    if (value == 0 || value > 0xffffffffull)
        return std::nullopt;
    request.pid = static_cast<unsigned long>(value);
    if (arguments[3] != "0" && arguments[3] != "1")
        return std::nullopt;
    request.relaunch = arguments[3] == "1";
    return request;
}

std::string appImageStagingPath(std::string_view appImage, std::string_view version)
{
    const std::size_t slash = appImage.find_last_of('/');
    if (appImage.empty() || appImage.front() != '/' || slash + 1 == appImage.size())
        return {};
    return std::string(appImage.substr(0, slash + 1)) + "." + std::string(appImage.substr(slash + 1)) + "." +
           std::string(version) + ".update";
}

namespace {
// The kind, then the path's FNV-1a hash.
std::string keyWithPath(std::string_view kindToken, std::string_view path)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : path) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::string key = std::string(kindToken) + "-";
    for (int shift = 60; shift >= 0; shift -= 4)
        key += "0123456789abcdef"[(hash >> shift) & 0xF];
    return key;
}
} // namespace

std::string updateCopyKey(std::string_view kindToken, std::string_view appImage)
{
    if (kindToken != packageKindToken(PackageKind::AppImage, false))
        return std::string(kindToken);
    return keyWithPath(kindToken, appImage);
}

std::string whatsNewCopyKey(std::string_view kindToken, std::string_view path)
{
    return path.empty() ? std::string(kindToken) : keyWithPath(kindToken, path);
}

std::string manualInstallCommand(std::string_view helper, std::string_view file, std::string_view version)
{
    auto quote = [](std::string_view text) {
        if (!text.empty() && std::all_of(text.begin(), text.end(), shellSafe))
            return std::string(text);
        std::string quoted = "'";
        for (char c : text)
            quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return quoted + "'";
    };
    return "sudo " + quote(helper) + " install " + quote(file) + " " + quote(version);
}

StartupArguments parseStartupArguments(const std::vector<std::string> &arguments)
{
    constexpr std::string_view health = "--hikari-health=", from = "--hikari-updated-from=";
    constexpr std::string_view exportDocs = "--export-docs";
    StartupArguments out;
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const std::string &argument = *it;
        if (argument == "--version")
            out.printVersion = true;
        else if (argument == exportDocs)
            out.exportDocs = std::next(it) != arguments.end() ? *++it : std::string();
        else if (argument.starts_with(exportDocs) && argument[exportDocs.size()] == '=')
            out.exportDocs = argument.substr(exportDocs.size() + 1);
        else if (argument.starts_with(health))
            out.healthFile = argument.substr(health.size());
        else if (argument.starts_with(from))
            out.updatedFrom = argument.substr(from.size());
        else
            out.rest.push_back(argument);
    }
    return out;
}

} // namespace hikari::application
