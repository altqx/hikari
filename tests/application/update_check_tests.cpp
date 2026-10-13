// P8: the update check against legacy UpdateChecker and SemVer at 20d647c4;
// ADR 0019: candidates, manifests, the fallback and the offer by package kind.

#include "hikari/application/update_check.h"

#include <gtest/gtest.h>

using namespace hikari::application;

namespace {

const std::string kHash(64, 'a');
const std::string kOtherHash(64, 'b');

ReleaseEntry release(std::string tag, std::vector<ReleaseAsset> assets = {}, bool prerelease = false)
{
    ReleaseEntry r;
    r.tag = std::move(tag);
    r.name = r.tag;
    r.prerelease = prerelease;
    r.assets = std::move(assets);
    return r;
}

ManifestFields manifest(const std::string &version, const std::string &codebase = "qt")
{
    ManifestFields f;
    f.schema = 1;
    f.version = version;
    f.codebase = codebase;
    f.offerTo = std::vector<std::string>{codebase};
    f.packagesIsObject = true;
    return f;
}

ManifestFields::Package package(std::string name, std::int64_t size = 100, std::string sha = kHash)
{
    ManifestFields::Package p;
    p.name = std::move(name);
    p.size = size;
    p.sha256 = std::move(sha);
    return p;
}

} // namespace

TEST(SemVer, ParsesAndOrdersLikeLegacy)
{
    ASSERT_TRUE(parseSemVer("v1.2.3"));
    EXPECT_EQ(parseSemVer("1.2.3-rc.1+build.5")->prerelease, "rc.1");
    for (const char *bad : {"1.2", "1.2.3.4", "01.2.3", "1.2.3-01", "1.2.3-", "1.2.3+", "x1.2.3", "v1.2.3-a..b"})
        EXPECT_FALSE(parseSemVer(bad)) << bad;
    EXPECT_TRUE(isNewerVersion("v0.0.2", "0.0.1-rc.1"));
    EXPECT_TRUE(isNewerVersion("0.0.1", "0.0.1-rc.1"));   // a release beats its prerelease
    EXPECT_TRUE(isNewerVersion("0.0.1-rc.2", "0.0.1-rc.1"));
    EXPECT_TRUE(isNewerVersion("1.0.0-rc.10", "1.0.0-rc.9")); // numeric identifiers by value
    EXPECT_TRUE(isNewerVersion("1.0.0-beta", "1.0.0-alpha.1"));
    EXPECT_TRUE(isNewerVersion("1.0.0-alpha.1", "1.0.0-alpha")); // more identifiers win
    EXPECT_TRUE(isNewerVersion("1.0.0-a", "1.0.0-1"));         // alphanumeric above numeric
    EXPECT_FALSE(isNewerVersion("1.0.0+other", "1.0.0"));      // build metadata ignored
    EXPECT_FALSE(isNewerVersion("1.0.0.1", "0.0.1"));          // old four-part tags are no versions
    EXPECT_TRUE(isPrereleaseTag("v1.2.0-rc.1"));
    EXPECT_FALSE(isPrereleaseTag("v1.2.0"));
}

// The candidates are sorted by version, not taken in list order: GitHub's
// list gives no order, and a legacy 0.0.9 published after Qt 0.1.0 comes first.
TEST(UpdateCheck, CandidatesBySemVerSkippingDraftsPrereleasesAndOtherTags)
{
    const std::vector<ReleaseEntry> feed{
        {"v0.3.0", "Draft", "u0", "", true, false, {}},
        {"v0.1.1", "Older", "u4", "", false, false, {}},
        {"v0.2.0-rc.1", "Tagged rc", "u1", "", false, false, {}},
        {"v0.1.5", "Flagged", "u2", "", false, true, {}},
        {"qt-metadata-20261001", "Metadata", "m", "", false, true, {}},
        {"0.1.9", "No v", "nv", "", false, false, {}},
        {"v0.1.8+build", "Build metadata", "bm", "", false, false, {}},
        {"v0.1.2", "Stable", "u3", "notes", false, false, {}},
        {"v0.1.2", "Twice", "u3b", "", false, false, {}},
    };
    const auto stable = updateCandidates(feed, "0.1.0", true);
    ASSERT_EQ(stable.size(), 2u);
    EXPECT_EQ(stable[0].url, "u3"); // the first listed of one version
    EXPECT_EQ(stable[1].url, "u4");
    const auto all = updateCandidates(feed, "0.1.0", false);
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all[0].tag, "v0.2.0-rc.1");
    EXPECT_EQ(all[1].tag, "v0.1.5");
    EXPECT_EQ(findNewerRelease(feed, "0.1.0", true)->url, "u3");
    EXPECT_FALSE(findNewerRelease(feed, "0.1.2", true));
    EXPECT_FALSE(findNewerRelease({}, "0.1.0", false));
    // A prerelease build takes prereleases even with "Stable versions only".
    const auto fromBeta = updateCandidates(feed, "0.1.0-beta.1", true);
    ASSERT_EQ(fromBeta.size(), 4u);
    EXPECT_EQ(fromBeta[0].tag, "v0.2.0-rc.1");
    EXPECT_EQ(fromBeta[3].tag, "v0.1.1");
    EXPECT_TRUE(isPrerelease(feed[2]));
    EXPECT_TRUE(isPrerelease(feed[3]));
    EXPECT_FALSE(isPrerelease(feed[7]));
    EXPECT_TRUE(automaticCheckDue(true, 100, 100));
    EXPECT_FALSE(automaticCheckDue(true, 99, 100));
    EXPECT_FALSE(automaticCheckDue(false, 200, 100));
}

TEST(UpdateCheck, CodebaseFollowsTheVersionCore)
{
    EXPECT_EQ(codebaseOfVersion("0.0.1"), Codebase::Legacy);
    EXPECT_EQ(codebaseOfVersion("v0.0.150-rc.2"), Codebase::Legacy);
    EXPECT_EQ(codebaseOfVersion("0.1.0-beta.2"), Codebase::Qt); // below 0.1.0 by SemVer, Qt by its core
    EXPECT_EQ(codebaseOfVersion("1.0.0"), Codebase::Qt);
    EXPECT_FALSE(codebaseOfVersion("1.0"));
    EXPECT_EQ(parseCodebase("legacy"), Codebase::Legacy);
    EXPECT_EQ(parseCodebase("qt"), Codebase::Qt);
    EXPECT_FALSE(parseCodebase("Qt"));
}

TEST(UpdateCheck, PackageKindMarker)
{
    EXPECT_EQ(parsePackageKind("nsis-user"), PackageKind::NsisUser);
    EXPECT_EQ(parsePackageKind("nsis-machine\n"), PackageKind::NsisMachine);
    EXPECT_EQ(parsePackageKind("appimage\r\n"), PackageKind::AppImage);
    EXPECT_EQ(parsePackageKind("deb"), PackageKind::Deb);
    EXPECT_EQ(parsePackageKind("rpm\n"), PackageKind::Rpm);
    EXPECT_EQ(parsePackageKind("arch\n"), PackageKind::Arch);
    for (const char *bad : {"", "\n", "deb\n\n", " deb", "DEB", "portable-linux", "arch extra"})
        EXPECT_FALSE(parsePackageKind(bad)) << bad;
    EXPECT_EQ(packageKindToken(PackageKind::Portable, true), "portable-windows");
    EXPECT_EQ(packageKindToken(PackageKind::Portable, false), "portable-linux");
    EXPECT_EQ(packageKindToken(PackageKind::Arch, false), "arch");
    EXPECT_EQ(packageKey(PackageKind::NsisMachine, true), "nsis");
    EXPECT_EQ(packageKey(PackageKind::AppImage, false), "appimage");
    EXPECT_EQ(packageKey(PackageKind::Arch, false), "");
    EXPECT_TRUE(installsUpdates(PackageKind::Rpm));
    EXPECT_FALSE(installsUpdates(PackageKind::Arch));
    EXPECT_FALSE(installsUpdates(PackageKind::Portable));
}

TEST(UpdateCheck, AssetNamesTagsAndHosts)
{
    EXPECT_EQ(assetNameFor("nsis", "0.1.0-beta.2"), "HikariSub-0.1.0-beta.2-windows-x64-setup.exe");
    EXPECT_EQ(assetNameFor("portable-windows", "0.1.0"), "HikariSub-0.1.0-windows-x64.zip");
    EXPECT_EQ(assetNameFor("appimage", "0.1.0"), "HikariSub-0.1.0-linux-x86_64.AppImage");
    EXPECT_EQ(assetNameFor("deb", "0.1.0-beta.2"), "hikarisub-0.1.0-beta.2-linux-amd64.deb");
    EXPECT_EQ(assetNameFor("rpm", "0.1.0"), "hikarisub-0.1.0-linux-x86_64.rpm");
    EXPECT_EQ(assetNameFor("portable-linux", "0.1.0"), "hikarisub-0.1.0-linux-x86_64.tar.gz");
    EXPECT_EQ(assetNameFor("arch", "0.1.0"), "");
    EXPECT_TRUE(isValidAssetName("hikarisub-0.1.0-linux-amd64.deb"));
    for (const char *bad : {"", ".hidden", "-x", "a/b", "a~b", "a b", "a%2f", "..", "a\\b"})
        EXPECT_FALSE(isValidAssetName(bad)) << bad;
    EXPECT_TRUE(isValidUrlTag("v0.1.0-rc.1"));
    for (const char *bad : {"", ".", "..", "v1/../x", "v1?x", "v1#x"})
        EXPECT_FALSE(isValidUrlTag(bad)) << bad;
    EXPECT_TRUE(isAllowedDownloadHost("github.com"));
    EXPECT_TRUE(isAllowedDownloadHost("Objects.GitHubUserContent.com"));
    EXPECT_TRUE(isAllowedDownloadHost("release-assets.githubusercontent.com"));
    for (const char *bad : {"api.github.com", "github.com.evil", "evilgithub.com", ""})
        EXPECT_FALSE(isAllowedDownloadHost(bad)) << bad;
}

TEST(UpdateManifest, ValidManifestCrossChecksTheDigests)
{
    const std::string appImage = assetNameFor("appimage", "0.2.0"), deb = assetNameFor("deb", "0.2.0");
    const std::string rpm = assetNameFor("rpm", "0.2.0");
    const auto r = release("v0.2.0", {{appImage, 100, "sha256:" + kHash},
                                      {deb, 100, "sha256:" + kOtherHash}, // differs from the manifest: unusable
                                      {rpm, 100, ""},                     // no digest: the manifest's hash stands
                                      {std::string(kManifestAsset), 900, ""}});
    auto f = manifest("0.2.0");
    f.offerTo = std::vector<std::string>{"qt", "legacy"};
    f.packages["appimage"] = package(appImage);
    f.packages["appimage"].minGlibc = "2.34";
    f.packages["deb"] = package(deb);
    f.packages["rpm"] = package(rpm, 100, std::string(64, 'C')); // either case, kept lowercase
    f.packages["nsis"] = package(assetNameFor("nsis", "0.2.0")); // no such asset in the release
    f.packages["future-kind"] = package("x");                  // unknown keys are ignored
    const auto result = readManifest(f, r);
    ASSERT_EQ(result.status, ManifestStatus::Valid);
    const auto &p = result.packages;
    EXPECT_TRUE(p.fromManifest);
    EXPECT_EQ(p.codebase, Codebase::Qt);
    EXPECT_TRUE(offeredTo(p, Codebase::Qt));
    EXPECT_TRUE(offeredTo(p, Codebase::Legacy));
    ASSERT_EQ(p.packages.size(), 2u);
    EXPECT_EQ(p.packages.at("appimage").minGlibc, "2.34");
    EXPECT_EQ(p.packages.at("rpm").sha256, std::string(64, 'c'));
}

TEST(UpdateManifest, BrokenManifestsAreInvalidAndNewSchemasUnknown)
{
    const auto r = release("v0.2.0");
    auto f = manifest("0.2.0");
    EXPECT_EQ(readManifest(f, r).status, ManifestStatus::Valid);
    auto unknown = f;
    unknown.schema = 2;
    EXPECT_EQ(readManifest(unknown, r).status, ManifestStatus::UnknownSchema);
    std::vector<ManifestFields> broken(9, f);
    broken[0].schema.reset();
    broken[1].version = "0.2.1"; // not the tag's
    broken[2].version.reset();
    broken[3].codebase = "legacy"; // 0.2.0 is Qt's
    broken[4].codebase = "other";
    broken[5].offerTo = std::vector<std::string>{};
    broken[6].offerTo = std::vector<std::string>{"qt", "other"};
    broken[7].offerTo.reset();
    broken[8].packagesIsObject = false;
    for (std::size_t i = 0; i < broken.size(); ++i)
        EXPECT_EQ(readManifest(broken[i], r).status, ManifestStatus::Invalid) << i;
    // A legacy manifest offered to legacy only is valid, and not offered to Qt.
    const auto legacy = readManifest(manifest("0.0.9", "legacy"), release("v0.0.9"));
    ASSERT_EQ(legacy.status, ManifestStatus::Valid);
    EXPECT_FALSE(offeredTo(legacy.packages, Codebase::Qt));
}

TEST(UpdateManifest, PackagesThatBreakTheRulesAreDropped)
{
    const std::string deb = assetNameFor("deb", "0.2.0");
    const auto r = release("v0.2.0", {{deb, 100, ""}, {"other.deb", 100, ""}});
    for (const auto &bad : {package("other.deb"), package(deb, 0), package(deb, -5), package(deb, 100, "abc"),
                            package(deb, 100, std::string(64, 'g'))}) {
        auto f = manifest("0.2.0");
        f.packages["deb"] = bad;
        const auto result = readManifest(f, r);
        ASSERT_EQ(result.status, ManifestStatus::Valid);
        EXPECT_TRUE(result.packages.packages.empty()) << *bad.name;
    }
}

TEST(UpdateManifest, FallbackTakesTheAssetsDigests)
{
    const auto r = release("v0.0.1", {{assetNameFor("portable-linux", "0.0.1"), 50, "sha256:" + kHash},
                                      {assetNameFor("portable-windows", "0.0.1"), 60, "SHA256:" + std::string(64, 'D')},
                                      {assetNameFor("appimage", "0.0.1"), 70, ""},            // no digest
                                      {assetNameFor("deb", "0.0.2"), 70, "sha256:" + kHash}, // another version's
                                      {assetNameFor("rpm", "0.0.1"), 70, "md5:abc"}});
    const auto p = fallbackPackages(r);
    EXPECT_FALSE(p.fromManifest);
    EXPECT_EQ(p.codebase, Codebase::Legacy);
    EXPECT_TRUE(offeredTo(p, Codebase::Legacy));
    EXPECT_FALSE(offeredTo(p, Codebase::Qt));
    ASSERT_EQ(p.packages.size(), 2u);
    EXPECT_EQ(p.packages.at("portable-linux").size, 50);
    EXPECT_EQ(p.packages.at("portable-windows").sha256, std::string(64, 'd'));
}

TEST(UpdateOffer, ByPackageKind)
{
    ReleasePackages p;
    p.offerTo = {Codebase::Qt};
    p.packages["nsis"] = {assetNameFor("nsis", "0.2.0"), 10, kHash, "10.0.17763", ""};
    p.packages["appimage"] = {assetNameFor("appimage", "0.2.0"), 10, kHash, "", "2.34"};
    p.packages["deb"] = {assetNameFor("deb", "0.2.0"), 10, kHash, "", "2.34"};
    p.packages["rpm"] = {assetNameFor("rpm", "0.2.0"), kMaxPackageBytes + 1, kHash, "", ""}; // over 1 GiB

    EXPECT_EQ(chooseOffer(PackageKind::NsisUser, true, p, "10.0.19045", "").kind, OfferKind::Install);
    const auto oldWindows = chooseOffer(PackageKind::NsisMachine, true, p, "10.0.17134", "");
    EXPECT_EQ(oldWindows.kind, OfferKind::Requirement);
    EXPECT_EQ(oldWindows.requirement, "10.0.17763");
    EXPECT_TRUE(oldWindows.requirementIsOs);
    const auto appImage = chooseOffer(PackageKind::AppImage, false, p, "", "2.39");
    EXPECT_EQ(appImage.kind, OfferKind::Install);
    EXPECT_EQ(appImage.package.name, "HikariSub-0.2.0-linux-x86_64.AppImage");
    const auto oldGlibc = chooseOffer(PackageKind::Deb, false, p, "", "2.31");
    EXPECT_EQ(oldGlibc.kind, OfferKind::Requirement);
    EXPECT_FALSE(oldGlibc.requirementIsOs);
    EXPECT_EQ(chooseOffer(PackageKind::Rpm, false, p, "", "2.39").kind, OfferKind::Notify);
    EXPECT_EQ(chooseOffer(PackageKind::Arch, false, p, "", "2.39").kind, OfferKind::Aur);
    EXPECT_EQ(chooseOffer(PackageKind::Portable, false, p, "", "2.39").kind, OfferKind::Notify);
    EXPECT_EQ(chooseOffer(PackageKind::Portable, true, p, "10.0.19045", "").kind, OfferKind::Notify);
    ReleasePackages none;
    EXPECT_EQ(chooseOffer(PackageKind::Deb, false, none, "", "2.39").kind, OfferKind::Notify);
    EXPECT_EQ(chooseOffer(PackageKind::Arch, false, none, "", "").kind, OfferKind::Aur);
}

TEST(UpdateOffer, DottedVersions)
{
    EXPECT_TRUE(dottedVersionAtLeast("2.39", "2.34"));
    EXPECT_TRUE(dottedVersionAtLeast("2.34", "2.34"));
    EXPECT_FALSE(dottedVersionAtLeast("2.31", "2.34"));
    EXPECT_TRUE(dottedVersionAtLeast("10.0.22631", "10.0.17763"));
    EXPECT_TRUE(dottedVersionAtLeast("10.1", "10.0.17763"));
    EXPECT_TRUE(dottedVersionAtLeast("2.34.0", "2.34"));
    EXPECT_TRUE(dottedVersionAtLeast("", ""));
    EXPECT_FALSE(dottedVersionAtLeast("", "2.34"));
    EXPECT_FALSE(dottedVersionAtLeast("2.x", "2.34"));
    EXPECT_FALSE(dottedVersionAtLeast("2.39", "2..34"));
}

TEST(UpdateInstall, DeadlineHashesAndCommands)
{
    EXPECT_EQ(downloadDeadlineMs(0), 10 * 60 * 1000);
    EXPECT_EQ(downloadDeadlineMs(50 * 1024 * 1024), 10 * 60 * 1000);
    EXPECT_GE(downloadDeadlineMs(std::int64_t{1} << 30), (std::int64_t{1} << 30) / (100 * 1024) * 1000);
    EXPECT_TRUE(isSha256Hex(std::string(64, 'F')));
    EXPECT_FALSE(isSha256Hex(std::string(63, 'f')));
    EXPECT_EQ(nsisUpdateArguments(false, true, 42, "C:\\Users\\A B\\AppData\\Local\\Programs\\HikariSub"),
              "/S /UPDATE /WAITPID=42 /RELAUNCH /CURRENTUSER /D=C:\\Users\\A B\\AppData\\Local\\Programs\\HikariSub");
    EXPECT_EQ(nsisUpdateArguments(true, false, 7, "C:\\Program Files\\HikariSub"),
              "/S /UPDATE /WAITPID=7 /ALLUSERS /D=C:\\Program Files\\HikariSub");
    // By hand, the helper checks the file as root, as through pkexec; the
    // package manager is never given the user's file.
    constexpr std::string_view helper = "/opt/hikarisub/libexec/hikarisub-update-helper";
    EXPECT_EQ(manualInstallCommand(helper, "/home/a/.cache/HikariSub/Updates/0.2.0/x.deb", "0.2.0"),
              "sudo /opt/hikarisub/libexec/hikarisub-update-helper install /home/a/.cache/HikariSub/Updates/0.2.0/x.deb 0.2.0");
    EXPECT_EQ(manualInstallCommand(helper, "/home/a b/x.rpm", "0.3.0-beta.1"),
              "sudo /opt/hikarisub/libexec/hikarisub-update-helper install '/home/a b/x.rpm' 0.3.0-beta.1");
    EXPECT_EQ(manualInstallCommand(helper, "/h/it's.rpm", "1.0.0"),
              "sudo /opt/hikarisub/libexec/hikarisub-update-helper install '/h/it'\\''s.rpm' 1.0.0");
}

TEST(UpdateInstall, EachCopyKeepsItsOwnFiles)
{
    // An AppImage's download is named for the image and the version.
    EXPECT_EQ(appImageStagingPath("/home/u/Apps/HikariSub.AppImage", "0.2.0"),
              "/home/u/Apps/.HikariSub.AppImage.0.2.0.update");
    EXPECT_NE(appImageStagingPath("/home/u/Apps/HikariSub-test.AppImage", "0.2.0"),
              appImageStagingPath("/home/u/Apps/HikariSub.AppImage", "0.2.0"));
    EXPECT_EQ(appImageStagingPath("HikariSub.AppImage", "0.2.0"), "");
    EXPECT_EQ(appImageStagingPath("/home/u/Apps/", "0.2.0"), "");
    EXPECT_EQ(appImageStagingPath("", "0.2.0"), "");
    // Records in the shared cache are named for the copy.
    EXPECT_EQ(updateCopyKey("deb", ""), "deb");
    EXPECT_EQ(updateCopyKey("nsis-machine", "/ignored"), "nsis-machine");
    const std::string a = updateCopyKey("appimage", "/home/u/Apps/HikariSub.AppImage");
    EXPECT_EQ(a.size(), 25u);
    EXPECT_EQ(a.rfind("appimage-", 0), 0u);
    EXPECT_NE(a, updateCopyKey("appimage", "/home/u/Apps/HikariSub-test.AppImage"));
    EXPECT_EQ(a, updateCopyKey("appimage", "/home/u/Apps/HikariSub.AppImage"));
}

TEST(UpdateInstall, StartupArguments)
{
    const auto a = parseStartupArguments({"--hikari-updated-from=0.1.0", "episode.ass", "--hikari-health=/c/health-0.2.0"});
    EXPECT_FALSE(a.printVersion);
    EXPECT_EQ(a.updatedFrom, "0.1.0");
    EXPECT_EQ(a.healthFile, "/c/health-0.2.0");
    ASSERT_EQ(a.rest.size(), 1u);
    EXPECT_EQ(a.rest[0], "episode.ass");
    EXPECT_TRUE(parseStartupArguments({"--version"}).printVersion);
    EXPECT_TRUE(parseStartupArguments({}).rest.empty());
}

// ADR 0019: what installs an update checks the version it is asked for and
// the file against the release's SHA256SUMS, and nothing the app recorded.
TEST(UpdateVerify, releaseVersionsOnly)
{
    EXPECT_TRUE(isReleaseVersion("0.2.0"));
    EXPECT_TRUE(isReleaseVersion("0.2.0-beta.1"));
    for (const char *bad : {"", "v0.2.0", "0.2.0+build.1", "01.2.0", "0.2", "0.2.0.1", "0.2.0;id", "0.2.0 ",
                            "0.2.0/../x", "../0.2.0", "0.2.0-01"})
        EXPECT_FALSE(isReleaseVersion(bad)) << bad;
    EXPECT_FALSE(isReleaseVersion(std::string(70, '1')));

    EXPECT_EQ(checkInstallVersion("0.2.0", "0.1.0"), InstallCheck::Ok);
    EXPECT_EQ(checkInstallVersion("0.1.0", "0.0.9"), InstallCheck::Ok);
    // The same, an older or a malformed version: an older release cannot be
    // put back through the installing side.
    EXPECT_EQ(checkInstallVersion("0.2.0", "0.2.0"), InstallCheck::BadVersion);
    EXPECT_EQ(checkInstallVersion("0.1.0", "0.2.0"), InstallCheck::BadVersion);
    EXPECT_EQ(checkInstallVersion("0.2.0-rc.1", "0.2.0"), InstallCheck::BadVersion);
    EXPECT_EQ(checkInstallVersion("v0.3.0", "0.2.0"), InstallCheck::BadVersion);
}

TEST(UpdateVerify, sumsUrlIsBuiltFromTheVersionOnly)
{
    EXPECT_EQ(sumsUrl("0.2.0"), "https://github.com/altqx/hikari/releases/download/v0.2.0/SHA256SUMS");
    EXPECT_EQ(sumsUrl("0.2.0-rc.1"), "https://github.com/altqx/hikari/releases/download/v0.2.0-rc.1/SHA256SUMS");
    EXPECT_TRUE(sumsUrl("0.2.0/../../evil").empty());
    EXPECT_TRUE(sumsUrl("0.2.0?x=1").empty());
}

TEST(UpdateVerify, findsTheDigestListedOnceForTheName)
{
    const std::string setup = "HikariSub-0.2.0-windows-x64-setup.exe";
    const std::string sums = kHash + "  " + setup + "\n" + kOtherHash + "  hikarisub-0.2.0-linux-amd64.deb\n";
    EXPECT_EQ(listedSha256(sums, setup), kHash);
    EXPECT_EQ(listedSha256(sums, "hikarisub-0.2.0-linux-amd64.deb"), kOtherHash);
    EXPECT_EQ(listedSha256(kHash + " *" + setup + "\r\n", setup), kHash);
    EXPECT_FALSE(listedSha256(sums, "HikariSub-0.2.0-windows-x64-setup"));
    EXPECT_FALSE(listedSha256(kHash + "  x" + setup + "\n", setup));
    EXPECT_FALSE(listedSha256(sums + kHash + "  " + setup + "\n", setup)); // twice
    EXPECT_FALSE(listedSha256(std::string(64, 'A') + "  " + setup + "\n", setup));
    EXPECT_FALSE(listedSha256(kHash + " " + setup + "\n", setup));
    EXPECT_FALSE(listedSha256("", setup));
    EXPECT_FALSE(listedSha256(sums + std::string(kMaxSumsBytes, '\n'), setup));
}

TEST(UpdateVerify, theCallersDigestHasNoSay)
{
    const std::string setup = "HikariSub-0.2.0-windows-x64-setup.exe";
    // The release lists kHash; the caller's forged file hashes to
    // kOtherHash, which its pending record would carry. Only the list decides.
    EXPECT_EQ(checkListedSha256(kHash + "  " + setup + "\n", setup, kOtherHash), InstallCheck::Mismatch);
    EXPECT_EQ(checkListedSha256(kHash + "  " + setup + "\n", setup, kHash), InstallCheck::Ok);
    EXPECT_EQ(checkListedSha256(kOtherHash + "  HikariSub-0.2.0-windows-x64.zip\n" + kHash + "  " + setup + "\n", setup,
                                kOtherHash),
              InstallCheck::Mismatch);
    EXPECT_EQ(checkListedSha256(kOtherHash + "  hikarisub-0.2.0-linux-amd64.deb\n", setup, kOtherHash),
              InstallCheck::NotListed);
}

TEST(UpdateVerify, applyArgumentsAreAVersionAFileAPidAndAFlag)
{
    const auto request = parseApplyArguments({"0.2.0", "C:\\Users\\a b\\x.exe", "4242", "1"});
    ASSERT_TRUE(request);
    EXPECT_EQ(request->version, "0.2.0");
    EXPECT_EQ(request->file, "C:\\Users\\a b\\x.exe");
    EXPECT_EQ(request->pid, 4242u);
    EXPECT_TRUE(request->relaunch);
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "4242", "1", kHash})); // no digest taken
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "4242"}));
    EXPECT_FALSE(parseApplyArguments({"v0.2.0", "C:\\x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "\\\\server\\share\\x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\a\\..\\x.exe", "4242", "0"}));
    // Windows drops a name's trailing spaces and dots: "C:\a\.. \x.exe" is "C:\x.exe".
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\a\\.. \\x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\a.\\x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe ", "4242", "0"}));
    EXPECT_TRUE(parseApplyArguments({"0.2.0", "C:\\a.b\\x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe:stream", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:/x.exe", "4242", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "0", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "-1", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "99999999999", "0"}));
    EXPECT_FALSE(parseApplyArguments({"0.2.0", "C:\\x.exe", "4242", "yes"}));
}
