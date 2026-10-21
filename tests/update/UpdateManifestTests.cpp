//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

// The installers spec's update rules: package kinds, the manifest, which
// release a check offers and how, and the records kept between starts.

#include "../check.h"
#include "../../HikariSub/PackageKind.h"
#include "../../HikariSub/Sha256.h"
#include "../../HikariSub/UpdateManifest.h"
#include "../../HikariSub/UpdatePolicy.h"

#include <string>
#include <vector>

using namespace update_manifest;
using package_kind::Flow;
using update_policy::Candidates;
using update_policy::ListCandidates;
using update_policy::ReleaseAsset;

namespace {

const std::string kHashA(64, 'a');
const std::string kHashB(64, 'b');

ReleaseAsset Asset(const std::string &name, long long size, const std::string &hash)
{
	return ReleaseAsset{ name, size, hash.empty() ? std::string() : "sha256:" + hash };
}

Release WithAssets(const char *tag, bool prerelease, std::vector<ReleaseAsset> assets)
{
	Release release{ tag, false, prerelease };
	release.assets = std::move(assets);
	return release;
}

// A schema 1 manifest for version, offered to offerTo (a JSON array), with
// one deb package of the given hash.
std::string ManifestJson(const std::string &version, const std::string &codebase, const std::string &offerTo,
	const std::string &hash = kHashA, const std::string &extra = "")
{
	return "{\"schema\": 1, \"product\": \"HikariSub\", \"version\": \"" + version + "\", \"codebase\": \"" +
		codebase + "\", \"offer_to\": " + offerTo + ", \"published\": \"2026-11-02T10:00:00Z\", \"packages\": {"
		"\"deb\": {\"name\": \"hikarisub-" + version + "-linux-amd64.deb\", \"size\": 1000, \"sha256\": \"" + hash +
		"\", \"min_glibc\": \"2.35\"}" + extra + "}}";
}

ManifestRead Read(const std::string &json, const std::string &version, Manifest *manifest = nullptr)
{
	Manifest scratch;
	return ReadManifest(JsonValue::Parse(json), version, manifest ? manifest : &scratch);
}

} // namespace

TEST(sha256_known_answers)
{
	CHECK(Sha256::Of("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Sha256::Of("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	std::string twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	CHECK(Sha256::Of(twoBlocks.data(), twoBlocks.size()) ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	// Fed a piece at a time, a million "a"s in uneven pieces.
	Sha256 hash;
	std::string piece(997, 'a');
	size_t left = 1000000;
	while (left > 0) {
		size_t take = left < piece.size() ? left : piece.size();
		hash.Update(piece.data(), take);
		left -= take;
	}
	CHECK(hash.HexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	// The hash is reset after a digest.
	CHECK(hash.HexDigest() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(package_kind_marker)
{
	using package_kind::Kind;
	using package_kind::ParseMarker;
	CHECK(ParseMarker("nsis-user") == Kind::NsisUser);
	CHECK(ParseMarker("nsis-machine\n") == Kind::NsisMachine);
	CHECK(ParseMarker("appimage\r\n") == Kind::AppImage);
	CHECK(ParseMarker("deb\n") == Kind::Deb);
	CHECK(ParseMarker("rpm") == Kind::Rpm);
	CHECK(ParseMarker("arch\n") == Kind::Arch);
	// One token and one line ending, nothing else.
	CHECK(!ParseMarker(""));
	CHECK(!ParseMarker("\n"));
	CHECK(!ParseMarker("deb\n\n"));
	CHECK(!ParseMarker(" deb"));
	CHECK(!ParseMarker("deb "));
	CHECK(!ParseMarker("DEB"));
	CHECK(!ParseMarker("portable-linux"));
	CHECK(!ParseMarker("flatpak"));
	for (Kind kind : { Kind::NsisUser, Kind::NsisMachine, Kind::AppImage, Kind::Deb, Kind::Rpm, Kind::Arch })
		CHECK(package_kind::ParseToken(package_kind::Token(kind)) == kind);
}

TEST(package_kind_resolves_at_run_time)
{
	using package_kind::Kind;
	using package_kind::Resolve;
	CHECK(Resolve(std::nullopt, true, false) == Kind::PortableWindows);
	CHECK(Resolve(std::nullopt, false, true) == Kind::PortableLinux);
	CHECK(Resolve(Kind::NsisMachine, true, false) == Kind::NsisMachine);
	CHECK(Resolve(Kind::Deb, false, false) == Kind::Deb);
	CHECK(Resolve(Kind::Arch, false, false) == Kind::Arch);
	// An AppImage only with $APPIMAGE naming a file: an extracted AppDir
	// is portable.
	CHECK(Resolve(Kind::AppImage, false, true) == Kind::AppImage);
	CHECK(Resolve(Kind::AppImage, false, false) == Kind::PortableLinux);
	// The other platform's marker is a copied tree.
	CHECK(Resolve(Kind::NsisUser, false, false) == Kind::PortableLinux);
	CHECK(Resolve(Kind::Rpm, true, false) == Kind::PortableWindows);

	// The data root moves for any marker, the AppImage's included.
	CHECK(package_kind::IsInstalled(Kind::AppImage));
	CHECK(!package_kind::IsInstalled(std::nullopt));
}

TEST(package_kind_flows)
{
	using package_kind::Kind;
	using package_kind::FlowOf;
	using package_kind::PackageKey;
	CHECK(FlowOf(Kind::NsisUser) == Flow::Install);
	CHECK(FlowOf(Kind::NsisMachine) == Flow::Install);
	CHECK(FlowOf(Kind::AppImage) == Flow::Install);
	CHECK(FlowOf(Kind::Deb) == Flow::Install);
	CHECK(FlowOf(Kind::Rpm) == Flow::Install);
	CHECK(FlowOf(Kind::Arch) == Flow::AurNotice);
	CHECK(FlowOf(Kind::PortableWindows) == Flow::Notify);
	CHECK(FlowOf(Kind::PortableLinux) == Flow::Notify);
	CHECK(PackageKey(Kind::NsisUser) == "nsis");
	CHECK(PackageKey(Kind::NsisMachine) == "nsis");
	CHECK(PackageKey(Kind::AppImage) == "appimage");
	CHECK(PackageKey(Kind::Deb) == "deb");
	CHECK(PackageKey(Kind::Rpm) == "rpm");
	// The AUR install takes no package: there is no "arch" key.
	CHECK(PackageKey(Kind::Arch).empty());
	CHECK(PackageKey(Kind::PortableLinux).empty());
	CHECK(!package_kind::HasAutoInstallOption(Kind::Arch));
	CHECK(!package_kind::HasAutoInstallOption(Kind::PortableWindows));
	CHECK(package_kind::HasAutoInstallOption(Kind::Rpm));
	CHECK(package_kind::AboutName(Kind::Arch) == "AUR");
	CHECK(package_kind::AboutName(Kind::AppImage) == "AppImage");
}

// The two AUR packages (installers spec, 4.5 and 5.5): aur-channel.txt
// decides which releases an AUR install is told about.
TEST(update_aur_channel)
{
	using package_kind::Kind;
	using package_kind::AurChannel;
	using package_kind::ParseAurChannel;
	CHECK(ParseAurChannel("beta\n") == AurChannel::Beta);
	CHECK(ParseAurChannel("stable") == AurChannel::Stable);
	CHECK(ParseAurChannel("stable\r\n") == AurChannel::Stable);
	CHECK(!ParseAurChannel(""));
	CHECK(!ParseAurChannel("Beta\n"));
	CHECK(!ParseAurChannel("beta\n\n"));
	CHECK(!ParseAurChannel("arch\n"));

	using package_kind::StableOnly;
	using update_policy::SkipsPrereleases;
	// hikarisub-beta-bin on a stable version is told about the next
	// prerelease, whatever the option says; hikarisub-stable-bin is not,
	// whatever the option says.
	CHECK(!SkipsPrereleases(StableOnly(true, Kind::Arch, AurChannel::Beta), "0.0.2"));
	CHECK(SkipsPrereleases(StableOnly(false, Kind::Arch, AurChannel::Stable), "0.0.2"));
	CHECK(!SkipsPrereleases(StableOnly(true, Kind::Arch, AurChannel::Beta), "0.0.2-rc.1"));
	// Without a channel (a PKGBUILD from before the two packages), and for
	// every other kind, the option decides.
	CHECK(SkipsPrereleases(StableOnly(true, Kind::Arch, std::nullopt), "0.0.2"));
	CHECK(!SkipsPrereleases(StableOnly(false, Kind::Arch, std::nullopt), "0.0.2"));
	CHECK(SkipsPrereleases(StableOnly(true, Kind::Deb, AurChannel::Beta), "0.0.2"));
	CHECK(!package_kind::HasStableOnlyOption(Kind::Arch, AurChannel::Beta));
	CHECK(!package_kind::HasStableOnlyOption(Kind::Arch, AurChannel::Stable));
	CHECK(package_kind::HasStableOnlyOption(Kind::Arch, std::nullopt));
	CHECK(package_kind::HasStableOnlyOption(Kind::AppImage, std::nullopt));

	// The notice says the package carries stable versions only just when
	// that is so.
	using package_kind::AurCarries;
	CHECK(AurCarries(false, AurChannel::Stable));
	CHECK(AurCarries(true, AurChannel::Beta));
	CHECK(!AurCarries(true, AurChannel::Stable));
	CHECK(!AurCarries(true, std::nullopt));
	CHECK(AurCarries(false, std::nullopt));
}

TEST(update_asset_names)
{
	CHECK(AssetName("nsis", "0.1.0-beta.2") == "HikariSub-0.1.0-beta.2-windows-x64-setup.exe");
	CHECK(AssetName("portable-windows", "0.0.2") == "HikariSub-0.0.2-windows-x64.zip");
	CHECK(AssetName("appimage", "0.0.2") == "HikariSub-0.0.2-linux-x86_64.AppImage");
	CHECK(AssetName("deb", "0.1.0-beta.2") == "hikarisub-0.1.0-beta.2-linux-amd64.deb");
	CHECK(AssetName("rpm", "0.0.2") == "hikarisub-0.0.2-linux-x86_64.rpm");
	CHECK(AssetName("portable-linux", "0.0.2") == "hikarisub-0.0.2-linux-x86_64.tar.gz");
	CHECK(AssetName("arch", "0.0.2").empty());
	for (std::string_view key : kPackageKeys)
		CHECK(IsAssetName(AssetName(key, "0.1.0-rc.1")));

	CHECK(IsAssetName("SHA256SUMS"));
	CHECK(IsAssetName("hikarisub-update.json"));
	CHECK(!IsAssetName(""));
	CHECK(!IsAssetName(".hidden"));
	CHECK(!IsAssetName("-dash"));
	CHECK(!IsAssetName("hikarisub_0.1.0~beta.2-1_amd64.deb"));
	CHECK(!IsAssetName("a/b"));
	CHECK(!IsAssetName("a b"));
	CHECK(!IsAssetName("..%2f"));
}

TEST(update_digests_and_dotted_versions)
{
	CHECK(DigestSha256("sha256:" + kHashA) == kHashA);
	CHECK(DigestSha256("sha256:" + std::string(64, 'A')) == kHashA);
	CHECK(DigestSha256("sha1:" + kHashA).empty());
	CHECK(DigestSha256("sha256:" + kHashA.substr(1)).empty());
	CHECK(DigestSha256("").empty());
	CHECK(IsSha256(kHashA));
	CHECK(!IsSha256(std::string(64, 'A')));
	CHECK(!IsSha256(std::string(64, 'g')));

	CHECK(CompareDotted("10.0.17763", "10.0.17763") == 0);
	CHECK(CompareDotted("10.0.19045", "10.0.17763") > 0);
	CHECK(CompareDotted("6.3.9600", "10.0.17763") < 0);
	CHECK(CompareDotted("2.35", "2.34") > 0);
	CHECK(CompareDotted("2.4", "2.34") < 0);
	CHECK(CompareDotted("2.35", "2.35.0") == 0);
	CHECK(CompareDotted("2.040", "2.40") == 0);
	CHECK(CompareDotted("", "2.34") < 0);
	CHECK(CompareDotted("2.34", "x") > 0);
	CHECK(!IsDottedVersion("2..34"));
	CHECK(!IsDottedVersion(".2"));
	CHECK(!IsDottedVersion("2.34-r1"));
}

TEST(update_manifest_reads_schema_1)
{
	Manifest manifest;
	std::string json = ManifestJson("0.1.0-beta.2", "qt", "[\"qt\", \"legacy\"]", kHashA,
		", \"arch\": {\"whatever\": 1}, \"nsis\": {\"name\": \"HikariSub-0.1.0-beta.2-windows-x64-setup.exe\", "
		"\"size\": 123456789, \"sha256\": \"" + kHashB + "\", \"min_os\": \"10.0.17763\", \"later\": [1]}");
	CHECK(Read(json, "0.1.0-beta.2", &manifest) == ManifestRead::Valid);
	CHECK(manifest.version == "0.1.0-beta.2");
	CHECK(manifest.codebase == Codebase::Qt);
	CHECK(manifest.OffersTo(Codebase::Qt));
	CHECK(manifest.OffersTo(Codebase::Legacy));
	// The unknown "arch" key is ignored, and so is "later".
	CHECK(manifest.packages.size() == 2);
	CHECK(manifest.packages.at("deb").size == 1000);
	CHECK(manifest.packages.at("deb").minGlibc == "2.35");
	CHECK(manifest.packages.at("nsis").sha256 == kHashB);
	CHECK(manifest.packages.at("nsis").minOs == "10.0.17763");

	Manifest legacy;
	CHECK(Read(ManifestJson("0.0.2", "legacy", "[\"legacy\"]"), "0.0.2", &legacy) == ManifestRead::Valid);
	CHECK(legacy.OffersTo(Codebase::Legacy));
	CHECK(!legacy.OffersTo(Codebase::Qt));
}

TEST(update_manifest_refuses_what_does_not_hold)
{
	CHECK(Read(ManifestJson("0.0.2", "legacy", "[\"legacy\"]"), "0.0.3") == ManifestRead::Invalid);
	// The codebase agrees with the version.
	CHECK(Read(ManifestJson("0.0.2", "qt", "[\"qt\"]"), "0.0.2") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "legacy", "[\"legacy\"]"), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "gtk", "[\"qt\"]"), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "qt", "[]"), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "qt", "\"qt\""), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "qt", "[\"qt\", \"everyone\"]"), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "qt", "[\"qt\"]", "ABC"), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read(ManifestJson("0.1.0", "qt", "[\"qt\"]", std::string(64, 'A')), "0.1.0") == ManifestRead::Invalid);
	CHECK(Read("[]", "0.1.0") == ManifestRead::Invalid);
	CHECK(Read("", "0.1.0") == ManifestRead::Invalid);
	CHECK(Read("{\"schema\": \"1\"}", "0.1.0") == ManifestRead::Invalid);
	CHECK(Read("{\"schema\": 1.5}", "0.1.0") == ManifestRead::Invalid);

	// A package that is not as 2.1 names it, too large, or of no size.
	auto withDeb = [](const std::string &deb) {
		return "{\"schema\": 1, \"version\": \"0.0.2\", \"codebase\": \"legacy\", \"offer_to\": [\"legacy\"], "
			"\"packages\": {\"deb\": " + deb + "}}";
	};
	std::string hash = "\"sha256\": \"" + kHashA + "\"";
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 5, " + hash + "}"), "0.0.2") ==
		ManifestRead::Valid);
	CHECK(Read(withDeb("{\"name\": \"other.deb\", \"size\": 5, " + hash + "}"), "0.0.2") == ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"../hikarisub-0.0.2-linux-amd64.deb\", \"size\": 5, " + hash + "}"), "0.0.2") ==
		ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 1073741825, " + hash + "}"),
		"0.0.2") == ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 1073741824, " + hash + "}"),
		"0.0.2") == ManifestRead::Valid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 0, " + hash + "}"), "0.0.2") ==
		ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 5.5, " + hash + "}"), "0.0.2") ==
		ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", " + hash + "}"), "0.0.2") ==
		ManifestRead::Invalid);
	CHECK(Read(withDeb("{\"name\": \"hikarisub-0.0.2-linux-amd64.deb\", \"size\": 5, " + hash +
		", \"min_glibc\": 2.35}"), "0.0.2") == ManifestRead::Invalid);
}

TEST(update_manifest_of_an_unknown_schema_is_no_manifest)
{
	CHECK(Read("{\"schema\": 2, \"anything\": true}", "0.1.0") == ManifestRead::UnknownSchema);

	// The release then counts as one without a manifest, its assets'
	// digests standing in.
	Release release = WithAssets("v0.0.3", false, { Asset("hikarisub-update.json", 50, kHashB),
		Asset("hikarisub-0.0.3-linux-amd64.deb", 1000, kHashA) });
	std::string text = "{\"schema\": 2}";
	std::optional<Manifest> admitted = Admit(release, &text);
	CHECK(admitted.has_value());
	CHECK(admitted && admitted->packages.at("deb").sha256 == kHashA);
}

TEST(update_fallback_reads_the_assets)
{
	// v0.0.1's assets: the zip and the tar.gz, with GitHub's digests.
	Manifest manifest = FallbackManifest("0.0.1", {
		Asset("HikariSub-0.0.1-windows-x64.zip", 1000, kHashA),
		Asset("hikarisub-0.0.1-linux-x86_64.tar.gz", 2000, std::string(64, 'C')),
		Asset("SHA256SUMS", 300, kHashB),
		// Not the name 2.1 gives this version.
		Asset("hikarisub-0.0.2-linux-amd64.deb", 1000, kHashA),
		// No digest, no package.
		Asset("HikariSub-0.0.1-linux-x86_64.AppImage", 1000, ""),
	});
	CHECK(manifest.codebase == Codebase::Legacy);
	CHECK(manifest.OffersTo(Codebase::Legacy));
	CHECK(!manifest.OffersTo(Codebase::Qt));
	CHECK(manifest.packages.size() == 2);
	CHECK(manifest.packages.at("portable-windows").size == 1000);
	CHECK(manifest.packages.at("portable-linux").sha256 == std::string(64, 'c'));

	// A Qt release without a manifest is offered to Qt only.
	Manifest qt = FallbackManifest("0.1.0-beta.1", {});
	CHECK(qt.codebase == Codebase::Qt);
	CHECK(qt.OffersTo(Codebase::Qt));
	CHECK(!qt.OffersTo(Codebase::Legacy));
}

TEST(update_cross_check_drops_packages_github_disagrees_with)
{
	std::string version = "0.0.3";
	std::string json = ManifestJson(version, "legacy", "[\"legacy\"]", kHashA,
		", \"rpm\": {\"name\": \"hikarisub-0.0.3-linux-x86_64.rpm\", \"size\": 1000, \"sha256\": \"" + kHashA + "\"}"
		", \"appimage\": {\"name\": \"HikariSub-0.0.3-linux-x86_64.AppImage\", \"size\": 1000, \"sha256\": \"" +
		kHashA + "\"}"
		", \"portable-linux\": {\"name\": \"hikarisub-0.0.3-linux-x86_64.tar.gz\", \"size\": 1000, \"sha256\": \"" +
		kHashA + "\"}");
	Release release = WithAssets("v0.0.3", false, {
		Asset("hikarisub-update.json", 50, ""),
		// Agrees.
		Asset("hikarisub-0.0.3-linux-amd64.deb", 1000, kHashA),
		// A digest that differs.
		Asset("hikarisub-0.0.3-linux-x86_64.rpm", 1000, kHashB),
		// Another size.
		Asset("HikariSub-0.0.3-linux-x86_64.AppImage", 999, kHashA),
		// No digest: the manifest's hash stands.
		Asset("hikarisub-0.0.3-linux-x86_64.tar.gz", 1000, ""),
	});
	std::optional<Manifest> manifest = Admit(release, &json);
	CHECK(manifest.has_value());
	if (manifest) {
		CHECK(manifest->packages.count("deb") == 1);
		CHECK(manifest->packages.count("rpm") == 0);
		CHECK(manifest->packages.count("appimage") == 0);
		CHECK(manifest->packages.count("portable-linux") == 1);
	}

	// A package whose asset is not on the release at all.
	Release missing = WithAssets("v0.0.3", false, { Asset("hikarisub-update.json", 50, "") });
	manifest = Admit(missing, &json);
	CHECK(manifest && manifest->packages.empty());
}

TEST(update_admit_honours_offer_to)
{
	auto release = [](const char *tag, bool pre) {
		return WithAssets(tag, pre, { Asset("hikarisub-update.json", 50, "") });
	};
	std::string qtOnly = ManifestJson("0.1.0", "qt", "[\"qt\"]");
	std::string both = ManifestJson("0.1.0", "qt", "[\"qt\", \"legacy\"]");
	CHECK(!Admit(release("v0.1.0", false), &qtOnly));
	CHECK(Admit(release("v0.1.0", false), &qtOnly, Codebase::Qt));
	CHECK(Admit(release("v0.1.0", false), &both));
	// The same for a prerelease: a legacy beta tester sees no Qt beta
	// until the gate opens.
	std::string beta = ManifestJson("0.1.0-beta.1", "qt", "[\"qt\"]");
	CHECK(!Admit(release("v0.1.0-beta.1", true), &beta));

	// A manifest that failed to come, too large or broken rules the
	// release out; it is never bypassed by the fallback.
	CHECK(!Admit(release("v0.1.0", false), nullptr));
	std::string large(kMaxManifestBytes + 1, ' ');
	CHECK(!Admit(release("v0.1.0", false), &large));
	std::string broken = "{\"schema\": 1";
	CHECK(!Admit(release("v0.1.0", false), &broken));
	// A legacy release without a manifest is admitted by the version rule,
	// a Qt one is not.
	CHECK(Admit(WithAssets("v0.0.2", false, {}), nullptr));
	CHECK(!Admit(WithAssets("v0.1.0", false, {}), nullptr));
	// A tag that cannot be built into a URL is no candidate.
	CHECK(!Admit(WithAssets("0.0.2", false, {}), nullptr));
	CHECK(!Admit(WithAssets("v0.0.2+build.1", false, {}), nullptr));
}

TEST(update_candidates_are_newest_first_and_capped)
{
	using update_policy::Release;
	std::vector<Release> releases = {
		Release{ "v0.0.2", false, false },
		Release{ "qt-metadata-linux-6.11.2", false, true },
		Release{ "v0.1.0-beta.1", false, true },
		Release{ "v0.0.4", true, false },
		Release{ "v0.0.3", false, false },
		Release{ "v0.0.1", false, false },
		Release{ "0.0.9", false, false },
		Release{ "v0.0.8+meta", false, false },
		Release{ "v0.1.0", false, false },
	};
	Candidates stable = ListCandidates(releases, "0.0.1", true);
	CHECK(stable.offer == (std::vector<int>{ 8, 4, 0 }));
	CHECK(stable.skipped == (std::vector<int>{ 2 }));

	Candidates all = ListCandidates(releases, "0.0.1", false);
	CHECK(all.offer == (std::vector<int>{ 8, 2, 4, 0 }));
	CHECK(all.skipped.empty());

	// An rc build takes prereleases whatever the option.
	Candidates rc = ListCandidates(releases, "0.0.2-rc.1", true);
	CHECK(rc.offer == (std::vector<int>{ 8, 2, 4, 0 }));

	std::vector<Release> many;
	for (int i = 2; i < 12; i++)
		many.push_back(Release{ "v0.0." + std::to_string(i), false, false });
	Candidates capped = ListCandidates(many, "0.0.1", true);
	CHECK(capped.offer.size() == update_policy::kMaxCandidates);
	CHECK(capped.offer.front() == 9);

	// One version twice: the first listed comes first.
	std::vector<Release> twice = { Release{ "v0.0.2", false, false }, Release{ "v0.0.3", false, false },
		Release{ "v0.0.3", false, false } };
	CHECK(ListCandidates(twice, "0.0.1", true).offer == (std::vector<int>{ 1, 2, 0 }));
	CHECK(ListCandidates(twice, "nonsense", true).offer.empty());
}

TEST(update_walk_offers_the_first_admitted_candidate)
{
	// A legacy client: Qt 0.1.0 is not offered to it, so 0.0.3 is.
	std::vector<Release> releases = {
		WithAssets("v0.1.0", false, { Asset("hikarisub-update.json", 50, "") }),
		WithAssets("v0.0.3", false, { Asset("hikarisub-0.0.3-linux-amd64.deb", 10, kHashA) }),
		WithAssets("v0.0.2", false, {}),
	};
	std::string qtOnly = ManifestJson("0.1.0", "qt", "[\"qt\"]");
	CandidateWalk walk(releases, ListCandidates(releases, "0.0.1", true));
	CHECK(walk.Current() == 0);
	CHECK(!walk.CurrentIsSkipped());
	CHECK(!Admit(releases[0], &qtOnly));
	walk.Skip();
	CHECK(walk.Current() == 1);
	CHECK(Admit(releases[1], nullptr));
	walk.Take();
	CHECK(walk.Found());
	CHECK(walk.Current() == -1);

	// The prereleases kept out come after the others, within the budget.
	std::vector<Release> pre = { Release{ "v0.0.3-rc.1", false, true }, Release{ "v0.0.2", false, false } };
	CandidateWalk preWalk(pre, ListCandidates(pre, "0.0.1", true));
	CHECK(preWalk.Current() == 1);
	preWalk.Skip();
	CHECK(preWalk.Current() == 0);
	CHECK(preWalk.CurrentIsSkipped());
	preWalk.Skip();
	CHECK(preWalk.Current() == -1);
	CHECK(!preWalk.Found());

	std::vector<Release> many;
	for (int i = 2; i < 12; i++)
		many.push_back(Release{ "v0.0." + std::to_string(i) + "-rc.1", false, true });
	many.push_back(Release{ "v0.0.2", false, false });
	CandidateWalk capped(many, ListCandidates(many, "0.0.1", true));
	int looked = 0;
	while (capped.Current() >= 0) {
		looked++;
		capped.Skip();
	}
	CHECK(looked == int(update_policy::kMaxCandidates));
}

TEST(update_candidate_details_follow_the_candidates)
{
	using update_policy::ReleasePages;
	ReleasePages pages("0.0.1", true);
	update_policy::CandidateDetails<std::string> details;
	int reads = 0;
	auto add = [&](const std::vector<Release> &page) {
		int number = pages.NextPage();
		pages.Add(page);
		details.Keep(pages, [&](size_t index) {
			reads++;
			return std::to_string(number) + ":" + page[index].tag;
		});
	};
	std::vector<Release> first;
	for (size_t i = 0; i < update_policy::kReleasesPerPage - 2; i++)
		first.push_back(Release{ "qt-metadata-" + std::to_string(i), false, true });
	first.push_back(Release{ "v0.0.2", false, false });
	first.push_back(Release{ "v0.0.3-rc.1", false, true });
	add(first);
	CHECK(reads == 2);
	CHECK(details.Size() == 2);
	CHECK(*details.Find(int(update_policy::kReleasesPerPage) - 2) == "1:v0.0.2");
	add({ Release{ "v0.0.3", false, false }, Release{ "v0.0.1", false, false } });
	CHECK(reads == 3);
	CHECK(details.Size() == 3);
	CHECK(*details.Find(int(update_policy::kReleasesPerPage)) == "2:v0.0.3");
	CHECK(*details.Find(int(update_policy::kReleasesPerPage) - 2) == "1:v0.0.2");
	CHECK(details.Find(int(update_policy::kReleasesPerPage) + 1) == nullptr);
}

TEST(update_decides_by_kind)
{
	using package_kind::Kind;
	Manifest manifest;
	manifest.packages["deb"] = Package{ "hikarisub-0.0.3-linux-amd64.deb", 10, kHashA, "", "2.35" };
	manifest.packages["nsis"] = Package{ "HikariSub-0.0.3-windows-x64-setup.exe", 10, kHashA, "10.0.17763", "" };
	SystemInfo system{ "10.0.19045", "2.39" };

	CHECK(Decide(Kind::Deb, true, manifest, system).type == OfferType::Install);
	CHECK(Decide(Kind::Deb, true, manifest, system).package.name == "hikarisub-0.0.3-linux-amd64.deb");
	CHECK(Decide(Kind::NsisMachine, true, manifest, system).type == OfferType::Install);
	// No package of the kind: the notice.
	CHECK(Decide(Kind::Rpm, true, manifest, system).type == OfferType::Notify);
	CHECK(Decide(Kind::AppImage, true, manifest, system).type == OfferType::Notify);
	// "Install updates automatically" off: the notice, for every kind.
	CHECK(Decide(Kind::Deb, false, manifest, system).type == OfferType::Notify);
	// The AUR never looks at the packages, nor the portable copies.
	CHECK(Decide(Kind::Arch, true, manifest, system).type == OfferType::AurNotice);
	CHECK(Decide(Kind::Arch, false, Manifest(), system).type == OfferType::AurNotice);
	CHECK(Decide(Kind::PortableLinux, true, manifest, system).type == OfferType::Notify);
	CHECK(Decide(Kind::PortableWindows, true, manifest, system).type == OfferType::Notify);

	// A system below the package's floor: the notice, naming it.
	Offer old = Decide(Kind::Deb, true, manifest, SystemInfo{ "", "2.31" });
	CHECK(old.type == OfferType::NotifyRequirement);
	CHECK(old.requirement == "glibc 2.35");
	Offer win7 = Decide(Kind::NsisUser, true, manifest, SystemInfo{ "6.1.7601", "" });
	CHECK(win7.type == OfferType::NotifyRequirement);
	CHECK(win7.requirement == "Windows 10.0.17763");
}

TEST(update_download_urls_are_built)
{
	CHECK(DownloadUrl("0.0.2", "hikarisub-0.0.2-linux-amd64.deb") ==
		"https://github.com/altqx/hikari/releases/download/v0.0.2/hikarisub-0.0.2-linux-amd64.deb");
	CHECK(DownloadUrl("0.0.2", "hikarisub-update.json") ==
		"https://github.com/altqx/hikari/releases/download/v0.0.2/hikarisub-update.json");
	CHECK(DownloadUrl("0.0.2", "../x").empty());
	CHECK(DownloadUrl("0.0.2/../..", "x").empty());
	CHECK(DownloadUrl("0.0.2+b", "x").empty());
	CHECK(DownloadUrl("", "x").empty());

	CHECK(IsDownloadUrl("https://github.com/altqx/hikari/releases/download/v0.0.2/x"));
	CHECK(IsDownloadUrl("https://objects.githubusercontent.com/github-production-release-asset/1"));
	CHECK(IsDownloadUrl("HTTPS://Release-Assets.GitHubUserContent.com/x?sig=1"));
	CHECK(!IsDownloadUrl("http://github.com/x"));
	CHECK(!IsDownloadUrl("https://github.com:443/x"));
	CHECK(!IsDownloadUrl("https://user@github.com/x"));
	CHECK(!IsDownloadUrl("https://github.com.evil.example/x"));
	CHECK(!IsDownloadUrl("https://evil.example/github.com/x"));
	CHECK(!IsDownloadUrl("https://api.github.com/x"));
	CHECK(!IsDownloadUrl("https://github.com"));
	CHECK(!IsDownloadUrl(""));

	CHECK(DownloadDeadlineSeconds(1000) == 600);
	CHECK(DownloadDeadlineSeconds(100LL * 1024 * 600) == 600);
	CHECK(DownloadDeadlineSeconds(1LL << 30) == (1LL << 30) / (100 * 1024));
}

TEST(update_local_feed)
{
	CHECK(feed::IsBase("http://127.0.0.1:8765/"));
	CHECK(feed::IsBase("https://example.com/feed/"));
	CHECK(!feed::IsBase("http://127.0.0.1:8765"));
	CHECK(!feed::IsBase("http://localhost:8765/"));
	CHECK(!feed::IsBase("http://127.0.0.10/"));
	CHECK(!feed::IsBase("http://example.com/"));
	CHECK(!feed::IsBase(""));
	std::string base = "http://127.0.0.1:8765/";
	CHECK(feed::ReleasesUrl(base) == "http://127.0.0.1:8765/releases.json");
	CHECK(feed::DownloadUrl(base, "0.0.3", "hikarisub-0.0.3-linux-amd64.deb") ==
		"http://127.0.0.1:8765/download/v0.0.3/hikarisub-0.0.3-linux-amd64.deb");
	CHECK(feed::DownloadUrl(base, "0.0.3", "../x").empty());
	CHECK(feed::PageUrl(base, "v0.0.3") == "http://127.0.0.1:8765/releases/tag/v0.0.3");
	CHECK(feed::IsFeedUrl(base, "http://127.0.0.1:8765/releases.json"));
	CHECK(!feed::IsFeedUrl(base, "http://127.0.0.1:8766/releases.json"));
	CHECK(!feed::IsFeedUrl("", "http://127.0.0.1:8765/releases.json"));
}

TEST(update_pending_record_round_trips)
{
	Pending pending{ "0.0.3", "deb", "/home/u/.cache/HikariSub/Updates/0.0.3/hikarisub-0.0.3-linux-amd64.deb",
		kHashA, 123456, "2026-10-13T10:00:00Z" };
	std::optional<Pending> read = ReadPending(JsonValue::Parse(FormatPending(pending)));
	CHECK(read.has_value());
	if (read) {
		CHECK(read->version == pending.version);
		CHECK(read->kind == "deb");
		CHECK(read->file == pending.file);
		CHECK(read->sha256 == kHashA);
		CHECK(read->size == 123456);
		CHECK(read->downloaded == pending.downloaded);
	}
	// A Windows path with backslashes and a quote survives.
	pending.file = "C:\\Users\\a \"b\"\\AppData\\Local\\HikariSub\\Updates\\0.0.3\\x.exe";
	pending.kind = "nsis-user";
	read = ReadPending(JsonValue::Parse(FormatPending(pending)));
	CHECK(read && read->file == pending.file);

	CHECK(!ReadPending(JsonValue::Parse("{}")));
	pending.kind = "arch";
	CHECK(ReadPending(JsonValue::Parse(FormatPending(pending))));
	pending.kind = "portable-linux";
	CHECK(!ReadPending(JsonValue::Parse(FormatPending(pending))));
	pending.kind = "deb";
	pending.sha256 = "x";
	CHECK(!ReadPending(JsonValue::Parse(FormatPending(pending))));

	LastResult result{ "0.0.3", false, 4, "apt-get failed\nsee the log" };
	std::optional<LastResult> back = ReadLastResult(JsonValue::Parse(FormatLastResult(result)));
	CHECK(back && back->version == "0.0.3" && !back->ok && back->code == 4 && back->message == result.message);
	CHECK(!ReadLastResult(JsonValue::Parse("{\"ok\": false}")));
	CHECK(ReadLastResult(JsonValue::Parse("{\"version\": \"0.0.3\", \"ok\": true}")));
}

TEST(update_versions_newer)
{
	CHECK(IsNewer("0.0.3", "0.0.2"));
	CHECK(IsNewer("0.1.0-beta.1", "0.0.9"));
	CHECK(!IsNewer("0.0.2", "0.0.2"));
	CHECK(!IsNewer("0.0.2-rc.1", "0.0.2"));
	CHECK(!IsNewer("junk", "0.0.2"));
	CHECK(CodebaseOf(*ParseSemVer("0.0.150")) == Codebase::Legacy);
	CHECK(CodebaseOf(*ParseSemVer("0.1.0-alpha.1")) == Codebase::Qt);
	CHECK(CodebaseOf(*ParseSemVer("1.0.0")) == Codebase::Qt);
}

TEST(update_hand_off_commands)
{
	CHECK(NsisArguments(4242, true, false, "C:\\Users\\a b\\AppData\\Local\\Programs\\HikariSub") ==
		"/S /UPDATE /WAITPID=4242 /RELAUNCH /CURRENTUSER /D=C:\\Users\\a b\\AppData\\Local\\Programs\\HikariSub");
	CHECK(NsisArguments(7, false, true, "C:\\Program Files\\HikariSub") ==
		"/S /UPDATE /WAITPID=7 /ALLUSERS /D=C:\\Program Files\\HikariSub");

	CHECK(ShellQuote("/tmp/a b") == "'/tmp/a b'");
	CHECK(ShellQuote("it's") == "'it'\\''s'");
	// By hand, the helper checks the file as root, as through pkexec; the
	// package manager is never given the user's file.
	CHECK(InstallCommand("/home/u/a b.deb", "0.4.0-beta.1") ==
		"sudo /opt/hikarisub/libexec/hikarisub-update-helper install '/home/u/a b.deb' '0.4.0-beta.1'");
	CHECK(InstallCommand("/home/u/it's.rpm", "1.0.0") ==
		"sudo /opt/hikarisub/libexec/hikarisub-update-helper install '/home/u/it'\\''s.rpm' '1.0.0'");
	CHECK(AppImageStagingPath("/home/u/Apps/HikariSub.AppImage", "0.0.3") ==
		"/home/u/Apps/.HikariSub.AppImage.0.0.3.update");
	CHECK(AppImageStagingPath("HikariSub.AppImage", "0.0.3").empty());
	CHECK(AppImageStagingPath("/home/u/Apps/", "0.0.3").empty());
	// A pending AppImage update belongs to the image it was staged for:
	// its folder and its name.
	CHECK(IsStagedBeside("/home/u/Apps/HikariSub.AppImage", "/home/u/Apps/.HikariSub.AppImage.0.0.3.update", "0.0.3"));
	CHECK(!IsStagedBeside("/home/u/Other/HikariSub.AppImage", "/home/u/Apps/.HikariSub.AppImage.0.0.3.update", "0.0.3"));
	CHECK(!IsStagedBeside("/home/u/Apps/HikariSub-test.AppImage", "/home/u/Apps/.HikariSub.AppImage.0.0.3.update", "0.0.3"));
	CHECK(!IsStagedBeside("/home/u/Apps/HikariSub.AppImage", "/home/u/Apps/.HikariSub.AppImage.0.0.4.update", "0.0.3"));
	CHECK(!IsStagedBeside("/home/u/Apps/HikariSub.AppImage", "/home/u/Apps/sub/.HikariSub.AppImage.0.0.3.update", "0.0.3"));
	CHECK(!IsStagedBeside("HikariSub.AppImage", ".HikariSub.AppImage.0.0.3.update", "0.0.3"));
	CHECK(IsStagedBeside("/HikariSub.AppImage", "/.HikariSub.AppImage.0.0.3.update", "0.0.3"));
	// The staged files an image clears are its own.
	CHECK(StagedVersion("HikariSub.AppImage", ".HikariSub.AppImage.0.0.3.update") == "0.0.3");
	CHECK(StagedVersion("HikariSub.AppImage", ".HikariSub.AppImage.0.1.0-beta.2.update.part") == "0.1.0-beta.2");
	CHECK(StagedVersion("HikariSub.AppImage", ".HikariSub-test.AppImage.0.0.3.update").empty());
	CHECK(StagedVersion("HikariSub.AppImage", ".HikariSub.AppImage.x.update").empty());
	CHECK(StagedVersion("HikariSub.AppImage", ".HikariSub.AppImage.0.0.3").empty());
	CHECK(StagedVersion("", "..0.0.3.update").empty());
	// Each copy keeps its own records in the shared cache.
	CHECK(CopyKey("deb", "") == "deb");
	CHECK(CopyKey("nsis-machine", "") == "nsis-machine");
	std::string a = CopyKey("appimage", "/home/u/Apps/HikariSub.AppImage");
	std::string b = CopyKey("appimage", "/home/u/Apps/HikariSub-test.AppImage");
	CHECK(a.size() == 25 && a.starts_with("appimage-"));
	CHECK(a != b);
	CHECK(a == CopyKey("appimage", "/home/u/Apps/HikariSub.AppImage"));
}

TEST(update_appimage_at_start_names_the_pending_version)
{
	// An ordinary quit names the AppImage update the next start installs.
	CHECK(IsAtStartVersion("0.0.3", "0.0.3"));
	CHECK(IsAtStartVersion("0.0.3\n", "0.0.3"));
	CHECK(IsAtStartVersion("0.1.0-beta.1", "0.1.0-beta.1"));
	// Another version, nothing, or what is not a version: nothing installs.
	CHECK(!IsAtStartVersion("0.0.4", "0.0.3"));
	CHECK(!IsAtStartVersion("", ""));
	CHECK(!IsAtStartVersion("\n", ""));
	CHECK(!IsAtStartVersion("0.0.3\n\n", "0.0.3"));
	CHECK(!IsAtStartVersion("junk", "junk"));
}

TEST(update_further_request_of_a_check_takes_data_again)
{
	update_policy::CheckRun run;
	CHECK(run.CutOff(true));
	CHECK(!run.TakesData());
	CHECK(run.PageEnded(update_policy::RequestEnd::Cancelled) == update_policy::Ending::CutOff);
	run.NextRequest();
	CHECK(run.TakesData());
	CHECK(run.PageEnded(update_policy::RequestEnd::Completed) == update_policy::Ending::Answered);
	CHECK(run.AsksNextPage());
	// The deadline is the check's, not the request's.
	CHECK(run.Fire(true) == update_policy::CheckRun::Fired::Cancel);
	run.NextRequest();
	CHECK(!run.AsksNextPage());
	CHECK(run.PageEnded(update_policy::RequestEnd::Cancelled) == update_policy::Ending::TimedOut);
}

namespace {

// The offer member of ADR-0021, with pitch as its JSON value.
std::string OfferJson(const std::string &channel = "prerelease", const std::string &support = "continues",
	const std::string &pitch = "{\"en\": {\"title\": \"Try the new HikariSub\", \"highlights\": [\"Faster\", "
		"\"Docks\"], \"note\": \"This is a beta.\"}}")
{
	return ", \"offer\": {\"to\": \"legacy\", \"channel\": \"" + channel + "\", \"legacy_support\": \"" + support +
		"\", \"pitch\": " + pitch + "}";
}

// A rewrite manifest of version, offered to Qt only, with an offer member
// (offer, as OfferJson gives it, or "" for none).
std::string RewriteJson(const std::string &version, const std::string &offer = OfferJson(),
	const std::string &offerTo = "[\"qt\"]")
{
	return "{\"schema\": 1, \"version\": \"" + version + "\", \"codebase\": \"qt\", \"offer_to\": " + offerTo +
		", \"packages\": {\"deb\": {\"name\": \"hikarisub-" + version + "-linux-amd64.deb\", \"size\": 1000, "
		"\"sha256\": \"" + kHashA + "\"}}" + offer + "}";
}

Release RewriteRelease(const std::string &version, bool prerelease)
{
	return WithAssets(("v" + version).c_str(), prerelease, { Asset("hikarisub-update.json", 50, ""),
		Asset("hikarisub-" + version + "-linux-amd64.deb", 1000, kHashA) });
}

// The offer a manifest's text holds, as legacy reads it.
std::optional<RewriteOffer> OfferOf(const std::string &json, const std::string &version)
{
	Manifest manifest;
	if (Read(json, version, &manifest) != ManifestRead::Valid)
		return std::nullopt;
	return manifest.offer;
}

} // namespace

TEST(update_offer_reads_the_member)
{
	std::optional<RewriteOffer> offer = OfferOf(RewriteJson("0.1.0-beta.1", OfferJson("prerelease", "continues",
		"{\"en\": {\"title\": \"Try the new HikariSub\", \"highlights\": [\"Faster\", \"Docks\"], "
		"\"note\": \"This is a beta.\", \"later\": 1}, \"pl\": {\"title\": \"Wypr\xC3\xB3""buj\", "
		"\"highlights\": [\"Szybszy\"]}}")), "0.1.0-beta.1");
	CHECK(offer.has_value());
	if (!offer)
		return;
	CHECK(offer->channel == OfferChannel::Prerelease);
	CHECK(!offer->legacySupportEnded);
	CHECK(offer->pitch.size() == 2);
	const Pitch &en = offer->pitch.at("en");
	CHECK(en.title == "Try the new HikariSub");
	CHECK(en.highlights == (std::vector<std::string>{ "Faster", "Docks" }));
	CHECK(en.note == "This is a beta.");
	CHECK(offer->pitch.at("pl").note.empty());

	std::optional<RewriteOffer> ended = OfferOf(RewriteJson("0.1.0", OfferJson("stable", "ended")), "0.1.0");
	CHECK(ended && ended->channel == OfferChannel::Stable && ended->legacySupportEnded);

	// No member, no offer; and a legacy manifest's is not read.
	CHECK(!OfferOf(RewriteJson("0.1.0", ""), "0.1.0"));
	CHECK(!OfferOf(ManifestJson("0.0.3", "legacy", "[\"legacy\"]", kHashA, "") + "", "0.0.3"));
	std::string legacyWithOffer = ManifestJson("0.0.3", "legacy", "[\"legacy\"]");
	legacyWithOffer.insert(legacyWithOffer.size() - 1, OfferJson());
	Manifest legacy;
	CHECK(Read(legacyWithOffer, "0.0.3", &legacy) == ManifestRead::Valid);
	CHECK(!legacy.offer);
}

TEST(update_offer_that_breaks_the_rules_is_ignored)
{
	auto pitch = [](const std::string &en) { return "{\"en\": " + en + "}"; };
	auto entry = [](const std::string &title, const std::string &highlights, const std::string &note = "") {
		return "{\"title\": " + title + ", \"highlights\": " + highlights + note + "}";
	};
	std::string title = "\"Try it\"";
	std::string one = "[\"Faster\"]";
	std::vector<std::string> broken = {
		// to, channel and legacy_support.
		", \"offer\": {\"to\": \"qt\", \"channel\": \"stable\", \"legacy_support\": \"continues\", \"pitch\": " +
			pitch(entry(title, one)) + "}",
		", \"offer\": {\"channel\": \"stable\", \"legacy_support\": \"continues\", \"pitch\": " +
			pitch(entry(title, one)) + "}",
		OfferJson("beta", "continues", pitch(entry(title, one))),
		OfferJson("stable", "maybe", pitch(entry(title, one))),
		", \"offer\": {\"to\": \"legacy\", \"channel\": \"stable\", \"pitch\": " + pitch(entry(title, one)) + "}",
		", \"offer\": \"yes\"",
		// The pitch: en is required, and each language holds to the rules.
		OfferJson("stable", "continues", "{\"pl\": " + entry(title, one) + "}"),
		OfferJson("stable", "continues", "[]"),
		OfferJson("stable", "continues", "{\"en\": " + entry(title, one) + ", \"pl\": " + entry(title, "[]") + "}"),
		OfferJson("stable", "continues", pitch(entry("\"\"", one))),
		OfferJson("stable", "continues", pitch(entry("1", one))),
		OfferJson("stable", "continues", pitch(entry("\"" + std::string(81, 'x') + "\"", one))),
		OfferJson("stable", "continues", pitch(entry(title, "[]"))),
		OfferJson("stable", "continues", pitch(entry(title, "\"Faster\""))),
		OfferJson("stable", "continues", pitch(entry(title, "[\"a\", \"b\", \"c\", \"d\", \"e\", \"f\"]"))),
		OfferJson("stable", "continues", pitch(entry(title, "[\"" + std::string(121, 'x') + "\"]"))),
		OfferJson("stable", "continues", pitch(entry(title, "[\"\"]"))),
		OfferJson("stable", "continues", pitch(entry(title, "[2]"))),
		OfferJson("stable", "continues", pitch(entry(title, one, ", \"note\": \"" + std::string(201, 'x') + "\""))),
		OfferJson("stable", "continues", pitch(entry(title, one, ", \"note\": 5"))),
		OfferJson("stable", "continues", pitch("\"Try it\"")),
	};
	for (const std::string &offer : broken) {
		Manifest manifest;
		// The rest of the manifest still counts.
		CHECK(Read(RewriteJson("0.1.0", offer), "0.1.0", &manifest) == ManifestRead::Valid);
		CHECK(!manifest.offer);
		CHECK(manifest.packages.count("deb") == 1);
	}

	// The limits count characters, not bytes: 80 two-byte characters fit.
	std::string wide;
	for (int i = 0; i < 80; i++)
		wide += "\xC3\xA9";
	CHECK(OfferOf(RewriteJson("0.1.0", OfferJson("stable", "continues", pitch(entry("\"" + wide + "\"", one)))),
		"0.1.0"));
	CHECK(!OfferOf(RewriteJson("0.1.0", OfferJson("stable", "continues",
		pitch(entry("\"" + wide + "\xC3\xA9\"", one)))), "0.1.0"));
	// Five highlights of 120 and a note of 200 are the most.
	std::string five = "[";
	for (int i = 0; i < 5; i++)
		five += std::string(i ? ", " : "") + "\"" + std::string(120, 'x') + "\"";
	five += "]";
	CHECK(OfferOf(RewriteJson("0.1.0", OfferJson("stable", "continues",
		pitch(entry(title, five, ", \"note\": \"" + std::string(200, 'x') + "\"")))), "0.1.0"));
}

TEST(update_offer_follows_the_channel)
{
	Release beta = RewriteRelease("0.1.0-beta.1", true);
	std::string prerelease = RewriteJson("0.1.0-beta.1", OfferJson("prerelease"));
	std::string stable = RewriteJson("0.1.0-beta.1", OfferJson("stable"));
	// Seen by those who see prereleases only.
	CHECK(Consider(beta, &prerelease, true).offer.has_value());
	CHECK(!Consider(beta, &prerelease, false).offer);
	// Stable: by everyone.
	CHECK(Consider(beta, &stable, false).offer.has_value());
	CHECK(Consider(beta, &stable, true).offer.has_value());
	// Who sees prereleases: "Stable versions only" off, or an rc build.
	CHECK(SeesPrereleases(false, "0.0.2"));
	CHECK(!SeesPrereleases(true, "0.0.2"));
	CHECK(SeesPrereleases(true, "0.0.2-rc.5"));

	// The offer carries its release's packages, cross-checked as an
	// update's are, and is no update.
	Considered considered = Consider(beta, &stable, false);
	CHECK(!considered.update);
	CHECK(considered.offer && considered.offer->version == "0.1.0-beta.1");
	CHECK(considered.offer && considered.offer->packages.count("deb") == 1);
	Release disagrees = WithAssets("v0.1.0-beta.1", true, { Asset("hikarisub-update.json", 50, ""),
		Asset("hikarisub-0.1.0-beta.1-linux-amd64.deb", 999, kHashA) });
	considered = Consider(disagrees, &stable, false);
	CHECK(considered.offer && considered.offer->packages.empty());

	// A Qt client is offered nothing.
	CHECK(!Consider(beta, &stable, true, Codebase::Qt).offer);
	// Nor is a manifest that failed to come, or a release without one.
	CHECK(!Consider(beta, nullptr, true).offer);
	CHECK(!Consider(WithAssets("v0.1.0", false, {}), nullptr, true).offer);
}

TEST(update_offer_to_legacy_in_offer_to_is_still_an_update)
{
	Release release = RewriteRelease("0.1.0", false);
	std::string both = RewriteJson("0.1.0", OfferJson("stable"), "[\"qt\", \"legacy\"]");
	Considered considered = Consider(release, &both, false);
	CHECK(considered.update.has_value());
	CHECK(considered.update && considered.update->packages.count("deb") == 1);
	// Admit is unchanged.
	CHECK(Admit(release, &both).has_value());
	std::string qtOnly = RewriteJson("0.1.0", OfferJson("stable"));
	CHECK(!Admit(release, &qtOnly));
}

TEST(update_offer_takes_the_highest)
{
	OfferChoice choice;
	CHECK(!choice.Get());
	Release beta1 = RewriteRelease("0.1.0-beta.1", true);
	Release beta2 = RewriteRelease("0.1.0-beta.2", true);
	Release stable = RewriteRelease("0.1.0", false);
	std::string beta1Json = RewriteJson("0.1.0-beta.1", OfferJson("stable"));
	std::string beta2Json = RewriteJson("0.1.0-beta.2", OfferJson("prerelease"));
	std::string stableJson = RewriteJson("0.1.0", OfferJson("stable"));
	// Whatever order they come in.
	choice.Take(Consider(beta1, &beta1Json, true).offer);
	choice.Take(Consider(stable, &stableJson, true).offer);
	choice.Take(Consider(beta2, &beta2Json, true).offer);
	choice.Take(std::nullopt);
	CHECK(choice.Get() && choice.Get()->version == "0.1.0");

	// Of those that apply: beta.2 is for prerelease users only.
	OfferChoice stableUser;
	stableUser.Take(Consider(beta2, &beta2Json, false).offer);
	stableUser.Take(Consider(beta1, &beta1Json, false).offer);
	CHECK(stableUser.Get() && stableUser.Get()->version == "0.1.0-beta.1");
}

TEST(update_offer_pitch_in_the_interface_language)
{
	RewriteOffer offer;
	offer.pitch["en"].title = "English";
	offer.pitch["pl"].title = "Polski";
	offer.pitch["ko-KR"].title = "Korean";
	offer.pitch["th"].title = "Thai";
	CHECK(PitchFor(offer, "pl").title == "Polski");
	// As Config.txt names a language, and in any case.
	CHECK(PitchFor(offer, "ko_KR").title == "Korean");
	CHECK(PitchFor(offer, "KO-kr").title == "Korean");
	// A region the pitch has not falls back to the language.
	CHECK(PitchFor(offer, "th_TH").title == "Thai");
	CHECK(PitchFor(offer, "pl_PL").title == "Polski");
	// Else English, the interface's own included.
	CHECK(PitchFor(offer, "ta").title == "English");
	CHECK(PitchFor(offer, "").title == "English");
	CHECK(PitchFor(offer, "en").title == "English");
	// A pitch of the language in another region stands in too.
	RewriteOffer regional;
	regional.pitch["en"].title = "English";
	regional.pitch["pt-BR"].title = "Portuguese";
	CHECK(PitchFor(regional, "pt").title == "Portuguese");
	CHECK(PitchFor(regional, "pt_PT").title == "Portuguese");
}

TEST(update_offer_at_start_honours_the_choices)
{
	using update_policy::kDay;
	long long now = 1800000000;
	// Never chosen.
	CHECK(ShowsOfferAtStart("0.1.0-beta.1", false, {}, 0, now));
	// Remind me later: not before 3 days, then at the first start after.
	long long remind = OfferRemindAt(now);
	CHECK(remind == now + 3 * kDay);
	CHECK(!ShowsOfferAtStart("0.1.0-beta.1", false, {}, remind, now));
	CHECK(!ShowsOfferAtStart("0.1.0-beta.1", false, {}, remind, now + 3 * kDay - 1));
	CHECK(ShowsOfferAtStart("0.1.0-beta.1", false, {}, remind, now + 3 * kDay));
	CHECK(ShowsOfferAtStart("0.1.0-beta.1", false, {}, remind, now + 30 * kDay));
	// A reminder further off than 3 days was set before the clock went
	// back, and is due.
	CHECK(ShowsOfferAtStart("0.1.0-beta.1", false, {}, remind, now - kDay));
	// Don't offer again: until a higher version carries an offer.
	CHECK(!ShowsOfferAtStart("0.1.0-beta.1", false, { "0.1.0-beta.1", false }, 0, now));
	CHECK(!ShowsOfferAtStart("0.1.0-beta.1", false, { "0.1.0-beta.2", false }, 0, now));
	CHECK(ShowsOfferAtStart("0.1.0-beta.2", false, { "0.1.0-beta.1", false }, 0, now));
	CHECK(ShowsOfferAtStart("0.1.0", false, { "0.1.0-beta.2", false }, 0, now));
	CHECK(!ShowsOfferAtStart("0.1.0-beta.1", false, { "0.1.0-beta.1", false }, remind, now + 3 * kDay));

	// A due reminder makes the startup check due, so the first start after
	// it shows the offer even within a day of the last check.
	CHECK(!OfferReminderDue(0, now));
	CHECK(!OfferReminderDue(remind, now));
	CHECK(OfferReminderDue(remind, now + 3 * kDay));
	CHECK(OfferReminderDue(remind, now - kDay));
}

TEST(update_offer_declined_shows_once_more_when_legacy_support_ends)
{
	long long now = 1800000000;
	// Declined while legacy support continued: the same version, or a
	// lower one, saying it has ended shows once more.
	DeclinedOffer continued{ "0.1.0", false };
	CHECK(!ShowsOfferAtStart("0.1.0", false, continued, 0, now));
	CHECK(ShowsOfferAtStart("0.1.0", true, continued, 0, now));
	CHECK(ShowsOfferAtStart("0.1.0-beta.2", true, continued, 0, now));
	// Declined again once it said so: it holds, until a higher version.
	DeclinedOffer ended{ "0.1.0", true };
	CHECK(!ShowsOfferAtStart("0.1.0", true, ended, 0, now));
	CHECK(!ShowsOfferAtStart("0.1.0-beta.2", false, ended, 0, now));
	CHECK(ShowsOfferAtStart("0.1.1", true, ended, 0, now));
	// Remind me later on the one shown once more still reminds.
	long long remind = OfferRemindAt(now);
	CHECK(!ShowsOfferAtStart("0.1.0", true, continued, remind, now));
	CHECK(ShowsOfferAtStart("0.1.0", true, continued, remind, now + kOfferRemindDelay));

	// Try it on an offer that was declined spends the lift too; on one
	// that was not, the decline stays as it was.
	CHECK(TriedAfter("0.1.0", true, continued).ended);
	CHECK(TriedAfter("0.1.0", true, continued).version == "0.1.0");
	DeclinedOffer older{ "0.1.0-beta.1", false };
	CHECK(!TriedAfter("0.1.0", true, older).ended);
	CHECK(TriedAfter("0.1.0", true, older).version == "0.1.0-beta.1");
	CHECK(TriedAfter("0.1.0", true, {}).version.empty());
}

TEST(update_walk_goes_on_for_offers)
{
	// A legacy update is found; for the offers only, the walk goes on
	// through the rewrite releases after it, and the legacy ones are done.
	std::vector<Release> releases = {
		Release{ "v0.0.3", false, false },
		Release{ "v0.1.0-beta.1", false, true },
		Release{ "v0.0.4-rc.1", false, true },
		Release{ "v0.1.0-beta.2", false, true },
	};
	CandidateWalk walk(releases, ListCandidates(releases, "0.0.2", true));
	CHECK(walk.Current() == 0);
	walk.Take();
	CHECK(walk.Found());
	CHECK(walk.Current() == 3);
	CHECK(walk.CurrentIsSkipped());
	walk.Skip();
	CHECK(walk.Current() == 1);
	walk.Skip();
	CHECK(walk.Current() == -1);
	CHECK(walk.Found());
}

TEST(update_walk_finds_a_legacy_update_past_newer_rewrite_releases)
{
	// The update is looked for among legacy releases and the offer among
	// the rewrite's, kMaxCandidates of each: more newer rewrite releases
	// than that never hide a legacy update.
	std::vector<Release> releases;
	for (int i = 0; i < 7; i++)
		releases.push_back(RewriteRelease("0.1." + std::to_string(i), false));
	releases.push_back(WithAssets("v0.0.3", false, { Asset("hikarisub-0.0.3-linux-amd64.deb", 10, kHashA) }));
	Candidates candidates = ListCandidates(releases, "0.0.2", true);
	CHECK(candidates.offer == (std::vector<int>{ 6, 5, 4, 3, 2, 7 }));

	CandidateWalk walk(releases, candidates);
	std::optional<Manifest> update;
	OfferChoice offers;
	int looked = 0;
	while (walk.Current() >= 0) {
		looked++;
		const Release &release = releases[walk.Current()];
		std::string version = update_policy::CandidateVersion(release.tag);
		std::string manifest = RewriteJson(version, OfferJson("stable"));
		Considered considered = Consider(release, HasManifest(release) ? &manifest : nullptr, false);
		offers.Take(std::move(considered.offer));
		if (considered.update && !walk.Found()) {
			update = considered.update;
			walk.Take();
		}
		else {
			walk.Skip();
		}
	}
	CHECK(looked == int(update_policy::kMaxCandidates) + 1);
	CHECK(update && update->version == "0.0.3");
	CHECK(offers.Get() && offers.Get()->version == "0.1.6");

	// Each codebase has its own budget, the prereleases kept out within it.
	std::vector<Release> many;
	for (int i = 3; i < 10; i++) {
		many.push_back(Release{ "v0.0." + std::to_string(i) + "-rc.1", false, true });
		many.push_back(Release{ "v0.1." + std::to_string(i), false, false });
	}
	many.push_back(Release{ "v0.0.3", false, false });
	CandidateWalk capped(many, ListCandidates(many, "0.0.2", true));
	int legacy = 0, rewrite = 0;
	while (capped.Current() >= 0) {
		(many[capped.Current()].tag.rfind("v0.0.", 0) == 0 ? legacy : rewrite)++;
		capped.Skip();
	}
	CHECK(legacy == int(update_policy::kMaxCandidates));
	CHECK(rewrite == int(update_policy::kMaxCandidates));
}

TEST(update_offer_try_it_follows_the_package_kind)
{
	using package_kind::Kind;
	Manifest manifest;
	CHECK(Read(RewriteJson("0.1.0-beta.1"), "0.1.0-beta.1", &manifest) == ManifestRead::Valid);
	Package nsis;
	nsis.name = "HikariSub-0.1.0-beta.1-windows-x64-setup.exe";
	nsis.size = 1000;
	nsis.sha256 = kHashA;
	manifest.packages["nsis"] = nsis;
	SystemInfo system;

	// Installed kinds download it, check it and offer Update ready: Try it
	// is the user's choice, whatever "Install updates automatically" says.
	for (Kind kind : { Kind::NsisUser, Kind::NsisMachine, Kind::Deb }) {
		Offer offer = TryOffer(kind, true, manifest, system);
		CHECK(offer.type == OfferType::Install);
		CHECK(offer.package.name == manifest.packages[std::string(package_kind::PackageKey(kind))].name);
	}
	// One that cannot replace itself, or has no package of its kind, gets
	// the release page.
	CHECK(TryOffer(Kind::Deb, false, manifest, system).type == OfferType::Notify);
	CHECK(TryOffer(Kind::AppImage, true, manifest, system).type == OfferType::Notify);
	// A portable copy gets the release page, and the note to unpack the
	// rewrite beside it.
	for (Kind kind : { Kind::PortableWindows, Kind::PortableLinux }) {
		CHECK(TryOffer(kind, false, manifest, system).type == OfferType::Notify);
		CHECK(TryOpensBeside(kind));
	}
	CHECK(!TryOpensBeside(Kind::Deb));
	CHECK(!TryOpensBeside(Kind::NsisUser));

	// AUR installs get no offer: each AUR package moves on its own.
	CHECK(!GetsOffers(Kind::Arch));
	for (Kind kind : { Kind::NsisUser, Kind::NsisMachine, Kind::AppImage, Kind::Deb, Kind::Rpm,
		Kind::PortableWindows, Kind::PortableLinux })
	{
		CHECK(GetsOffers(kind));
	}
}
