#include "hikari/plugins/marketplace.h"

#include "hikari/api/schema.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace hikari::plugins {

namespace fs = std::filesystem;
using api::Json;

namespace {

constexpr std::size_t kMaxIndexBytes = 32 * 1024 * 1024;
constexpr std::string_view kIndexComment = "file:index.json serial:";

struct Schema {
    Json root;
    Json defs;
};

const Schema &schema()
{
    static const Schema loaded = [] {
        Schema s{*Json::parse(marketplaceIndexSchema()), Json::object()};
        auto &members = s.root.asObject();
        for (auto it = members.begin(); it != members.end();) {
            if (it->key == "$defs")
                s.defs = it->value;
            if (it->key.starts_with('$'))
                it = members.erase(it);
            else
                ++it;
        }
        return s;
    }();
    return loaded;
}

std::string text(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

std::vector<std::string> texts(const Json &object, std::string_view key)
{
    std::vector<std::string> out;
    if (const Json *value = object.find(key))
        for (const Json &item : value->asArray())
            out.push_back(item.asString());
    return out;
}

std::optional<Revocation> revocation(const Json &object)
{
    const Json *revoked = object.find("revoked");
    if (!revoked)
        return std::nullopt;
    return Revocation{text(*revoked, "reason"), text(*revoked, "date")};
}

ListedVersion versionOf(const Json &v)
{
    ListedVersion out;
    out.version = text(v, "version");
    out.api = v.find("api")->asInt();
    out.unrestricted = text(v, "tier") == "unrestricted";
    out.name = text(v, "name");
    out.summary = text(v, "summary");
    out.description = text(v, "description");
    out.license = text(v, "license");
    out.homepage = text(v, "homepage");
    out.authors = texts(v, "authors");
    out.permissions = *v.find("permissions");
    out.url = text(v, "url");
    out.sha256 = text(v, "sha256");
    out.size = v.find("size")->asInt();
    out.published = text(v, "published");
    out.changelog = text(v, "changelog");
    out.icon = text(v, "icon");
    if (const Json *shots = v.find("screenshots"))
        for (const Json &shot : shots->asArray())
            out.screenshots.push_back({text(shot, "url"), text(shot, "caption")});
    out.readme = text(v, "readme");
    out.revoked = revocation(v);
    return out;
}

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

}

const ListedVersion *Listing::newest(std::int64_t apiVersion) const
{
    if (revoked)
        return nullptr;
    const auto it =
        std::ranges::find_if(versions, [&](const ListedVersion &v) { return !v.revoked && v.api <= apiVersion; });
    return it == versions.end() ? nullptr : &*it;
}

const Listing *MarketIndex::find(std::string_view id) const
{
    const auto it = std::ranges::find(plugins, id, &Listing::id);
    return it == plugins.end() ? nullptr : &*it;
}

std::expected<MarketIndex, IndexRefusal> readIndex(std::span<const std::byte> bytes, std::string_view signature,
                                                   std::span<const minisign::PublicKey> trusted, std::int64_t highestSeen)
{
    using Kind = IndexRefusal::Kind;
    if (bytes.size() > kMaxIndexBytes)
        return std::unexpected(IndexRefusal{Kind::Malformed, "index.json is larger than 32 MiB"});
    const auto verified = minisign::verify(bytes, signature, trusted);
    if (!verified)
        return std::unexpected(IndexRefusal{Kind::Signature, std::string(minisign::refusalText(verified.error()))});
    if (!verified->trustedComment.starts_with(kIndexComment))
        return std::unexpected(IndexRefusal{Kind::OtherFile, "the signature is for " + inQuotes(verified->trustedComment)});
    auto json = Json::parse(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    if (!json)
        return std::unexpected(IndexRefusal{Kind::Malformed, "index.json is not JSON"});
    if (auto failure = api::validate(schema().root, *json, schema().defs))
        return std::unexpected(
            IndexRefusal{Kind::Malformed, "index.json fails its schema at " + failure->path + ": " + failure->message});

    MarketIndex index;
    index.serial = json->find("serial")->asInt();
    index.generated = text(*json, "generated");
    if (verified->trustedComment != std::string(kIndexComment) + std::to_string(index.serial) + " generated:" + index.generated)
        return std::unexpected(IndexRefusal{Kind::OtherFile, "the signature is for " + inQuotes(verified->trustedComment)});
    if (index.serial < highestSeen)
        return std::unexpected(IndexRefusal{Kind::Older, "index serial " + std::to_string(index.serial) +
                                                             " is older than serial " + std::to_string(highestSeen)});

    for (const Json::Member &api : json->find("apiVersions")->asObject())
        index.apiVersions[std::stoll(api.key)] = api.value.asString();
    index.featured = texts(*json, "featured");
    for (const Json &p : json->find("plugins")->asArray()) {
        const auto key = minisign::parsePublicKey(text(p, "publicKey"));
        if (!key)
            return std::unexpected(IndexRefusal{Kind::Malformed, inQuotes(text(p, "id")) + " has no valid publicKey"});
        Listing listing{text(p, "id"), text(p, "author"), text(p, "authorName"), text(p, "repo"),
                        *key,          texts(p, "categories"), revocation(p), {}};
        for (const Json &v : p.find("versions")->asArray())
            listing.versions.push_back(versionOf(v));
        index.plugins.push_back(std::move(listing));
    }
    return index;
}

std::optional<std::string> listedPackageProblem(const ListedVersion &listed, std::span<const std::byte> bytes)
{
    if (static_cast<std::int64_t>(bytes.size()) != listed.size)
        return "the download is " + std::to_string(bytes.size()) + " bytes, not the " + std::to_string(listed.size) +
               " the marketplace lists";
    if (minisign::sha256Hex(bytes) != listed.sha256)
        return std::string("the download\xE2\x80\x99s SHA-256 isn\xE2\x80\x99t the one the marketplace lists");
    return std::nullopt;
}

std::optional<std::string> listedManifestProblem(const Listing &listing, const ListedVersion &listed,
                                                 const Manifest &manifest, const std::optional<minisign::PublicKey> &pinned)
{
    if (manifest.id != listing.id)
        return "the package is " + inQuotes(manifest.id) + ", not " + inQuotes(listing.id);
    if (compareVersions(manifest.version, listed.version) != 0)
        return "the package is version " + manifest.version + ", not " + listed.version;
    if ((manifest.tier == Tier::Unrestricted) != listed.unrestricted || manifest.api != listed.api)
        return std::string("the package asks for another tier or API than the marketplace lists");
    if (!manifest.publicKey || *manifest.publicKey != listing.publicKey)
        return "the package\xE2\x80\x99s publicKey isn\xE2\x80\x99t the key the marketplace lists for " + inQuotes(listing.id);
    if (pinned && *pinned != listing.publicKey)
        return "the marketplace lists key " + listing.publicKey.idText() + ", but the installed copy was signed by key " +
               pinned->idText();
    return std::nullopt;
}

std::optional<Revoked> revocation(const Listing &listing, std::string_view version, std::int64_t apiVersion)
{
    if (listing.revoked)
        return Revoked{listing.revoked->reason, {}};
    const auto installed = std::ranges::find(listing.versions, version, &ListedVersion::version);
    if (installed == listing.versions.end() || !installed->revoked)
        return std::nullopt;
    const ListedVersion *good = listing.newest(apiVersion);
    return Revoked{installed->revoked->reason, good ? good->version : std::string()};
}

std::int64_t loadHighestSerial(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return 0;
    std::stringstream contents;
    contents << in.rdbuf();
    const auto json = Json::parse(contents.str());
    const Json *serial = json ? json->find("serial") : nullptr;
    return serial && serial->isInt() && serial->asInt() > 0 ? serial->asInt() : 0;
}

bool saveHighestSerial(const fs::path &file, std::int64_t serial)
{
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    fs::path partial = file;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << Json::object({{"serial", serial}}).dump(2) << "\n";
        if (!out)
            return false;
    }
    fs::rename(partial, file, error);
    return !error;
}

namespace {

constexpr std::size_t kMaxIdLength = 100;

bool isIdWord(std::string_view word)
{
    if (word.empty() || word.front() == '-' || word.back() == '-' || word.find("--") != std::string_view::npos)
        return false;
    return std::ranges::all_of(word, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool fitsManifestId(std::string_view id)
{
    if (id.size() > kMaxIdLength || id.find('.') == std::string_view::npos)
        return false;
    for (std::size_t start = 0;;) {
        const std::size_t dot = id.find('.', start);
        if (!isIdWord(id.substr(start, dot - start)))
            return false;
        if (dot == std::string_view::npos)
            return true;
        start = dot + 1;
    }
}

bool startsCaseless(std::string_view text, std::string_view lowerPrefix)
{
    return text.size() >= lowerPrefix.size() &&
           std::ranges::equal(text.substr(0, lowerPrefix.size()), lowerPrefix,
                              [](char a, char b) { return (a >= 'A' && a <= 'Z' ? char(a - 'A' + 'a') : a) == b; });
}

}

std::optional<std::string> installLinkId(std::string_view link)
{
    constexpr std::string_view scheme = "hikarisub://", action = "plugin/install?id=";
    if (!startsCaseless(link, scheme) || !link.substr(scheme.size()).starts_with(action))
        return std::nullopt;
    const std::string_view id = link.substr(scheme.size() + action.size());
    return fitsManifestId(id) ? std::optional<std::string>(id) : std::nullopt;
}

}
