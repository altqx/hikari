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
#include "UpdateVerify.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
		return update_policy::IsLegacyVersion(version) ? Codebase::Legacy : Codebase::Qt;
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

	// What a rewrite release's manifest may offer legacy users, its
	// "offer" member (ADR-0021): to try the rewrite, opt-in.
	enum class OfferChannel
	{
		// To those who see prereleases (SeesPrereleases()).
		Prerelease,
		// To every legacy user.
		Stable,
	};

	// Plain text, in one language.
	struct Pitch
	{
		std::string title;
		std::vector<std::string> highlights;
		// Or "".
		std::string note;
	};

	struct RewriteOffer
	{
		OfferChannel channel = OfferChannel::Prerelease;
		// legacy_support "ended": legacy gets no further updates.
		bool legacySupportEnded = false;
		// By language tag as the manifest gives it; "en" is always there.
		std::map<std::string, Pitch> pitch;
	};

	// The pitch's limits, in characters.
	constexpr size_t kMaxOfferTitle = 80;
	constexpr size_t kMaxOfferHighlights = 5;
	constexpr size_t kMaxOfferHighlight = 120;
	constexpr size_t kMaxOfferNote = 200;

	struct Manifest
	{
		std::string version;
		Codebase codebase = Codebase::Legacy;
		bool offerLegacy = false;
		bool offerQt = false;
		// By package key; a kind the release does not build is absent.
		std::map<std::string, Package> packages;
		// A rewrite release's offer to legacy, when it holds one that
		// keeps to the rules.
		std::optional<RewriteOffer> offer;

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

		// The characters of UTF-8 text: the bytes that do not continue one.
		inline size_t CharacterCount(std::string_view text)
		{
			size_t count = 0;
			for (char c : text) {
				if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
					count++;
			}
			return count;
		}

		// A string of 1 to limit characters, or "" for anything else.
		inline bool ReadText(const JsonValue *value, size_t limit, std::string *out)
		{
			if (!value || value->GetType() != JsonValue::Type::String)
				return false;
			size_t length = CharacterCount(value->Utf8());
			if (length == 0 || length > limit)
				return false;
			*out = value->Utf8();
			return true;
		}

		inline bool ReadPitch(const JsonValue &entry, Pitch *pitch)
		{
			if (entry.GetType() != JsonValue::Type::Object)
				return false;
			if (!ReadText(entry.Find("title"), kMaxOfferTitle, &pitch->title))
				return false;
			const JsonValue *highlights = Member(entry, "highlights", JsonValue::Type::Array);
			if (!highlights || highlights->Items().empty() || highlights->Items().size() > kMaxOfferHighlights)
				return false;
			for (const JsonValue &highlight : highlights->Items()) {
				std::string text;
				if (!ReadText(&highlight, kMaxOfferHighlight, &text))
					return false;
				pitch->highlights.push_back(text);
			}
			if (const JsonValue *note = entry.Find("note")) {
				if (!ReadText(note, kMaxOfferNote, &pitch->note))
					return false;
			}
			return true;
		}

		// The offer member (ADR-0021), or nothing when it breaks a rule.
		// Unknown keys are ignored, as elsewhere in the manifest.
		inline std::optional<RewriteOffer> ReadOffer(const JsonValue &value)
		{
			if (value.GetType() != JsonValue::Type::Object)
				return std::nullopt;
			const JsonValue *to = Member(value, "to", JsonValue::Type::String);
			const JsonValue *channel = Member(value, "channel", JsonValue::Type::String);
			const JsonValue *support = Member(value, "legacy_support", JsonValue::Type::String);
			const JsonValue *pitches = Member(value, "pitch", JsonValue::Type::Object);
			if (!to || to->Utf8() != CodebaseName(Codebase::Legacy) || !channel || !support || !pitches)
				return std::nullopt;
			RewriteOffer offer;
			if (channel->Utf8() == "prerelease")
				offer.channel = OfferChannel::Prerelease;
			else if (channel->Utf8() == "stable")
				offer.channel = OfferChannel::Stable;
			else
				return std::nullopt;
			if (support->Utf8() != "continues" && support->Utf8() != "ended")
				return std::nullopt;
			offer.legacySupportEnded = support->Utf8() == "ended";
			for (const auto &[language, entry] : pitches->Members()) {
				Pitch pitch;
				if (!ReadPitch(entry, &pitch))
					return std::nullopt;
				offer.pitch[language] = std::move(pitch);
			}
			if (!offer.pitch.count("en"))
				return std::nullopt;
			return offer;
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
		// A rewrite release's only; one that breaks a rule is ignored, and
		// the rest of the manifest still counts.
		if (read.codebase == Codebase::Qt) {
			if (const JsonValue *offer = root.Find("offer"))
				read.offer = detail::ReadOffer(*offer);
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

	// Whether a user sees prereleases: "Stable versions only" is off, or
	// this is a release candidate (update_policy::SkipsPrereleases()).
	constexpr bool SeesPrereleases(bool stableOnly, std::string_view installed)
	{
		return !update_policy::SkipsPrereleases(stableOnly, installed);
	}

	// What a candidate release holds for this copy.
	struct Considered
	{
		// The update it offers, as Admit() gives it.
		std::optional<Manifest> update;
		// A rewrite release whose offer applies to this user (ADR-0021):
		// its manifest, the packages cross-checked as an update's are.
		std::optional<Manifest> offer;
	};

	// What a candidate release holds for this copy, from its manifest when
	// it has one: an update, or nothing for a release whose manifest failed
	// to come (manifest null) or to read, one not offered to this codebase,
	// or a tag that is no candidate. A broken manifest is never bypassed.
	// A rewrite release that is not offered to legacy can still carry an
	// offer, which applies by its channel; one that is offered to legacy is
	// an update like any other. seesPrereleases: SeesPrereleases().
	inline Considered Consider(const Release &release, const std::string *manifestText, bool seesPrereleases,
		Codebase client = kThisCodebase)
	{
		Considered considered;
		std::string version = update_policy::CandidateVersion(release.tag);
		if (version.empty())
			return considered;
		Manifest manifest;
		if (HasManifest(release)) {
			if (!manifestText || manifestText->size() > kMaxManifestBytes)
				return considered;
			switch (ReadManifest(JsonValue::Parse(*manifestText), version, &manifest)) {
			case ManifestRead::Valid:
				break;
			case ManifestRead::UnknownSchema:
				manifest = FallbackManifest(version, release.assets);
				break;
			case ManifestRead::Invalid:
				return considered;
			}
		}
		else {
			manifest = FallbackManifest(version, release.assets);
		}
		CrossCheck(&manifest, release.assets);
		if (manifest.OffersTo(client)) {
			considered.update = std::move(manifest);
			return considered;
		}
		if (client == Codebase::Legacy && manifest.offer &&
			(manifest.offer->channel == OfferChannel::Stable || seesPrereleases))
		{
			considered.offer = std::move(manifest);
		}
		return considered;
	}

	// The update a candidate release offers this copy (Consider()).
	inline std::optional<Manifest> Admit(const Release &release, const std::string *manifestText,
		Codebase client = kThisCodebase)
	{
		return Consider(release, manifestText, false, client).update;
	}

	// The highest of the offers a check comes across, in whatever order
	// it comes across them (ADR-0021).
	class OfferChoice
	{
	public:
		void Take(std::optional<Manifest> offered)
		{
			if (!offered)
				return;
			std::optional<SemVer> version = ParseSemVer(offered->version);
			if (!version)
				return;
			if (best && CompareSemVer(*version, *ParseSemVer(best->version)) <= 0)
				return;
			best = std::move(offered);
		}

		// Its manifest, offer and packages, or nullptr.
		const Manifest *Get() const { return best ? &*best : nullptr; }

	private:
		std::optional<Manifest> best;
	};

	// The pitch in the interface language, as Config.txt names it ("pl",
	// "ko_KR"; "" for English), else in English. A tag is matched without
	// case and with "_" as "-": the whole tag, then its language alone,
	// then the language in any region.
	inline const Pitch &PitchFor(const RewriteOffer &offer, std::string_view language)
	{
		auto normal = [](std::string_view tag) {
			std::string text = Lowercase(tag);
			for (char &c : text) {
				if (c == '_')
					c = '-';
			}
			return text;
		};
		auto primary = [](const std::string &tag) { return tag.substr(0, tag.find('-')); };
		std::string wanted = normal(language);
		if (wanted.empty())
			return offer.pitch.at("en");
		const Pitch *same = nullptr;
		const Pitch *base = nullptr;
		const Pitch *region = nullptr;
		for (const auto &[tag, pitch] : offer.pitch) {
			std::string key = normal(tag);
			if (key == wanted)
				same = &pitch;
			else if (key == primary(wanted))
				base = &pitch;
			else if (!region && primary(key) == primary(wanted))
				region = &pitch;
		}
		return same ? *same : base ? *base : region ? *region : offer.pitch.at("en");
	}

	// "Remind me later" shows the offer again at the first start this long
	// after.
	constexpr long long kOfferRemindDelay = 3 * update_policy::kDay;

	constexpr long long OfferRemindAt(long long now)
	{
		return now + kOfferRemindDelay;
	}

	// Whether a reminder is set and has come: a time further off than the
	// delay was set before the clock was turned back, and has too.
	constexpr bool OfferReminderDue(long long remindAt, long long now)
	{
		return remindAt > 0 && (now >= remindAt || remindAt - now > kOfferRemindDelay);
	}

	// What "Don't offer again" declined: the offer's version, and whether
	// it said legacy gets no further updates.
	struct DeclinedOffer
	{
		std::string version;
		bool ended = false;
	};

	// Whether the startup check shows the offer of version, ended when it
	// says legacy gets no further updates: not one that "Don't offer
	// again" declined, or a lower one, unless it is the first to say so
	// since; and not while a reminder is waiting.
	inline bool ShowsOfferAtStart(std::string_view version, bool ended, const DeclinedOffer &declined,
		long long remindAt, long long now)
	{
		if (update_policy::WasDeclined(version, declined.version) && (!ended || declined.ended))
			return false;
		return remindAt <= 0 || OfferReminderDue(remindAt, now);
	}

	// The decline after "Don't offer again" on the offer of version: it
	// replaces declined unless declined is higher, as when the end of
	// legacy support showed a lower offer once more; that keeps the
	// higher version and spends the lift.
	inline DeclinedOffer DeclinedAfter(std::string_view version, bool ended, const DeclinedOffer &declined)
	{
		std::optional<SemVer> offered = ParseSemVer(version), kept = ParseSemVer(declined.version);
		if (offered && kept && CompareSemVer(*kept, *offered) > 0)
			return { declined.version, declined.ended || ended };
		return { std::string(version), ended };
	}

	// The decline after Try it on the offer of version: one it covers now
	// covers the end of legacy support too, if the offer said so, as that
	// was shown once more.
	inline DeclinedOffer TriedAfter(std::string_view version, bool ended, DeclinedOffer declined)
	{
		if (ended && update_policy::WasDeclined(version, declined.version))
			declined.ended = true;
		return declined;
	}

	// How the offer's window was left: Try it, Don't offer again, or
	// Remind me later, as any other way of closing it is.
	enum class OfferAnswer
	{
		Tried,
		NoOffer,
		Later,
	};

	// What the offer's window leaves recorded: the decline, and when the
	// reminder comes (0 for none).
	struct OfferRecord
	{
		DeclinedOffer declined;
		long long remindAt = 0;
	};

	// The record after answer, at now, on the offer of version. Remind me
	// later is the later choice than a decline, as when Help showed an
	// offer declined before, so the offer comes back at the reminder.
	inline OfferRecord RecordAfter(OfferAnswer answer, std::string_view version, bool ended,
		const DeclinedOffer &declined, long long now)
	{
		switch (answer) {
		case OfferAnswer::Tried:
			return { TriedAfter(version, ended, declined), 0 };
		case OfferAnswer::NoOffer:
			return { DeclinedAfter(version, ended, declined), 0 };
		case OfferAnswer::Later:
			break;
		}
		return { {}, OfferRemindAt(now) };
	}

	// What the check at startup looks for.
	enum class StartCheck
	{
		None,
		// The updates and the offer.
		Full,
		// The offer only: a reminder came with the automatic checks off,
		// which show no update.
		OfferOnly,
	};

	// The check at startup, with autoCheck the automatic checks, checkDue
	// once a day is over since the last, and reminderDue once a reminder
	// of the offer has come (OfferReminderDue()).
	constexpr StartCheck CheckAtStart(bool autoCheck, bool checkDue, bool reminderDue)
	{
		if (!autoCheck)
			return reminderDue ? StartCheck::OfferOnly : StartCheck::None;
		return checkDue || reminderDue ? StartCheck::Full : StartCheck::None;
	}

	// Whether the startup check that answered spends a reminder that has
	// come, as it does whether the offer shows now or waits for a legacy
	// update: not when a check asked for from the menu joined it, as the
	// offer does not show at start then, nor in a check with the automatic
	// checks off (StartCheck::OfferOnly) while the offer waits, or when
	// none came as a manifest did not (offerMissed), as only the reminder
	// brings the next.
	constexpr bool SpendsReminder(bool askedFromMenu, bool offerOnly, bool offerWaits, bool offerMissed)
	{
		return !askedFromMenu && !(offerOnly && (offerWaits || offerMissed));
	}

	// How a check ends that ending stopped while it read the manifests:
	// answered once the update is found, as only offers were left to look
	// for, and the highest found so far stands.
	constexpr update_policy::Ending WalkEnding(update_policy::Ending ending, bool updateFound)
	{
		return ending == update_policy::Ending::TimedOut && updateFound ? update_policy::Ending::Answered : ending;
	}

	// The columns a code point takes: two for the wide East Asian ones and
	// emoji, one for the rest.
	constexpr size_t ColumnsOf(char32_t c)
	{
		return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
			(c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
			(c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x3FFFD) ? 2 : 1;
	}

	// UTF-8 text broken at spaces into lines of up to width columns, as
	// the static texts do not wrap; a run longer than that, as Thai or
	// Chinese is written, goes on from the line it starts on and is
	// broken at the width.
	inline std::string WrapText(std::string_view text, size_t width)
	{
		std::string wrapped;
		size_t line = 0;
		// The word so far: where each code point starts in it, and its
		// columns.
		std::string word;
		std::vector<std::pair<size_t, size_t>> points;
		size_t wordColumns = 0;
		auto flush = [&] {
			if (word.empty())
				return;
			if (line > 0 && (line + 1 + wordColumns <= width || (wordColumns > width && line + 1 < width))) {
				wrapped += ' ';
				line++;
			}
			else if (line > 0) {
				wrapped += '\n';
				line = 0;
			}
			for (size_t i = 0; i < points.size(); i++) {
				if (line > 0 && line + points[i].second > width) {
					wrapped += '\n';
					line = 0;
				}
				size_t end = i + 1 < points.size() ? points[i + 1].first : word.size();
				wrapped.append(word, points[i].first, end - points[i].first);
				line += points[i].second;
			}
			word.clear();
			points.clear();
			wordColumns = 0;
		};
		for (size_t i = 0; i < text.size();) {
			unsigned char lead = static_cast<unsigned char>(text[i]);
			size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
			if (i + length > text.size())
				length = 1;
			char32_t c = length == 1 ? lead : lead & (0x7F >> length);
			for (size_t k = 1; k < length; k++)
				c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
			if (c == ' ') {
				flush();
			}
			else if (c == '\n') {
				flush();
				wrapped += '\n';
				line = 0;
			}
			else {
				size_t columns = ColumnsOf(c);
				points.emplace_back(word.size(), columns);
				word.append(text.substr(i, length));
				wordColumns += columns;
			}
			i += length;
		}
		flush();
		return wrapped;
	}

	// The releases a check looks at, in order: the candidates that fit the
	// channel, then those "stable versions only" kept out, kMaxCandidates
	// legacy releases, and as many of the rewrite's from each list, so
	// stable ones do not keep out a newer prerelease's offer. The first that Admit()
	// takes is the update. Past it, only the rewrite's are left, for the
	// offers they may carry: those newer than the update, when it is the
	// rewrite's itself.
	class CandidateWalk
	{
	public:
		CandidateWalk(const std::vector<update_policy::Release> &releases,
			const update_policy::Candidates &candidates)
		{
			size_t legacyCount = 0;
			for (const auto &[list, skipped] : { std::pair{ &candidates.offer, false },
				std::pair{ &candidates.skipped, true } })
			{
				size_t rewriteCount = 0;
				for (int index : *list) {
					std::optional<SemVer> version =
						ParseSemVer(update_policy::CandidateVersion(releases[index].tag));
					bool legacy = version && CodebaseOf(*version) == Codebase::Legacy;
					size_t &count = legacy ? legacyCount : rewriteCount;
					if (count < update_policy::kMaxCandidates) {
						count++;
						order.push_back({ index, skipped, legacy, version });
					}
				}
			}
		}

		// The release to look at, as an index into the list, or -1 when
		// the walk is over.
		int Current() const { return next >= order.size() ? -1 : order[next].index; }

		// Whether the current release is a prerelease that "stable
		// versions only" kept out, which is mentioned, not offered.
		bool CurrentIsSkipped() const { return Current() >= 0 && order[next].skipped; }

		// The current release is ruled out; on to the next.
		void Skip() { Advance(); }

		// The current release is the update; on to the rewrite's after it.
		void Take()
		{
			if (Current() < 0)
				return;
			found = true;
			if (!order[next].legacy)
				foundRewrite = order[next].version;
			Advance();
		}

		bool Found() const { return found; }

	private:
		struct Entry
		{
			int index;
			bool skipped;
			bool legacy;
			std::optional<SemVer> version;
		};

		// Whether an entry is done with once the update is found.
		bool Passed(const Entry &entry) const
		{
			return entry.legacy || (foundRewrite && (!entry.version || CompareSemVer(*entry.version, *foundRewrite) <= 0));
		}

		void Advance()
		{
			if (next < order.size())
				next++;
			while (found && next < order.size() && Passed(order[next]))
				next++;
		}

		std::vector<Entry> order;
		size_t next = 0;
		bool found = false;
		// The update, when it is the rewrite's.
		std::optional<SemVer> foundRewrite;
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

	// AUR installs get no offer of the rewrite: each AUR package moves to
	// it on its own (ADR-0021).
	constexpr bool GetsOffers(Kind kind)
	{
		return package_kind::FlowOf(kind) != package_kind::Flow::AurNotice;
	}

	// What Try it on the offer does: an installed kind downloads it, as
	// the user asked, whatever "Install updates automatically" says;
	// canInstall is whether this copy can replace itself. A portable copy,
	// or one that cannot, gets the release page.
	inline Offer TryOffer(Kind kind, bool canInstall, const Manifest &manifest, const SystemInfo &system)
	{
		return Decide(kind, canInstall, manifest, system);
	}

	// Whether Try it leads to a package to unpack beside this copy: a
	// portable one's.
	constexpr bool TryOpensBeside(Kind kind)
	{
		return package_kind::FlowOf(kind) == package_kind::Flow::Notify;
	}

	// Whether Try it opens the release page, which ends the offer's
	// window as tried, rather than download: also when the package needs
	// a newer system, which the window then says.
	constexpr bool TryOpensReleasePage(OfferType type)
	{
		return type != OfferType::Install;
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

	// <update cache>/pending-<CopyKey>.json: a verified download waiting
	// to be installed (4.1).
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
	// for the image and the version, so that a later version does not
	// overwrite it and another AppImage in the same folder stages its own.
	// "" when $APPIMAGE is not an absolute file path.
	inline std::string AppImageStagingPath(std::string_view appImage, std::string_view version)
	{
		size_t slash = appImage.find_last_of('/');
		if (appImage.empty() || appImage[0] != '/' || slash + 1 == appImage.size())
			return std::string();
		return std::string(appImage.substr(0, slash + 1)) + "." + std::string(appImage.substr(slash + 1)) + "." +
			std::string(version) + ".update";
	}

	// The version a file beside the AppImage was staged for, when it is
	// this image's download or its .part; else "".
	inline std::string StagedVersion(std::string_view appImageName, std::string_view name)
	{
		std::string prefix = "." + std::string(appImageName) + ".";
		if (appImageName.empty() || !name.starts_with(prefix))
			return std::string();
		name.remove_prefix(prefix.size());
		if (name.ends_with(".part"))
			name.remove_suffix(5);
		if (!name.ends_with(".update"))
			return std::string();
		name.remove_suffix(7);
		return ParseSemVer(name) ? std::string(name) : std::string();
	}

	// Whether a pending AppImage update was staged for this image: the
	// cache is shared by every copy the user keeps, and a download for
	// another copy, in another folder or under another name, is that
	// copy's to install.
	inline bool IsStagedBeside(std::string_view appImage, std::string_view file, std::string_view version)
	{
		std::string staged = AppImageStagingPath(appImage, version);
		return !staged.empty() && file == staged;
	}

	// What this copy's files in the shared update cache are named for
	// (pending-<key>.json and its markers), so that copies of other kinds,
	// and AppImages at other paths, keep their own: the kind's token, and
	// for an AppImage a hash of its path too. A collision only shares a
	// record, which IsStagedBeside still tells apart.
	inline std::string CopyKey(std::string_view kindToken, std::string_view appImage)
	{
		if (kindToken != package_kind::Token(package_kind::Kind::AppImage))
			return std::string(kindToken);
		unsigned long long hash = 14695981039346656037ULL; // FNV-1a
		for (unsigned char c : appImage) {
			hash ^= c;
			hash *= 1099511628211ULL;
		}
		std::string key = std::string(kindToken) + "-";
		for (int shift = 60; shift >= 0; shift -= 4)
			key += "0123456789abcdef"[(hash >> shift) & 0xF];
		return key;
	}

	// The update helper of the deb and rpm, and the switch that tells the
	// new version which it updated from.
	constexpr std::string_view kUpdateHelper = "/opt/hikarisub/libexec/hikarisub-update-helper";
	constexpr std::string_view kNativeLauncher = "/usr/bin/hikarisub";
	constexpr std::string_view kUpdatedFromSwitch = "--hikari-updated-from=";
	constexpr std::string_view kHealthSwitch = "--hikari-health=";

	// The NSIS installer's arguments in /UPDATE mode (4.2).
	using update_verify::NsisArguments;

	// The helper's exit codes (4.4), and pkexec's for a prompt that was
	// dismissed or not authorised. The Windows installing side
	// (UpdateApply.h) exits with the same 0, 2, 3, 4 and 6.
	constexpr int kHelperOk = update_verify::kOk;
	constexpr int kHelperBadArguments = update_verify::kBadArguments;
	constexpr int kHelperMismatch = update_verify::kMismatch;
	constexpr int kHelperPackageManagerFailed = update_verify::kFailed;
	constexpr int kHelperLocked = 5;
	constexpr int kHelperNoSums = update_verify::kNoSums;
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

	// Whether the at-start file an ordinary quit wrote names the
	// pending update's version: that version, with an optional newline.
	inline bool IsAtStartVersion(std::string_view named, std::string_view pending)
	{
		if (!named.empty() && named.back() == '\n')
			named.remove_suffix(1);
		return !named.empty() && named == pending && update_verify::IsReleaseVersion(named);
	}

	// The command a user runs to install a downloaded package by hand,
	// when there is no pkexec (4.4): the update helper through sudo, so the
	// package is checked against the release's SHA256SUMS, as root, as it
	// is through pkexec. The file the app downloaded is not handed to the
	// package manager itself; another process of the user's could replace
	// it after the app's own check.
	inline std::string InstallCommand(std::string_view file, std::string_view version)
	{
		return "sudo " + std::string(kUpdateHelper) + " install " + ShellQuote(file) + " " + ShellQuote(version);
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
