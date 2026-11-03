#pragma once

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

struct InstalledPlugin {
    std::string version;
    bool enabled = true;
    std::optional<minisign::PublicKey> key; // pinned at first signed install
    std::map<std::string, FileGrant> grants; // by the manifest's file request id
};

// plugins.json in the user folder, by plugin id.
using Registry = std::map<std::string, InstalledPlugin>;

// A missing file is an empty registry; an unreadable or malformed one is an error, so
// pinned keys are never silently forgotten.
std::expected<Registry, std::string> loadRegistry(const std::filesystem::path &file);
// Writes beside the file and renames over it.
bool saveRegistry(const std::filesystem::path &file, const Registry &registry);

}
