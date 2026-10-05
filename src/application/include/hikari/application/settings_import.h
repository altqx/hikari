#pragma once

// O3: the one-shot legacy settings importer's readers and review plan
// (docs/qt/proposals/settings-import.md, accepted 2026-09-27 and 2026-09-29:
// S44-import, S44-macro-alias and C04-short-file; the migration worksheet
// docs/qt/proposals/settings-migration-map.md). No file system and no Qt:
// the caller (app::SettingsImportController) discovers the legacy root, hashes
// and snapshots its files, and stages, activates and rolls back generations.
//
// The legacy loaders are never run: config::LoadOptions / LoadAudioOpts /
// SetRawOptions and Hotkeys::LoadHkeys rewrite their inputs (ConfigConverter,
// "The shortcuts file is outdated") and install defaults. The readers here
// reproduce what those loaders put in effect at 20d647c4 (citing them), on a
// snapshot, and say what they found:
// - Config.txt / AudioConfig.txt: SetRawOptions' lines and brace blocks, the
//   ConfigConverter renames for old headers (ConfigNeedToConvert), the
//   record-count rule, AudioConfig's reset of the audio options, and each
//   option's typed reading (GetBool, wxAtoi, GetTable).
// - Hotkeys.txt / AudioHotkeys.txt: LoadHkeys' header rule for four-part
//   build headers (a semantic version header is current:
//   O3-hotkeys-semver-header, approved 2026-10-05), the ConvertHotkeys renames, "<SYMBOL or id> <G|S|E|V|A>=<accel>" and the
//   "Script <file>-<k>=<accel>" automation lines. A short file's valid
//   bindings are read (C04-short-file, approved 2026-09-27).
// - Rules.txt, Dictionary/UserDic.udic and Dictionary/*.dic/.aff pairs are
//   authored collections, carried over byte for byte (R6-dictionary-location).
//
// The plan has a row for every record found and for every registry option
// and default binding the files leave out, with its destination or an explicit
// disposition. Legacy themes are excluded (PROGRAM_THEME, Themes/*.txt and any
// colour key: per-colour theme settings are gone, the theme layer owns
// appearance). The wave-5 retirements (user decisions, 2026-10-05) are dropped
// with a notice: GLOBAL_VIDEO_INDEXING bindings, and TOOLBAR_IDS /
// TOOLBAR_ALIGNMENT (no main toolbar). Unknown and invalid records stay
// visible as unresolved.

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

// Whose reading of the text files (R5-per-platform): the Windows build read in
// text mode, stopping at Ctrl+Z. CRLF reads as LF on every platform
// (O3-linux-crlf-blocks, approved 2026-10-05), where the Linux build kept
// every '\r' and so never closed a brace table.
enum class ReadBy { Windows, Linux };
#ifdef _WIN32
inline constexpr ReadBy kReadBy = ReadBy::Windows;
#else
inline constexpr ReadBy kReadBy = ReadBy::Linux;
#endif

enum class SourceKind { Config, AudioConfig, Hotkeys, AudioHotkeys, Rules, UserDictionary, Dictionary };

// A legacy root's file (legacy pathfull: the folder of the executable).
struct KnownSource {
    SourceKind kind;
    std::string_view path; // relative to the root, '/' separated
};
// Config/Config.txt, Config/AudioConfig.txt, Config/Hotkeys.txt,
// Config/AudioHotkeys.txt, Config/Rules.txt, Dictionary/UserDic.udic
// (config.cpp:542-545, 868; Hotkeys.cpp:241; MisspellReplacer.cpp:262;
// SpellChecker.cpp:49-50). Dictionary/*.dic and *.aff are found by listing.
std::span<const KnownSource> knownSources();

struct SourceFile {
    SourceKind kind = SourceKind::Config;
    std::string path;   // relative to the root ("Config/Config.txt")
    std::string bytes;  // the snapshot's bytes
    std::string sha256; // hex, the caller's
};

// How text bytes were read. Legacy's wxConvAuto read a BOM or valid UTF-8;
// anything else is ambiguous here: the plan offers an explicit
// interpretation rather than a silent guess.
enum class Interpretation { Latin1, Windows1252 };
struct TextEvidence {
    bool utf8Bom = false;
    bool utf16Bom = false;
    bool validUtf8 = false;
    bool decoded = false;
    bool crlf = false; // CR LF pairs were read as LF (O3-linux-crlf-blocks on Linux)
    std::string interpretation; // "UTF-8", "UTF-16LE", "Latin-1", ...; empty when not decoded
};
struct DecodedText {
    std::optional<std::string> text; // UTF-8, CR LF read as LF
    TextEvidence evidence;
};
DecodedText decodeSettingsText(std::string_view bytes, ReadBy readBy,
                               std::optional<Interpretation> interpretation = std::nullopt);

// ---- Config.txt / AudioConfig.txt

struct ConfigRecord {
    int line = 0;           // 1-based line in the file (the block's first line)
    std::string label;      // CatchValsLabs' label, after a ConfigConverter rename
    std::string value;      // its value (a block's whole "{\n\t...\n}")
    std::string renamedFrom; // the old name ConfigConverter replaced, if any
};
struct ConfigFile {
    std::string header;     // the first line's "[...]" name (txt.BeforeFirst(']').Mid(1))
    bool converted = false; // ConfigNeedToConvert: the old names were renamed
    bool crashed = false;   // ended with "___Program Crashed___"
    int count = 0;          // SetRawOptions' g
    std::vector<ConfigRecord> records;
    std::string unterminatedBlock; // a block whose "}" never came (dropped)
};
// config::ConfigNeedToConvert (config.cpp:852-862).
bool configNeedsConversion(std::string_view header);
// ConfigConverter::ConvertConfig's text (ConfigConverter.cpp:503-551), with
// the header line it writes ("[<progname>]"); `renames` gets each renamed
// line's old name, by the converted text's line.
std::string convertConfigText(std::string_view text, std::string_view progname,
                              std::map<int, std::string> *renames = nullptr);
// LoadOptions / LoadAudioOpts up to SetRawOptions (config.cpp:538-577,
// 864-884, 120-153): text as FileOpen gave it.
ConfigFile readConfigFile(std::string_view text);

// GetTable's reading of a brace table (config.cpp:753-765): the text between
// "{\n" and "\n}" split at '\n', each entry without its first character.
enum class TableMode { Strtok, ReturnEmptyAll };
std::vector<std::string> legacyTable(std::string_view value, TableMode mode);

// ---- Hotkeys.txt / AudioHotkeys.txt

struct HotkeyRecord {
    int line = 0;
    std::string text;        // the trimmed line
    bool script = false;     // "Script <file>-<k>=<accel>"
    std::string label;       // the symbol, number or script name
    int window = -1;         // "GSEVA".find(letter), -1 when not one
    std::string accel;       // the binding, "" for a blank record
    std::string renamedFrom; // the old name ConfigConverter replaced, if any
};
struct HotkeyFile {
    std::string header;     // the first line when it starts with '['
    int version = 0;        // wxAtoi of the build number LoadHkeys reads
    bool semanticVersion = false; // "[<name> v<semver>]": read as current (O3-hotkeys-semver-header)
    bool accepted = false;  // read: checkVer, or a semantic version header
    bool converted = false; // version < 1141: ConvertHotkeys renames
    int count = 0;          // LoadHkeys' g
    std::vector<HotkeyRecord> records;
};
// Hotkeys::LoadHkeys (Hotkeys.cpp:236-311) and ConvertHotkeys
// (ConfigConverter.cpp:579-608).
// wxAtoi reads the build number each build's way (hotkeyLabelNumber).
HotkeyFile readHotkeyFile(std::string_view text, bool windows = kReadBy == ReadBy::Windows);

// ---- Migration tables (ConfigConverter::CreateTable, theme exclusions)

struct ConfigAlias {
    std::string_view from;
    std::string_view to;
    std::string_view delimiter; // "", "\f", "|" or ","
};
std::span<const ConfigAlias> configAliases();  // 179
struct NameAlias {
    std::string_view from;
    std::string_view to;
};
std::span<const NameAlias> colourAliases();    // 133 (theme files only)
std::span<const NameAlias> hotkeyAliases();    // 162
// config.h COLOR: the 139 theme colour keys (theme-exclusions.csv).
std::span<const std::string_view> themeColourKeys();
// The options LoadDefaultConfig and LoadDefaultAudioConfig assign.
std::span<const std::string_view> mainDefaultKeys();
std::span<const std::string_view> audioDefaultKeys();
// Retired by the wave-5 user decisions (2026-10-05).
bool isRetiredOption(std::string_view legacyKey);
bool isRetiredAction(int id);

// ---- The plan

enum class RowKind { File, Setting, Shortcut, Macro, Collection };
enum class Disposition {
    Read,       // a source file: what was read from it (evidence)
    Change,     // differs from the destination: import or keep, the user's choice
    Unchanged,  // the destination already has it
    Missing,    // the legacy file leaves it out (no change; a default binding may be unbound)
    Superseded, // legacy did not keep it in effect (a later record, the audio reset...)
    Unresolved, // unknown, invalid, foreign-platform or undecodable: report only
    Excluded,   // legacy themes (accepted exclusion)
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
// The destination profile the plan compares with.
struct Destination {
    // Registry values that are set (Settings::isSet).
    std::map<std::string, SettingValue, std::less<>> values;
    // Authored collection files present, by path in the settings folder
    // ("Rules.txt", "Dictionary/UserDic.udic").
    std::map<std::string, DestinationFile, std::less<>> files;
    // Dictionaries the program ships (names without extension).
    std::set<std::string, std::less<>> bundledDictionaries;
    bool windowsHost = false;
    // Whether a path exists here (a missing one is reported, never replaced).
    std::function<bool(std::string_view)> pathExists;
    // An imported macro binding's problem with the scripts loaded now
    // (missing, basename collision); "" when it resolves. Optional.
    std::function<std::string(std::string_view alias)> macroProblem;
};

// What an activated import recorded (its identity: the source hashes, the
// mapping version and the destination profile) and the rows it imported.
struct Receipt {
    std::vector<std::pair<std::string, std::string>> sources; // path, sha256
    int mappingVersion = kMappingVersion;
    std::string destinationProfile;
    // Row id -> the value it wrote; kept rows are absent.
    std::map<std::string, SettingValue, std::less<>> imported;
    std::set<std::string, std::less<>> kept;
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
// The rows the user's choice imports by default.
std::set<std::string, std::less<>> proposedRows(const Plan &plan);
Receipt receiptOf(const Plan &plan, const std::set<std::string, std::less<>> &chosen, std::string destinationProfile);

// The registry settings whose values are paths (foreign and missing ones are
// reported) and the brace-table options' GetTable mode.
bool isPathSetting(std::string_view id);
bool isPathListSetting(std::string_view id);
// A path written for the other platform: a drive or UNC path on Linux, a
// '/'-rooted one on Windows.
bool isForeignPath(std::string_view path, bool windowsHost);

} // namespace hikari::application::settings_import
