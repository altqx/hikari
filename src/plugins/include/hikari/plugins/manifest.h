#pragma once

#include "hikari/api/json.h"
#include "hikari/minisign/minisign.h"

#include <cstdint>
#include <expected>
#include <map>
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
    std::optional<std::string> vcsRead; // the reason, when asked for
    std::optional<std::string> background; // the reason, when asked for
    std::optional<std::string> programs;   // the reason, when asked for
    std::optional<std::string> presence; // the reason, when asked for
};

struct Command {
    std::string id;
    std::string title;
    std::string icon;              // an icon role or own:<id>, or empty
    std::vector<std::string> when; // conditions that must all hold; "!" negates one
    std::string description;
    bool palette = true;
};

enum class MenuLocation { Grid, LineEditor, MenuBar };

struct MenuPlacement {
    std::string command;
    MenuLocation location = MenuLocation::MenuBar;
    std::string group;
};

struct StatusItem {
    std::string id;
    std::string title;
    std::string text;
    std::string icon;
    std::string tooltip;
    std::string command; // empty: not a button
};

struct Keybinding {
    std::string command;
    std::string key; // Qt's portable text
};

enum class PanelArea { Left, Right, Bottom };

struct Panel {
    std::string id;
    std::string title;
    PanelArea area = PanelArea::Right;
    std::string qml; // a package path: a QML panel; empty: a declarative one
};

enum class SettingType { Boolean, Integer, Number, String, Choice, List, Secret };

struct Setting {
    std::string id;
    std::string title;
    api::Json definition; // the manifest's object
    SettingType type = SettingType::String;

    // A Secret lives in the system keychain, never in plugins.json or a Project, and has no default.
    bool secret() const { return type == SettingType::Secret; }
    // The manifest's default; null for a Secret.
    api::Json fallback() const;
};

struct NodePort {
    std::string name;
    std::string type;  // a Lua node's port type: Number, Text, Colour, Alpha, Time, Point, Bool or Tags
    api::Json fallback; // null: none; else the input's value while no wire reaches it
};

struct NodeContribution {
    std::string id;
    std::string title;
    std::string summary;
    std::string file; // a package .lua path: the node's code
    std::vector<NodePort> inputs, outputs;
};

struct FormatContribution {
    std::string id;
    std::string title;
    std::vector<std::string> extensions; // lower case, without the dot
    bool imports = false;
    bool exports = false;
};

struct ProgramDownload {
    std::string url;
    std::string sha256;
    std::int64_t size = 0;
    std::string archive; // "zip", "tar.gz", or empty for the program itself
    std::string entry;   // with archive: the program's path inside it
};

struct ProgramRun {
    std::string path; // bundled: a package path; empty when downloaded
    std::optional<ProgramDownload> download;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;
};

enum class Confinement { Container, LowIntegrity };

struct Program {
    std::string id;
    std::string title;
    std::map<std::string, ProgramRun> platforms; // by "linux-x64", "windows-x64", …
    bool network = false;
    bool api = false;
    Confinement confinement = Confinement::Container;
};

struct McpServer {
    std::string id;
    std::string title;
    std::string description;
    std::string program;
};

// "linux-x64", "windows-arm64", …: the platform key of the computer HikariSub runs on.
std::string_view thisPlatform();

struct Screenshot {
    std::string path;
    std::string caption;
};

// A content pack file, shown beside its built-in counterpart (docs/plugins.md, Content packs).
struct ContentFile {
    std::string id;
    std::string title; // empty for a karaoke preset, whose file holds its title
    std::string path;  // a package path
};

// A Hunspell pair the spell checker offers after the user's and the bundled dictionaries.
struct Dictionary {
    std::string language; // the Hunspell name, such as en_US
    std::string title;
    std::string aff; // package paths
    std::string dic;
};

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::int64_t api = 1;
    std::string main;        // the Lua main file, or empty
    std::string mainProgram; // a program plugin's program id, or empty
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
    std::vector<Command> commands;
    std::vector<Panel> panels;
    std::vector<MenuPlacement> menus;
    std::vector<StatusItem> statusItems;
    std::vector<Keybinding> keybindings;
    std::vector<Setting> settings;
    std::vector<NodeContribution> nodes;
    std::vector<FormatContribution> formats;
    std::vector<Program> programs;
    std::vector<ContentFile> styles;
    std::vector<ContentFile> karaokePresets;
    std::vector<Dictionary> dictionaries;
    std::vector<ContentFile> graphs;
    std::vector<McpServer> mcpServers;

    bool hasQmlPanels() const;
    const Program *program(std::string_view id) const;
};

// The published JSON Schema, docs/schemas/hikari-plugin.schema.json.
std::string_view manifestSchema();

struct ManifestProblem {
    enum class Kind { NotJson, Schema, DuplicateId, BadDefault, NeedsNewerApi, QmlNeedsFullTrust, UnknownCommand, DuplicateKey };
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
// Every declared setting: the stored value when it still fits, else the default. A Secret is null here;
// its value comes from the keychain only.
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
    bool vcsRead = false;
    bool background = false;
    bool runPrograms = false;
    // Programs that are new, gain network, or move to lowIntegrity.
    std::vector<std::string> programs;
    bool presence = false;
    std::vector<std::string> mcpServers;

    bool any() const
    {
        return unrestricted || allDocuments || !networkHosts.empty() || !files.empty() || vcsRead || background ||
               runPrograms || !programs.empty() || !mcpServers.empty() || presence;
    }
};
PermissionIncrease permissionIncrease(const Manifest &current, const Manifest &next);

}
