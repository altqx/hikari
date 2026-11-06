#pragma once

#include "hikari/api/json.h"
#include "hikari/minisign/minisign.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::plugins {

inline constexpr std::string_view kManifestName = "hikari-plugin.json";

enum class Tier { Restricted, Unrestricted };

struct FileGrantRequest {
    std::string id;
    std::string title;
    bool writable = false;
    std::string reason;
};

struct Permissions {
    std::optional<std::string> allDocuments; // the reason, when asked for
    std::vector<std::string> networkHosts;
    std::string networkReason;
    std::vector<FileGrantRequest> files;
};

struct Contribution {
    std::string id;
    std::string title;
};

enum class PanelArea { Left, Right, Bottom };

struct Panel {
    std::string id;
    std::string title;
    PanelArea area = PanelArea::Right;
};

struct Setting {
    std::string id;
    std::string title;
    api::Json definition; // the manifest's object, with type and default
};

struct Screenshot {
    std::string path;
    std::string caption;
};

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::int64_t api = 1;
    std::string main;
    Tier tier = Tier::Restricted;
    std::vector<std::string> authors;
    std::string description;
    std::string license;
    std::string homepage;
    std::string summary;
    std::string icon; // a package path, or empty
    std::vector<Screenshot> screenshots;
    std::optional<minisign::PublicKey> publicKey;
    Permissions permissions;
    std::vector<Contribution> commands;
    std::vector<Panel> panels;
    std::vector<Setting> settings;
};

// The published JSON Schema, docs/schemas/hikari-plugin.schema.json.
std::string_view manifestSchema();

struct ManifestProblem {
    enum class Kind { NotJson, Schema, DuplicateId, BadDefault, NeedsNewerApi };
    Kind kind = Kind::Schema;
    std::string at;     // a JSON pointer into the manifest
    std::string detail; // English, technical: the parser's or validator's words
    std::int64_t neededApi = 0;
};

std::expected<Manifest, ManifestProblem> parseManifest(std::string_view text, std::int64_t apiVersion);
// A manifest's permissions object; parts of the wrong shape are left out.
Permissions readPermissions(const api::Json &permissions);

// Why `value` doesn't fit the setting's type, options or bounds (English), or nothing when it does.
std::optional<std::string> settingRefusal(const Setting &setting, const api::Json &value);
// Every declared setting: the stored value when it still fits, else the default.
api::Json settingValues(const Manifest &manifest, const api::Json &stored);

// Semantic-version order; build metadata is ignored. Both must be valid versions.
int compareVersions(std::string_view a, std::string_view b);

// What `next` asks for that `current` doesn't: the unrestricted tier, every document, network
// hosts, or folders (new ones, or ones that become writable).
struct PermissionIncrease {
    bool unrestricted = false;
    bool allDocuments = false;
    std::vector<std::string> networkHosts;
    std::vector<std::string> files; // file request ids

    bool any() const { return unrestricted || allDocuments || !networkHosts.empty() || !files.empty(); }
};
PermissionIncrease permissionIncrease(const Manifest &current, const Manifest &next);

}
