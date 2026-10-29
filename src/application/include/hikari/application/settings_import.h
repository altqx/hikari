#pragma once

#include "hikari/application/hotkeys.h"
#include "hikari/application/settings.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::settings_import {

// The mapping's version: a receipt made with another one is a fresh diff.
inline constexpr int kMappingVersion = 1;

// Windows reads stop at Ctrl+Z (text mode); CRLF reads as LF on both.
enum class ReadBy { Windows, Linux };
#ifdef _WIN32
inline constexpr ReadBy kReadBy = ReadBy::Windows;
#else
inline constexpr ReadBy kReadBy = ReadBy::Linux;
#endif

enum class SourceKind { Config, AudioConfig, Hotkeys, AudioHotkeys, Rules, UserDictionary, Dictionary };

// A file under a HikariSub legacy root, the folder of its executable.
struct KnownSource {
    SourceKind kind;
    std::string_view path; // relative to the root, '/' separated
};
// Dictionary/*.dic and *.aff are not listed; they are found by listing the folder.
std::span<const KnownSource> knownSources();

struct SourceFile {
    SourceKind kind = SourceKind::Config;
    std::string path;   // relative to the root ("Config/Config.txt")
    std::string bytes;
    std::string sha256; // hex
};

// For text with no BOM that is not valid UTF-8.
enum class Interpretation { Latin1, Windows1252 };
struct TextEvidence {
    bool utf8Bom = false;
    bool utf16Bom = false;
    bool validUtf8 = false;
    bool decoded = false;
    bool crlf = false; // CR LF pairs were read as LF
    std::string interpretation; // "UTF-8", "UTF-16LE", "Latin-1", ...; empty when not decoded
};
struct DecodedText {
    std::optional<std::string> text; // UTF-8, CR LF read as LF
    TextEvidence evidence;
};
DecodedText decodeSettingsText(std::string_view bytes, ReadBy readBy,
                               std::optional<Interpretation> interpretation = std::nullopt);

struct ConfigRecord {
    int line = 0;           // 1-based line in the file (the block's first line)
    std::string label;      // after any rename
    std::string value;      // a block's whole "{\n\t...\n}"
    std::string renamedFrom; // the old name, if renamed
};
struct ConfigFile {
    std::string header;     // the first line's "[...]" name
    bool converted = false; // old names were renamed
    bool crashed = false;   // ended with "___Program Crashed___"
    int count = 0;          // non-blank lines and blocks
    std::vector<ConfigRecord> records;
    std::string unterminatedBlock; // a block whose "}" never came (dropped)
};
bool configNeedsConversion(std::string_view header);
// Writes the header "[<progname>]"; `renames` gets each renamed line's old
// name, by line of the converted text.
std::string convertConfigText(std::string_view text, std::string_view progname,
                              std::map<int, std::string> *renames = nullptr);
ConfigFile readConfigFile(std::string_view text);

// The text between "{\n" and "\n}" split at '\n', each entry without its first character.
enum class TableMode { Strtok, ReturnEmptyAll };
std::vector<std::string> legacyTable(std::string_view value, TableMode mode);

struct HotkeyRecord {
    int line = 0;
    std::string text;        // the trimmed line
    bool script = false;     // "Script <file>-<k>=<accel>"
    std::string label;       // the symbol, number or script name
    int window = -1;         // "GSEVA".find(letter), -1 when not one
    std::string accel;       // the binding, "" for a blank record
    std::string renamedFrom; // the old name, if renamed
};
struct HotkeyFile {
    std::string header;     // the first line when it starts with '['
    int version = 0;        // the header's build number
    bool semanticVersion = false; // "[<name> v<semver>]": read as current
    bool accepted = false;  // a current build, or a semantic version header
    bool converted = false; // version < 1141: old names renamed
    int count = 0;          // records with a binding
    std::vector<HotkeyRecord> records;
};
// `windows`: read the build number as the Windows build did (hotkeyLabelNumber).
HotkeyFile readHotkeyFile(std::string_view text, bool windows = kReadBy == ReadBy::Windows);

struct ConfigAlias {
    std::string_view from;
    std::string_view to;
    std::string_view delimiter; // "", "\f", "|" or ","
};
std::span<const ConfigAlias> configAliases();
struct NameAlias {
    std::string_view from;
    std::string_view to;
};
std::span<const NameAlias> colourAliases();    // theme files only
std::span<const NameAlias> hotkeyAliases();
std::span<const std::string_view> themeColourKeys();
// The options HikariSub legacy assigns defaults to.
std::span<const std::string_view> mainDefaultKeys();
std::span<const std::string_view> audioDefaultKeys();
// The reason is the plan row's notice; empty: not retired.
bool isRetiredOption(std::string_view legacyKey);
bool isRetiredAction(int id);
std::string_view retiredOptionReason(std::string_view legacyKey);
std::string_view retiredActionReason(int id);

enum class RowKind { File, Setting, Shortcut, Macro, Collection };
enum class Disposition {
    Read,       // a source file: what was read from it (evidence)
    Change,     // differs from the destination: import or keep, the user's choice
    Unchanged,  // the destination already has it
    Missing,    // the legacy file leaves it out (no change; a default binding may be unbound)
    Superseded, // legacy did not keep it in effect (a later record, the audio reset...)
    Unresolved, // unknown, invalid, foreign-platform or undecodable: report only
    Excluded,   // legacy themes
    Retired,    // retired action or option: dropped with a notice
};
std::string_view dispositionName(Disposition d);

struct PlanRow {
    std::string id;          // stable: "setting:grid.font", "shortcut:GLOBAL_SAVE_SUBS:G", ...
    RowKind kind = RowKind::Setting;
    std::string source;      // "Config/Config.txt:12"
    std::string sourceKey;   // the legacy key, symbol or file
    std::string raw;         // the original token
    std::string destination; // registry id, "<SYMBOL> <window>", macro name or file path
    std::optional<SettingValue> value;   // what an import writes
    std::optional<SettingValue> current; // what the destination holds
    Disposition disposition = Disposition::Change;
    bool selectable = false;
    bool proposedImport = false;
    std::string reason;
    std::vector<std::string> unresolvedPaths; // foreign-platform or missing entries, kept
    // Shortcut rows: the binding's (id, window). Collection rows: the files
    // written, by path in the settings folder.
    HotkeyId binding;
    std::vector<std::string> files;
};

struct DestinationFile {
    std::string bytes;
    std::string sha256;
};
struct Destination {
    // Only values that are set.
    std::map<std::string, SettingValue, std::less<>> values;
    // Authored collection files present, by path in the settings folder
    // ("rules.toml", "Dictionary/UserDic.udic").
    std::map<std::string, DestinationFile, std::less<>> files;
    // Dictionaries the program ships (names without extension).
    std::set<std::string, std::less<>> bundledDictionaries;
    bool windowsHost = false;
    // Whether a path exists here (a missing one is reported, never replaced).
    std::function<bool(std::string_view)> pathExists;
    // An imported macro binding's problem with the scripts loaded now
    // (missing, basename collision); "" when it resolves. Optional.
    std::function<std::string(std::string_view alias)> macroProblem;
    // A VSFILTER_INSTANCE naming a renderer not installed stays unresolved.
    // Unset: not checked.
    std::function<bool(std::string_view name)> rendererInstalled;
    // rules.toml's text for Rules.txt's bytes. Unset: the rules are not imported.
    std::function<std::string(std::string_view legacy)> convertRules;
};

// Identity: the source hashes, the mapping version and the destination profile.
struct Receipt {
    std::vector<std::pair<std::string, std::string>> sources; // path, sha256
    int mappingVersion = kMappingVersion;
    std::string destinationProfile;
    // Row id -> the value it wrote; kept rows are absent.
    std::map<std::string, SettingValue, std::less<>> imported;
    std::set<std::string, std::less<>> kept;
    // The legacy root it was read from; not part of the identity.
    std::string root;
    bool sameIdentity(const Receipt &other) const;
};

struct PlanOptions {
    ReadBy readBy = kReadBy;
    std::optional<Interpretation> interpretation; // for files that are not UTF-8
    // Theme files found in the root (Themes/*.txt): named in the plan only.
    std::vector<std::string> themeFiles;
    const Receipt *previous = nullptr;
};

struct Plan {
    std::vector<PlanRow> rows;
    std::vector<std::pair<std::string, std::string>> sources; // path, sha256
    // The collection files an import writes, by path in the settings folder.
    std::map<std::string, std::string, std::less<>> files;
    bool ambiguousEncoding = false; // a file needs an explicit interpretation
    int mappingVersion = kMappingVersion;
    const PlanRow *row(std::string_view id) const;
};

Plan buildPlan(const std::vector<SourceFile> &sources, const Destination &destination, const PlanOptions &options = {});

// The complete profile after importing the rows `chosen` names (selectable
// Change and Missing rows; others are ignored) over `destination`.
struct Profile {
    std::map<std::string, SettingValue, std::less<>> values;
    std::map<std::string, std::string, std::less<>> files;
    friend bool operator==(const Profile &, const Profile &) = default;
};
Profile profileOf(const Destination &destination);
Profile applyPlan(const Plan &plan, const std::set<std::string, std::less<>> &chosen, const Destination &destination);
std::set<std::string, std::less<>> proposedRows(const Plan &plan);
Receipt receiptOf(const Plan &plan, const std::set<std::string, std::less<>> &chosen, std::string destinationProfile);

// Foreign and missing paths in these settings are reported.
bool isPathSetting(std::string_view id);
bool isPathListSetting(std::string_view id);
// A path written for the other platform: a drive or UNC path on Linux, a
// '/'-rooted one on Windows.
bool isForeignPath(std::string_view path, bool windowsHost);

} // namespace hikari::application::settings_import
