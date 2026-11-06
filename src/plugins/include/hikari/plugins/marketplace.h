#pragma once

#include "hikari/api/json.h"
#include "hikari/minisign/minisign.h"
#include "hikari/plugins/manifest.h"
#include "hikari/plugins/registry.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::plugins {

// The key that signs the marketplace's index.json, separate from the release key (ADR 0022).
inline constexpr std::string_view kMarketplaceKey = "RWRj5HLU1C1i70gsNZclt1QV9nnY1f2sCHn+OGCw3Jok59Ox8sXliEoH";

// docs/schemas/marketplace-index.schema.json, compiled in.
std::string_view marketplaceIndexSchema();

struct Revocation {
    std::string reason;
    std::string date;
};

struct ListedScreenshot {
    std::string url;
    std::string caption;
};

struct ListedVersion {
    std::string version;
    std::int64_t api = 1;
    bool unrestricted = false;
    std::string name;
    std::string summary;
    std::string description;
    std::string license;
    std::string homepage;
    std::vector<std::string> authors;
    api::Json permissions = api::Json::object();
    std::string url;
    std::string sha256;
    std::int64_t size = 0;
    std::string published;
    std::string changelog;
    std::string icon;
    std::vector<ListedScreenshot> screenshots;
    std::string readme;
    std::optional<Revocation> revoked;
};

struct Listing {
    std::string id;
    std::string author;
    std::string authorName;
    std::string repo;
    minisign::PublicKey publicKey;
    std::vector<std::string> categories;
    std::optional<Revocation> revoked;
    std::vector<ListedVersion> versions; // newest first

    // The newest version this HikariSub can run that isn't revoked; null when none is.
    const ListedVersion *newest(std::int64_t apiVersion) const;
};

struct MarketIndex {
    std::int64_t serial = 0;
    std::string generated;
    std::map<std::int64_t, std::string> apiVersions; // the first HikariSub version with each API number
    std::vector<std::string> featured;
    std::vector<Listing> plugins;

    const Listing *find(std::string_view id) const;
};

struct IndexRefusal {
    enum class Kind { Signature, OtherFile, Older, Malformed };
    Kind kind = Kind::Malformed;
    std::string detail;
};

// Checks the signature against `trusted`, then that its trusted comment names this index.json
// and its serial, and that the serial isn't below `highestSeen`; only then reads the JSON.
std::expected<MarketIndex, IndexRefusal> readIndex(std::span<const std::byte> bytes, std::string_view signature,
                                                   std::span<const minisign::PublicKey> trusted, std::int64_t highestSeen);

// Why downloaded bytes aren't the package the index lists, or nothing.
std::optional<std::string> listedPackageProblem(const ListedVersion &listed, std::span<const std::byte> bytes);
// Why the package's manifest doesn't match its listing: another id, version, tier or API, or
// another author key than the listing's or the one pinned at first install.
std::optional<std::string> listedManifestProblem(const Listing &listing, const ListedVersion &listed, const Manifest &manifest,
                                                 const std::optional<minisign::PublicKey> &pinned);

// Why the installed `version` of `listing` must be turned off, with the version to offer instead
// (the newest one this HikariSub can run that isn't revoked; "" when there's none, or when the whole
// plugin is revoked); nullopt when it may run.
std::optional<Revoked> revocation(const Listing &listing, std::string_view version, std::int64_t apiVersion);

// The highest index serial HikariSub has accepted, kept in <user folder>/marketplace.json; 0 when none.
std::int64_t loadHighestSerial(const std::filesystem::path &file);
bool saveHighestSerial(const std::filesystem::path &file, std::int64_t serial);

}
