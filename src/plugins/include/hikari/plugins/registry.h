#pragma once

#include "hikari/api/json.h"
#include "hikari/minisign/minisign.h"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace hikari::plugins {

struct FileGrant {
    std::string path;
    bool writable = false;
};

// Where the installed copy came from: a package file the user chose, or the marketplace.
enum class Source { File, Marketplace };

// The marketplace revoked the installed version or the whole plugin, so it was turned off.
struct Revoked {
    std::string reason;
    std::string lastGood; // the version to offer instead; "" when the whole plugin is revoked
    bool operator==(const Revoked &) const = default;
};

struct InstalledPlugin {
    std::string version;
    bool enabled = true;
    std::optional<minisign::PublicKey> key; // pinned at first signed install
    std::map<std::string, FileGrant> grants; // by the manifest's file request id
    api::Json settings = api::Json::object(); // values set, by the manifest's setting id
    Source source = Source::File;
    bool autoUpdate = true;
    std::string skip;     // a version automatic updates pass over
    std::string previous; // the version kept in plugins/.previous/<id> for roll back
    std::string held;     // a newer version that asks for more, waiting for the user
    std::optional<Revoked> revoked;
};

// plugins.json in the user folder, by plugin id.
using Registry = std::map<std::string, InstalledPlugin>;

// A missing file is an empty registry; an unreadable or malformed one is an error, so
// pinned keys are never silently forgotten.
std::expected<Registry, std::string> loadRegistry(const std::filesystem::path &file);
// Writes beside the file and renames over it.
bool saveRegistry(const std::filesystem::path &file, const Registry &registry);

}
