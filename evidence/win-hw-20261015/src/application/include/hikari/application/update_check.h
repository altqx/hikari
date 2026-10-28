#pragma once

// The update check and its choice (docs/qt/distribution.md, ADR 0019). It
// reads the GitHub release list and each candidate's update manifest, or a
// release's own asset digests when it has no manifest; keeps the releases
// offered to this codebase; and chooses, by the install's package kind,
// between installing the matching package, notifying only and the AUR
// notice. Everything here is pure: the network, files and processes are the
// app layer's (UpdateChecker, UpdateInstaller).

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

// The codebase a release belongs to: below 0.1.0 the legacy wx app, from
// 0.1.0 on this one.
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

// What installs an update decides by itself that the package is the
// release's own file (ADR 0019): nothing the unprivileged app recorded,
// the pending record and its SHA-256 included, is trusted there. It takes a
// version and a file, downloads that release's SHA256SUMS from the fixed
// GitHub URL, and compares the file with the digest listed for the release
// file's own name. The Linux update helper does the same in shell.
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

// Without pkexec: the command that installs the downloaded file, the update
// helper through sudo. The helper checks the file against the release's
// SHA256SUMS as root, as it does through pkexec; the package manager is
// never given the user's own file, which another process of the user's
// could replace after the app's check.
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

// The program's own switches; the rest of the command line is left as it is.
struct StartupArguments {
    bool printVersion = false;
    std::string healthFile;  // --hikari-health=<path>
    std::string updatedFrom; // --hikari-updated-from=<version>
    std::vector<std::string> rest;
};
StartupArguments parseStartupArguments(const std::vector<std::string> &arguments);

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
