#pragma once

// Pure: the network, files and processes are the app layer's.

#include <cstdint>
#include <functional>
#include <map>
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

// Below 0.1.0 HikariSub legacy, from 0.1.0 on HikariSub.
enum class Codebase { Legacy, Qt };
std::optional<Codebase> codebaseOfVersion(std::string_view version);
std::optional<Codebase> parseCodebase(std::string_view name);

// How this copy was installed, from package-kind.txt beside the
// executable. No marker, or a token this build does not know, is portable.
enum class PackageKind { Portable, NsisUser, NsisMachine, AppImage, Deb, Rpm, Arch };
// The marker's one token with an optional trailing newline; anything else
// is no kind (portable).
std::optional<PackageKind> parsePackageKind(std::string_view marker);
// The token (`portable-windows` or `portable-linux` by platform), as
// `--version` prints it.
std::string_view packageKindToken(PackageKind kind, bool windows);
// The manifest's package key; empty for the AUR, which never downloads.
std::string_view packageKey(PackageKind kind, bool windows);
// NSIS, AppImage, deb and rpm install updates; the AUR and portable copies
// only say that one exists.
bool installsUpdates(PackageKind kind);

// The channel of an AUR install, from aur-channel.txt beside the executable,
// which the PKGBUILD writes: "stable" (hikarisub-stable-bin, stable releases
// only) or "beta" (hikarisub-beta-bin, every release). The file's one token
// with an optional trailing newline; anything else is no channel.
enum class AurChannel { Stable, Beta };
std::optional<AurChannel> parseAurChannel(std::string_view content);
// "Stable versions only" as a check applies it: an AUR install with a
// channel follows its package (hikarisub-beta-bin is told about prereleases,
// hikarisub-stable-bin only about stable releases) whatever the option says;
// every other copy follows the option.
bool stableOnlyFor(bool option, PackageKind kind, std::optional<AurChannel> channel);
// Whether the option means anything here: not for an AUR install with a
// channel, whose package decides.
bool hasStableOnlyOption(PackageKind kind, std::optional<AurChannel> channel);
// Whether the install's AUR package carries a release: a stable one always,
// a prerelease only hikarisub-beta-bin. The AUR notice says when it does not.
bool aurCarries(bool prerelease, std::optional<AurChannel> channel);
// `helper` ("yay" when empty) upgrading the install's package, or the whole
// system when the install has no channel.
std::string aurUpdateCommand(std::string_view helper, std::optional<AurChannel> channel);

// Every asset name, and a tag that may go into a URL.
bool isValidAssetName(std::string_view name);
bool isValidUrlTag(std::string_view tag);
// The asset of package key `key` for version `version` (no "v"); empty for
// a key that has no asset.
std::string assetNameFor(std::string_view key, std::string_view version);
// The hosts a download may be redirected to, compared without case.
bool isAllowedDownloadHost(std::string_view host);

struct ReleaseAsset {
    std::string name;
    std::int64_t size = 0;
    std::string digest; // GitHub's "sha256:<hex>", or empty
};

struct ReleaseEntry {
    std::string tag, name, url, notes; // the reader gives a release without a name its tag
    bool draft = false, prerelease = false;
    std::vector<ReleaseAsset> assets;
    std::string published; // GitHub's published_at ("2026-10-21T09:00:00Z"), or empty
};
// The releases that are no drafts, have a SemVer tag, are newer than
// `current` and fit the channel: highest version first, one per version
// (the first listed wins).
std::vector<ReleaseEntry> updateCandidates(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                           bool stableOnly);
// At most this many candidates are looked at in one check.
inline constexpr std::size_t kMaxCandidates = 5;
// The highest candidate.
std::optional<ReleaseEntry> findNewerRelease(const std::vector<ReleaseEntry> &releases, std::string_view current,
                                             bool stableOnly);
// A prerelease by GitHub's flag or by its tag.
bool isPrerelease(const ReleaseEntry &release);

// The releases whose notes show for `installed` after `since`, the version
// that ran before it: no drafts, HikariSub's only (0.1.0 on), newer than
// `since` and up to `installed`, on every channel, highest first, one per
// version (the first listed wins). A `since` that is no version, HikariSub
// legacy's or not below `installed` gives `installed` alone.
std::vector<ReleaseEntry> whatsNewReleases(const std::vector<ReleaseEntry> &releases, std::string_view since,
                                           std::string_view installed);
// At most this many versions show; the rest are counted.
inline constexpr std::size_t kMaxWhatsNew = 10;
struct WhatsNewShown {
    std::vector<ReleaseEntry> releases;
    std::size_t more = 0;
};
// What shows of whatsNewReleases on the update checker's channel: "stable
// only" (stableOnlyFor) leaves pre-releases out on a stable build; a
// pre-release build shows them, as its check offers it the next one.
WhatsNewShown whatsNewShown(const std::vector<ReleaseEntry> &releases, std::string_view installed, bool stableOnly);
// The version that ran before `installed`, recorded at each start: the one
// that ran last when it differs (an update; none on the first start ever),
// else `storedSince`, kept until the next update. None where the installed
// version shows alone: a downgrade, HikariSub legacy, no version.
std::string whatsNewSince(std::string_view lastRun, std::string_view storedSince, std::string_view installed);

// The update manifest, hikarisub-update.json.
inline constexpr std::string_view kManifestAsset = "hikarisub-update.json";
inline constexpr std::int64_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::int64_t kMaxPackageBytes = std::int64_t{1} << 30;

struct UpdatePackage {
    std::string name;
    std::int64_t size = 0;
    std::string sha256; // lowercase hex
    std::string minOs, minGlibc;
};

// A release's packages, from its manifest or, without one, its assets.
struct ReleasePackages {
    Codebase codebase = Codebase::Qt;
    std::vector<Codebase> offerTo;
    std::map<std::string, UpdatePackage, std::less<>> packages;
    bool fromManifest = false;
};

// The manifest's fields as read from its JSON: a field that is missing or
// of another type is left unset.
struct ManifestFields {
    std::optional<std::int64_t> schema;
    std::optional<std::string> version, codebase;
    std::optional<std::vector<std::string>> offerTo; // unset unless every element is a string
    struct Package {
        std::optional<std::string> name, sha256, minOs, minGlibc;
        std::optional<std::int64_t> size;
    };
    std::map<std::string, Package, std::less<>> packages; // only the entries that are objects
    bool packagesIsObject = false;
};

enum class ManifestStatus { Valid, UnknownSchema, Invalid };
struct ManifestResult {
    ManifestStatus status = ManifestStatus::Invalid;
    ReleasePackages packages; // with Valid
};
// The field rules for the manifest of `release`. An unknown schema is told
// apart: that release then reads as having no manifest. A package whose
// fields break the rules is dropped, and so is one whose hash differs from
// its asset's digest.
ManifestResult readManifest(const ManifestFields &fields, const ReleaseEntry &release);
// A release without a manifest: the codebase from the version, offered to
// that codebase alone, the packages its assets named for this version, with
// their digests as hashes (an asset without one gives no package).
ReleasePackages fallbackPackages(const ReleaseEntry &release);
bool offeredTo(const ReleasePackages &packages, Codebase codebase);

// What a check offers.
enum class OfferKind {
    Install,     // download, verify, then "Update ready"
    Notify,      // the notice and the release page
    Requirement, // notify only: the package needs a newer system (min_os / min_glibc)
    Aur,         // the AUR notice: update with the package manager
};
struct UpdateOffer {
    OfferKind kind = OfferKind::Notify;
    UpdatePackage package;   // with Install and Requirement
    std::string requirement; // with Requirement: the version needed
    bool requirementIsOs = false;
};
// `osVersion` is Windows' major.minor.build and `glibcVersion` glibc's; the
// other may be empty. Install only with a package for the kind that has a
// hash and a size within kMaxPackageBytes and fits the system.
UpdateOffer chooseOffer(PackageKind kind, bool windows, const ReleasePackages &packages, std::string_view osVersion,
                        std::string_view glibcVersion);
// Dotted numbers ("10.0.17763", "2.34"): whether `have` is at least `need`.
// An empty `need` asks for nothing; a malformed one, or a malformed `have`,
// is not met.
bool dottedVersionAtLeast(std::string_view have, std::string_view need);

// The whole-download deadline: max(10 min, the size at 100 KiB/s).
std::int64_t downloadDeadlineMs(std::int64_t size);
// 64 hexadecimal characters, either case.
bool isSha256Hex(std::string_view text);

// The NSIS installer's command line in its update mode, `/D=` last and
// unquoted as NSIS reads it. `installDir` has backslashes and no trailing one.
std::string nsisUpdateArguments(bool machine, bool relaunch, long long waitPid, std::string_view installDir);

// The installing side trusts nothing the unprivileged app recorded: it
// downloads the release's SHA256SUMS from the fixed GitHub URL and compares
// the file with the digest listed for its name.
inline constexpr std::string_view kSumsName = "SHA256SUMS";
inline constexpr std::size_t kMaxSumsBytes = 64 * 1024;
// The installing side's exit codes, the Linux helper's too.
inline constexpr int kApplyOk = 0;
inline constexpr int kApplyBadArguments = 2;
inline constexpr int kApplyMismatch = 3;
inline constexpr int kApplyFailed = 4;
inline constexpr int kApplyNoSums = 6;

// A release version: SemVer without a "v" or build metadata, as tags carry
// it, 64 characters at most.
bool isReleaseVersion(std::string_view version);
// https://github.com/altqx/hikari/releases/download/v<V>/SHA256SUMS, or
// empty when `version` is not a release version.
std::string sumsUrl(std::string_view version);
// The digest SHA256SUMS lists for `name` ("<sha256>  <name>" or
// "<sha256> *<name>", CRLF too): 64 lowercase hex digits, listed exactly once.
std::optional<std::string> listedSha256(std::string_view sums, std::string_view name);
enum class InstallCheck { Ok, BadVersion, NotListed, Mismatch };
// A release version newer than the installed one; nothing else.
InstallCheck checkInstallVersion(std::string_view version, std::string_view installed);
// `actual`, the SHA-256 of the file as the installing side read it, against
// the one the release lists for `name`.
InstallCheck checkListedSha256(std::string_view sums, std::string_view name, std::string_view actual);

// --hikari-apply-update <version> <file> <pid> <0|1 relaunch>: the Windows
// installing side's request. `file` is an absolute path on a drive
// ("X:\..."), with no "..", wildcard or stream, and no name that ends in a
// dot or a space.
inline constexpr std::string_view kApplyUpdateSwitch = "--hikari-apply-update";
struct ApplyRequest {
    std::string version;
    std::string file; // UTF-8
    unsigned long pid = 0;
    bool relaunch = false;
};
std::optional<ApplyRequest> parseApplyArguments(const std::vector<std::string> &arguments);
bool isPlainWindowsPath(std::string_view path);

// Without pkexec: the update helper through sudo, which checks the file
// against SHA256SUMS as root; the package manager never gets the user's own
// file, which another process could replace after the app's check.
std::string manualInstallCommand(std::string_view helper, std::string_view file, std::string_view version);

// The AppImage's download, beside the image: hidden, and named for the
// image and the version, so that a later version does not overwrite it and
// another AppImage in the same folder stages its own. "" when appImage is
// not an absolute file path.
std::string appImageStagingPath(std::string_view appImage, std::string_view version);

// What a copy's files in the shared update cache are named for (its
// pending record and markers), so that copies of other kinds, and AppImages
// at other paths, keep their own: the kind's token, and for an AppImage a
// hash of its path too (FNV-1a 64).
std::string updateCopyKey(std::string_view kindToken, std::string_view appImage);
// What's new's name for a copy: every copy by its file (the AppImage, else
// the executable), as copies of one kind in two folders (portable) can run
// different versions; with no file, the kind alone.
std::string whatsNewCopyKey(std::string_view kindToken, std::string_view path);

// The program's own switches; the rest of the command line is left as it is.
struct StartupArguments {
    bool printVersion = false;
    std::string healthFile;  // --hikari-health=<path>
    std::string updatedFrom; // --hikari-updated-from=<version>
    // --export-docs <dir> (or =<dir>); "" when the folder is missing.
    std::optional<std::string> exportDocs;
    std::vector<std::string> rest;
};
StartupArguments parseStartupArguments(const std::vector<std::string> &arguments);

// An automatic check runs only when it is on and the next-check time has
// passed; any finished check moves that time a day on,
// "Remind me in a week" a week.
inline constexpr std::int64_t kUpdateDay = 24 * 60 * 60;
inline constexpr std::int64_t kUpdateWeek = 7 * kUpdateDay;
inline bool automaticCheckDue(bool autoCheck, std::int64_t now, std::int64_t nextCheck)
{
    return autoCheck && now >= nextCheck;
}

} // namespace hikari::application
