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

// A release's update manifest, hikarisub-update.json, and what the check
// makes of it: which release to offer this copy, and how. Also the records
// the updater keeps between starts. Apart from wx and the network so that
// the tests can decide it. The names, formats and rules here are a
// contract with the release job and the Qt app (installers spec, sections
// 2 to 4).

#pragma once

#include "JsonValue.h"
#include "PackageKind.h"
#include "SemVer.h"
#include "UpdatePolicy.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace update_manifest
{
	using package_kind::Kind;
	using update_policy::Release;
	using update_policy::ReleaseAsset;

	enum class Codebase
	{
		Legacy,
		Qt,
	};

	// This app.
	constexpr Codebase kThisCodebase = Codebase::Legacy;

	constexpr std::string_view CodebaseName(Codebase codebase)
	{
		return codebase == Codebase::Legacy ? "legacy" : "qt";
	}

	constexpr std::optional<Codebase> ParseCodebase(std::string_view name)
	{
		if (name == "legacy")
			return Codebase::Legacy;
		if (name == "qt")
			return Codebase::Qt;
		return std::nullopt;
	}

	// Legacy stays below 0.1.0 and Qt starts there (1.2).
	constexpr Codebase CodebaseOf(const SemVer &version)
	{
		return version.major == 0 && version.minor == 0 ? Codebase::Legacy : Codebase::Qt;
	}

	constexpr std::string_view kManifestName = "hikarisub-update.json";
	// A larger manifest is cut off and ignored.
	constexpr size_t kMaxManifestBytes = 64 * 1024;
	// A larger package is refused.
	constexpr long long kMaxPackageBytes = 1LL << 30;

	// The manifest's package keys, as 2.1 names them.
	constexpr std::string_view kPackageKeys[] = { "nsis", "portable-windows", "appimage", "deb", "rpm",
		"portable-linux" };

	// ^[A-Za-z0-9][A-Za-z0-9._-]*$, the names GitHub keeps as uploaded.
	constexpr bool IsAssetName(std::string_view name)
	{
		if (name.empty())
			return false;
		for (size_t i = 0; i < name.size(); i++) {
			char c = name[i];
			bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
			if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '-')))
				return false;
		}
		return true;
	}

	// The asset name of a package kind for a version (2.1), or "" for a key
	// that is none.
	inline std::string AssetName(std::string_view key, std::string_view version)
	{
		std::string v(version);
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
		return std::string();
	}

	constexpr bool IsSha256(std::string_view hex)
	{
		if (hex.size() != 64)
			return false;
		for (char c : hex) {
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}

	inline std::string Lowercase(std::string_view text)
	{
		std::string lower(text);
		for (char &c : lower) {
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return lower;
	}

	// The hash of a GitHub digest, "sha256:<hex>", in lowercase, or "".
	inline std::string DigestSha256(std::string_view digest)
	{
		constexpr std::string_view kPrefix = "sha256:";
		if (digest.substr(0, kPrefix.size()) != kPrefix)
			return std::string();
		std::string hex = Lowercase(digest.substr(kPrefix.size()));
		return IsSha256(hex) ? hex : std::string();
	}

	// "10.0.17763" or "2.35": numbers and dots, a number at each end.
	constexpr bool IsDottedVersion(std::string_view text)
	{
		if (text.empty() || text.front() == '.' || text.back() == '.')
			return false;
		char previous = 0;
		for (char c : text) {
			if (c == '.' && previous == '.')
				return false;
			if (c != '.' && !(c >= '0' && c <= '9'))
				return false;
			previous = c;
		}
		return true;
	}

	// Negative, zero or positive as a is lower, the same or higher, number
	// by number; a missing number is 0. Text that is not a dotted version
	// is lower than any that is.
	inline int CompareDotted(std::string_view a, std::string_view b)
	{
		bool aValid = IsDottedVersion(a), bValid = IsDottedVersion(b);
		if (!aValid || !bValid)
			return aValid - bValid;
		while (!a.empty() || !b.empty()) {
			auto take = [](std::string_view &text) {
				size_t dot = text.find('.');
				std::string_view part = text.substr(0, dot);
				text = dot == std::string_view::npos ? std::string_view() : text.substr(dot + 1);
				while (part.size() > 1 && part[0] == '0')
					part.remove_prefix(1);
				return part;
			};
			std::string_view left = take(a), right = take(b);
			if (left.empty())
				left = "0";
			if (right.empty())
				right = "0";
			if (left.size() != right.size())
				return left.size() < right.size() ? -1 : 1;
			int result = left.compare(right);
			if (result != 0)
				return result < 0 ? -1 : 1;
		}
		return 0;
	}

	struct Package
	{
		std::string name;
		long long size = 0;
		// 64 lowercase hex characters.
		std::string sha256;
		// The lowest Windows major.minor.build, or "".
		std::string minOs;
		// The lowest glibc, or "".
		std::string minGlibc;
	};

	struct Manifest
	{
		std::string version;
		Codebase codebase = Codebase::Legacy;
		bool offerLegacy = false;
		bool offerQt = false;
		// By package key; a kind the release does not build is absent.
		std::map<std::string, Package> packages;

		bool OffersTo(Codebase client) const { return client == Codebase::Legacy ? offerLegacy : offerQt; }
	};

	enum class ManifestRead
	{
		Valid,
		// A schema this client does not know: the release counts as one
		// without a manifest.
		UnknownSchema,
		// A broken manifest, which rules the release out.
		Invalid,
	};

	namespace detail
	{
		inline bool IsWholeNumber(const JsonValue &value, long long low, long long high, long long *out)
		{
			if (value.GetType() != JsonValue::Type::Number)
				return false;
			double number = value.Number();
			if (!(number >= double(low) && number <= double(high)))
				return false;
			long long whole = static_cast<long long>(number);
			if (double(whole) != number)
				return false;
			*out = whole;
			return true;
		}

		inline const JsonValue *Member(const JsonValue &object, const char *key, JsonValue::Type type)
		{
			const JsonValue *value = object.Find(key);
			return value && value->GetType() == type ? value : nullptr;
		}

		// A package entry of the manifest, for key and version, or false.
		inline bool ReadPackage(const JsonValue &entry, std::string_view key, std::string_view version,
			Package *package)
		{
			if (entry.GetType() != JsonValue::Type::Object)
				return false;
			const JsonValue *name = Member(entry, "name", JsonValue::Type::String);
			const JsonValue *size = entry.Find("size");
			const JsonValue *sha256 = Member(entry, "sha256", JsonValue::Type::String);
			if (!name || !size || !sha256)
				return false;
			// The name is the one 2.1 gives, so that the URL built from it
			// is the asset's.
			if (!IsAssetName(name->Utf8()) || name->Utf8() != AssetName(key, version))
				return false;
			if (!IsWholeNumber(*size, 1, kMaxPackageBytes, &package->size))
				return false;
			if (!IsSha256(sha256->Utf8()))
				return false;
			package->name = name->Utf8();
			package->sha256 = sha256->Utf8();
			for (auto [field, out] : { std::pair<const char *, std::string *>{ "min_os", &package->minOs },
				std::pair<const char *, std::string *>{ "min_glibc", &package->minGlibc } })
			{
				const JsonValue *value = entry.Find(field);
				if (!value)
					continue;
				if (value->GetType() != JsonValue::Type::String || !IsDottedVersion(value->Utf8()))
					return false;
				*out = value->Utf8();
			}
			return true;
		}
	}

	// Reads a manifest for the release of version, its tag without "v"
	// (2.3). Unknown keys are ignored at every level.
	inline ManifestRead ReadManifest(const JsonValue &root, std::string_view version, Manifest *manifest)
	{
		using detail::Member;
		if (root.GetType() != JsonValue::Type::Object)
			return ManifestRead::Invalid;
		long long schema = 0;
		const JsonValue *schemaValue = root.Find("schema");
		if (!schemaValue || !detail::IsWholeNumber(*schemaValue, 0, 1LL << 40, &schema))
			return ManifestRead::Invalid;
		if (schema != 1)
			return ManifestRead::UnknownSchema;

		std::optional<SemVer> tagVersion = ParseSemVer(version);
		const JsonValue *versionValue = Member(root, "version", JsonValue::Type::String);
		if (!tagVersion || !versionValue || versionValue->Utf8() != version)
			return ManifestRead::Invalid;

		const JsonValue *codebaseValue = Member(root, "codebase", JsonValue::Type::String);
		std::optional<Codebase> codebase = codebaseValue ? ParseCodebase(codebaseValue->Utf8()) : std::nullopt;
		if (!codebase || *codebase != CodebaseOf(*tagVersion))
			return ManifestRead::Invalid;

		Manifest read;
		read.version = std::string(version);
		read.codebase = *codebase;
		const JsonValue *offerTo = Member(root, "offer_to", JsonValue::Type::Array);
		if (!offerTo || offerTo->Items().empty())
			return ManifestRead::Invalid;
		for (const JsonValue &entry : offerTo->Items()) {
			std::optional<Codebase> client = entry.GetType() == JsonValue::Type::String ?
				ParseCodebase(entry.Utf8()) : std::nullopt;
			if (!client)
				return ManifestRead::Invalid;
			(*client == Codebase::Legacy ? read.offerLegacy : read.offerQt) = true;
		}

		const JsonValue *packages = Member(root, "packages", JsonValue::Type::Object);
		if (!packages)
			return ManifestRead::Invalid;
		for (std::string_view key : kPackageKeys) {
			const JsonValue *entry = packages->Find(std::string(key));
			if (!entry)
				continue;
			Package package;
			if (!detail::ReadPackage(*entry, key, version, &package))
				return ManifestRead::Invalid;
			read.packages[std::string(key)] = package;
		}
		*manifest = std::move(read);
		return ManifestRead::Valid;
	}

	// What a release without a manifest, every one before the installers,
	// holds (2.4): the codebase its version gives, offered to that
	// codebase alone, and the assets named as 2.1 names them for its
	// version, with GitHub's digest as the hash. An asset without a digest
	// gives no package.
	inline Manifest FallbackManifest(std::string_view version, const std::vector<ReleaseAsset> &assets)
	{
		Manifest manifest;
		manifest.version = std::string(version);
		std::optional<SemVer> parsed = ParseSemVer(version);
		manifest.codebase = parsed ? CodebaseOf(*parsed) : Codebase::Qt;
		(manifest.codebase == Codebase::Legacy ? manifest.offerLegacy : manifest.offerQt) = parsed.has_value();
		for (std::string_view key : kPackageKeys) {
			std::string name = AssetName(key, version);
			for (const ReleaseAsset &asset : assets) {
				if (asset.name != name)
					continue;
				std::string sha256 = DigestSha256(asset.digest);
				if (sha256.empty() || asset.size < 1 || asset.size > kMaxPackageBytes)
					break;
				manifest.packages[std::string(key)] = Package{ name, asset.size, sha256, "", "" };
				break;
			}
		}
		return manifest;
	}

	// Drops each package that the release does not hold as the manifest
	// says: an asset of that name is missing, of another size, or with a
	// digest that differs from the manifest's hash.
	inline void CrossCheck(Manifest *manifest, const std::vector<ReleaseAsset> &assets)
	{
		for (auto it = manifest->packages.begin(); it != manifest->packages.end();) {
			const ReleaseAsset *found = nullptr;
			for (const ReleaseAsset &asset : assets) {
				if (asset.name == it->second.name) {
					found = &asset;
					break;
				}
			}
			bool usable = found && found->size == it->second.size &&
				(found->digest.empty() || DigestSha256(found->digest) == it->second.sha256);
			it = usable ? std::next(it) : manifest->packages.erase(it);
		}
	}

	inline bool HasManifest(const Release &release)
	{
		for (const ReleaseAsset &asset : release.assets) {
			if (asset.name == kManifestName)
				return true;
		}
		return false;
	}

	// What a candidate release offers this copy, from its manifest when it
	// has one, or nothing: a release whose manifest failed to come
	// (manifest null) or to read, one not offered to this codebase, or a
	// tag that is no candidate. A broken manifest is never bypassed.
	inline std::optional<Manifest> Admit(const Release &release, const std::string *manifestText,
		Codebase client = kThisCodebase)
	{
		std::string version = update_policy::CandidateVersion(release.tag);
		if (version.empty())
			return std::nullopt;
		Manifest manifest;
		if (HasManifest(release)) {
			if (!manifestText || manifestText->size() > kMaxManifestBytes)
				return std::nullopt;
			switch (ReadManifest(JsonValue::Parse(*manifestText), version, &manifest)) {
			case ManifestRead::Valid:
				break;
			case ManifestRead::UnknownSchema:
				manifest = FallbackManifest(version, release.assets);
				break;
			case ManifestRead::Invalid:
				return std::nullopt;
			}
		}
		else {
			manifest = FallbackManifest(version, release.assets);
		}
		if (!manifest.OffersTo(client))
			return std::nullopt;
		CrossCheck(&manifest, release.assets);
		return manifest;
	}

	// The releases a check looks at, in order: the candidates that fit the
	// channel, then those "stable versions only" kept out, kMaxCandidates
	// in all. The first that Admit() takes is the one offered.
	class CandidateWalk
	{
	public:
		explicit CandidateWalk(const update_policy::Candidates &candidates)
		{
			for (int index : candidates.offer)
				order.push_back({ index, false });
			for (int index : candidates.skipped)
				order.push_back({ index, true });
			if (order.size() > update_policy::kMaxCandidates)
				order.resize(update_policy::kMaxCandidates);
		}

		// The release to look at, as an index into the list, or -1 when
		// the walk is over.
		int Current() const { return done || next >= order.size() ? -1 : order[next].first; }

		// Whether the current release is a prerelease that "stable
		// versions only" kept out, which is mentioned, not offered.
		bool CurrentIsSkipped() const { return Current() >= 0 && order[next].second; }

		// The current release is ruled out; on to the next.
		void Skip()
		{
			if (Current() >= 0)
				next++;
		}

		// The current release is the one; the walk is over.
		void Take()
		{
			if (Current() >= 0)
				done = true;
		}

		bool Found() const { return done; }

	private:
		std::vector<std::pair<int, bool>> order;
		size_t next = 0;
		bool done = false;
	};

	// What the system is, for a package's min_os and min_glibc.
	struct SystemInfo
	{
		// "major.minor.build" from RtlGetVersion, on Windows.
		std::string windowsVersion;
		// gnu_get_libc_version(), on Linux.
		std::string glibcVersion;
	};

	enum class OfferType
	{
		// Download, verify and install (4.1 to 4.4).
		Install,
		// The release page only, as the package needs a newer system.
		NotifyRequirement,
		// The release page only (4.6).
		Notify,
		// Update with the package manager (4.5).
		AurNotice,
	};

	struct Offer
	{
		OfferType type = OfferType::Notify;
		// For Install.
		Package package;
		// For NotifyRequirement: "Windows 10.0.17763" or "glibc 2.35".
		std::string requirement;
	};

	// How an admitted release reaches this copy (2.4, step 4).
	// autoInstall is "Install updates automatically".
	inline Offer Decide(Kind kind, bool autoInstall, const Manifest &manifest, const SystemInfo &system)
	{
		Offer offer;
		switch (package_kind::FlowOf(kind)) {
		case package_kind::Flow::AurNotice:
			offer.type = OfferType::AurNotice;
			return offer;
		case package_kind::Flow::Notify:
			return offer;
		case package_kind::Flow::Install:
			break;
		}
		if (!autoInstall)
			return offer;
		auto found = manifest.packages.find(std::string(package_kind::PackageKey(kind)));
		if (found == manifest.packages.end() || !IsSha256(found->second.sha256))
			return offer;
		const Package &package = found->second;
		bool windows = kind == Kind::NsisUser || kind == Kind::NsisMachine;
		if (windows && !package.minOs.empty() && CompareDotted(system.windowsVersion, package.minOs) < 0) {
			offer.type = OfferType::NotifyRequirement;
			offer.requirement = "Windows " + package.minOs;
			return offer;
		}
		if (!windows && !package.minGlibc.empty() && CompareDotted(system.glibcVersion, package.minGlibc) < 0) {
			offer.type = OfferType::NotifyRequirement;
			offer.requirement = "glibc " + package.minGlibc;
			return offer;
		}
		offer.type = OfferType::Install;
		offer.package = package;
		return offer;
	}

	// Whether a release is a prerelease, by GitHub's flag or its tag, for
	// the AUR notice's wording (4.5).
	inline bool IsPrerelease(const Release &release)
	{
		std::optional<SemVer> version = ParseSemVer(release.tag);
		return release.prerelease || (version && !version->prerelease.empty());
	}

	// Where a release's files are downloaded from (2.2): built from the
	// tag and the name, never taken from a reply. Empty when either fails
	// its check.
	inline std::string DownloadUrl(std::string_view version, std::string_view name)
	{
		std::string tag = "v" + std::string(version);
		if (update_policy::CandidateVersion(tag).empty() || !IsAssetName(name))
			return std::string();
		return "https://github.com/" + std::string(update_policy::kRepository) + "/releases/download/" + tag +
			"/" + std::string(name);
	}

	// Whether a download's URL, after any redirects, is https on one of the
	// hosts GitHub serves release files from. The host is compared without
	// case; a port or user info fails, as IsApiReplyUrl() does.
	inline bool IsDownloadUrl(std::string_view url)
	{
		constexpr std::string_view kScheme = "https://";
		if (Lowercase(url.substr(0, kScheme.size())) != kScheme)
			return false;
		std::string_view rest = url.substr(kScheme.size());
		size_t end = rest.find('/');
		if (end == std::string_view::npos)
			return false;
		std::string host = Lowercase(rest.substr(0, end));
		return host == "github.com" || host == "objects.githubusercontent.com" ||
			host == "release-assets.githubusercontent.com";
	}

	// The whole-download deadline: ten minutes, or the time the package
	// takes at 100 KiB/s if longer (4.1).
	constexpr long long DownloadDeadlineSeconds(long long size)
	{
		long long atSlowRate = size / (100 * 1024);
		return atSlowRate > 600 ? atSlowRate : 600;
	}

	// How long a download may go without a byte before it fails.
	constexpr long long kStallSeconds = 60;

	// Whether version is newer than running. Anything that is not a
	// version is not.
	inline bool IsNewer(std::string_view version, std::string_view running)
	{
		std::optional<SemVer> a = ParseSemVer(version), b = ParseSemVer(running);
		return a && b && CompareSemVer(*a, *b) > 0;
	}

	// JSON string literal for UTF-8 text.
	inline std::string JsonQuote(std::string_view text)
	{
		std::string out = "\"";
		for (char c : text) {
			unsigned char u = static_cast<unsigned char>(c);
			if (c == '"' || c == '\\') {
				out += '\\';
				out += c;
			}
			else if (u < 0x20) {
				char escaped[8];
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", u);
				out += escaped;
			}
			else {
				out += c;
			}
		}
		return out + "\"";
	}

	// <update cache>/pending.json: a verified download waiting to be
	// installed (4.1).
	struct Pending
	{
		std::string version;
		// The kind's token.
		std::string kind;
		// The absolute path of the verified file.
		std::string file;
		std::string sha256;
		long long size = 0;
		// RFC 3339.
		std::string downloaded;
	};

	inline std::string FormatPending(const Pending &pending)
	{
		return "{\"schema\": 1, \"version\": " + JsonQuote(pending.version) + ", \"kind\": " +
			JsonQuote(pending.kind) + ", \"file\": " + JsonQuote(pending.file) + ", \"sha256\": " +
			JsonQuote(pending.sha256) + ", \"size\": " + std::to_string(pending.size) + ", \"downloaded\": " +
			JsonQuote(pending.downloaded) + "}\n";
	}

	inline std::optional<Pending> ReadPending(const JsonValue &root)
	{
		using detail::Member;
		if (root.GetType() != JsonValue::Type::Object)
			return std::nullopt;
		long long schema = 0;
		const JsonValue *schemaValue = root.Find("schema");
		if (!schemaValue || !detail::IsWholeNumber(*schemaValue, 1, 1, &schema))
			return std::nullopt;
		Pending pending;
		const char *keys[] = { "version", "kind", "file", "sha256", "downloaded" };
		std::string *fields[] = { &pending.version, &pending.kind, &pending.file, &pending.sha256,
			&pending.downloaded };
		for (size_t i = 0; i < 5; i++) {
			const JsonValue *value = Member(root, keys[i], JsonValue::Type::String);
			if (!value)
				return std::nullopt;
			*fields[i] = value->Utf8();
		}
		const JsonValue *size = root.Find("size");
		if (!size || !detail::IsWholeNumber(*size, 1, kMaxPackageBytes, &pending.size))
			return std::nullopt;
		if (!ParseSemVer(pending.version) || !package_kind::ParseToken(pending.kind) ||
			!IsSha256(pending.sha256) || pending.file.empty())
		{
			return std::nullopt;
		}
		return pending;
	}

	// <update cache>/last-result.json: how an install that the app handed
	// off ended, shown once at the next start (4.1).
	struct LastResult
	{
		std::string version;
		bool ok = false;
		int code = 0;
		std::string message;
	};

	inline std::string FormatLastResult(const LastResult &result)
	{
		return "{\"version\": " + JsonQuote(result.version) + ", \"ok\": " + (result.ok ? "true" : "false") +
			", \"code\": " + std::to_string(result.code) + ", \"message\": " + JsonQuote(result.message) + "}\n";
	}

	inline std::optional<LastResult> ReadLastResult(const JsonValue &root)
	{
		if (root.GetType() != JsonValue::Type::Object)
			return std::nullopt;
		LastResult result;
		const JsonValue *version = detail::Member(root, "version", JsonValue::Type::String);
		const JsonValue *ok = detail::Member(root, "ok", JsonValue::Type::Bool);
		const JsonValue *message = detail::Member(root, "message", JsonValue::Type::String);
		if (!version || !ok)
			return std::nullopt;
		result.version = version->Utf8();
		result.ok = root.GetBool("ok");
		long long code = 0;
		if (const JsonValue *codeValue = root.Find("code"))
			detail::IsWholeNumber(*codeValue, -(1LL << 31), (1LL << 31) - 1, &code);
		result.code = static_cast<int>(code);
		if (message)
			result.message = message->Utf8();
		return result;
	}

	// The AppImage's download, beside $APPIMAGE (4.3): hidden, and named
	// for the version, so that a later one does not overwrite it.
	inline std::string AppImageStagingName(std::string_view version)
	{
		return ".HikariSub-" + std::string(version) + ".AppImage";
	}

	// The update helper of the deb and rpm, and the switch that tells the
	// new version which it updated from.
	constexpr std::string_view kUpdateHelper = "/opt/hikarisub/libexec/hikarisub-update-helper";
	constexpr std::string_view kNativeLauncher = "/usr/bin/hikarisub";
	constexpr std::string_view kUpdatedFromSwitch = "--hikari-updated-from=";
	constexpr std::string_view kHealthSwitch = "--hikari-health=";

	// The NSIS installer's arguments in /UPDATE mode (4.2). /D= comes last
	// and unquoted, as NSIS takes the rest of the line as the folder.
	inline std::string NsisArguments(unsigned long pid, bool relaunch, bool allUsers, std::string_view installDir)
	{
		std::string arguments = "/S /UPDATE /WAITPID=" + std::to_string(pid);
		if (relaunch)
			arguments += " /RELAUNCH";
		arguments += allUsers ? " /ALLUSERS" : " /CURRENTUSER";
		arguments += " /D=" + std::string(installDir);
		return arguments;
	}

	// The helper's exit codes (4.4), and pkexec's for a prompt that was
	// dismissed or not authorised.
	constexpr int kHelperOk = 0;
	constexpr int kHelperBadArguments = 2;
	constexpr int kHelperMismatch = 3;
	constexpr int kHelperPackageManagerFailed = 4;
	constexpr int kHelperLocked = 5;
	constexpr int kPkexecDismissed = 126;
	constexpr int kPkexecNotAuthorised = 127;

	// Single quotes for sh, with any single quote in the text closed,
	// escaped and reopened.
	inline std::string ShellQuote(std::string_view text)
	{
		std::string quoted = "'";
		for (char c : text) {
			if (c == '\'')
				quoted += "'\\''";
			else
				quoted += c;
		}
		return quoted + "'";
	}

	enum class InstallTool
	{
		Apt,
		Dnf,
		Zypper,
	};

	// The command a user runs to install a downloaded package by hand,
	// when there is no pkexec (4.4).
	inline std::string InstallCommand(InstallTool tool, std::string_view file)
	{
		switch (tool) {
		case InstallTool::Apt: return "sudo apt install " + ShellQuote(file);
		case InstallTool::Dnf: return "sudo dnf install " + ShellQuote(file);
		case InstallTool::Zypper: return "sudo zypper install --allow-unsigned-rpm " + ShellQuote(file);
		}
		return std::string();
	}

	// The local update feed of test builds (installers spec, 8.4): a base
	// URL, "http://127.0.0.1:<port>/" or https, that stands in for GitHub.
	namespace feed
	{
		// Whether base can stand in for GitHub: https anywhere, or http on
		// 127.0.0.1 only; ending in "/".
		inline bool IsBase(std::string_view base)
		{
			if (base.empty() || base.back() != '/')
				return false;
			std::string lower = Lowercase(base);
			if (lower.rfind("https://", 0) == 0)
				return lower.size() > 9;
			constexpr std::string_view kLocal = "http://127.0.0.1";
			if (lower.rfind(kLocal, 0) != 0)
				return false;
			std::string_view rest = std::string_view(lower).substr(kLocal.size());
			return rest[0] == '/' || rest[0] == ':';
		}

		inline std::string ReleasesUrl(std::string_view base) { return std::string(base) + "releases.json"; }

		inline std::string DownloadUrl(std::string_view base, std::string_view version, std::string_view name)
		{
			if (update_manifest::DownloadUrl(version, name).empty())
				return std::string();
			return std::string(base) + "download/v" + std::string(version) + "/" + std::string(name);
		}

		inline std::string PageUrl(std::string_view base, std::string_view tag)
		{
			if (update_policy::ReleasePageUrl(tag).empty())
				return std::string();
			return std::string(base) + "releases/tag/" + std::string(tag);
		}

		// Whether a reply's final URL is under base.
		inline bool IsFeedUrl(std::string_view base, std::string_view url)
		{
			return !base.empty() && Lowercase(url.substr(0, base.size())) == Lowercase(base);
		}
	}
}
