// O3: the one-shot settings importer's readers and review plan against the
// legacy loaders at 20d647c4 (config.cpp SetRawOptions / LoadOptions /
// LoadAudioOpts, Hotkeys.cpp LoadHkeys, ConfigConverter.cpp) and the
// migration worksheet (docs/qt/proposals/settings-migration-map): every
// option and action row reaches a destination or an explicit disposition,
// C04-short-file, S44-import's no-op repeat and kept user edits,
// S44-macro-alias, foreign-platform paths, the theme exclusion and the
// wave-5 retirements. The generations (snapshot, activation, rollback) are
// hikari_ui_settings_import_tests.

#include "hikari/application/settings_import.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/settings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>

using namespace hikari::application;
using namespace hikari::application::settings_import;

namespace {

std::string readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// A CSV line of quoted fields ("" inside a field is a quote).
std::vector<std::string> csvFields(const std::string &line)
{
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(std::move(field));
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    out.push_back(std::move(field));
    return out;
}

std::vector<std::vector<std::string>> csvRows(const std::string &path)
{
    std::istringstream csv(readFile(path));
    std::string line;
    std::getline(csv, line); // header
    std::vector<std::vector<std::string>> rows;
    while (std::getline(csv, line))
        if (!line.empty())
            rows.push_back(csvFields(line));
    return rows;
}

std::string fakeHash(std::string_view bytes)
{
    return std::to_string(std::hash<std::string_view>{}(bytes));
}

SourceFile source(SourceKind kind, std::string path, std::string bytes)
{
    SourceFile s;
    s.kind = kind;
    s.path = std::move(path);
    s.sha256 = fakeHash(bytes);
    s.bytes = std::move(bytes);
    return s;
}

// The running legacy build's header (config.cpp:67; VersionHikariSub.h:35).
constexpr std::string_view kHeader = "[HikariSub v0.0.1-rc.1]\r\n";
// A header of the four-part builds LoadHkeys accepts ("0.8.0.build" past
// 487, Hotkeys.cpp:257-268) and ConfigNeedToConvert leaves alone (>= 1142).
constexpr std::string_view kBuildHeader = "[Kainote v0.9.0.1500]\r\n";

SourceFile config(std::string_view body, std::string_view header = kHeader)
{
    return source(SourceKind::Config, "Config/Config.txt", std::string(header) + std::string(body));
}

SourceFile audioConfig(std::string_view body, std::string_view header = kHeader)
{
    return source(SourceKind::AudioConfig, "Config/AudioConfig.txt", std::string(header) + std::string(body));
}

SourceFile hotkeys(std::string_view body, std::string_view header = kBuildHeader)
{
    return source(SourceKind::Hotkeys, "Config/Hotkeys.txt", std::string(header) + std::string(body));
}

SourceFile audioHotkeys(std::string_view body, std::string_view header = kBuildHeader)
{
    return source(SourceKind::AudioHotkeys, "Config/AudioHotkeys.txt", std::string(header) + std::string(body));
}

// Eleven records of padding: more than SetRawOptions' ten (config.cpp:150).
std::string padding()
{
    std::string out;
    for (int i = 1; i <= 11; ++i)
        out += "EDITBOX_TAG_BUTTON_VALUE" + std::to_string(i) + "=\r\n";
    return out;
}

Destination linuxHost()
{
    Destination d;
    d.windowsHost = false;
    return d;
}

PlanOptions readBy(ReadBy r)
{
    PlanOptions o;
    o.readBy = r;
    return o;
}

const PlanRow &rowOf(const Plan &plan, std::string_view id)
{
    const PlanRow *row = plan.row(id);
    EXPECT_NE(row, nullptr) << id;
    static const PlanRow none;
    return row ? *row : none;
}

std::vector<const PlanRow *> rowsKeyed(const Plan &plan, std::string_view key)
{
    std::vector<const PlanRow *> out;
    for (const auto &r : plan.rows)
        if (r.sourceKey == key)
            out.push_back(&r);
    return out;
}

// A raw Config.txt token other than the default, per type.
std::string sampleRaw(const SettingDefinition &def)
{
    switch (def.type) {
    case SettingType::Bool:
        return std::get<bool>(def.defaultValue) ? "false" : "true";
    case SettingType::Int:
        return std::to_string(std::get<std::int64_t>(def.defaultValue) + 7);
    case SettingType::String:
        return std::get<std::string>(def.defaultValue) + "x";
    case SettingType::StringList:
        return "{\n\ta\n\t\n\tb\n}";
    }
    return {};
}

// A destination profile after an import (its values and files).
Destination destinationOf(const Profile &profile, Destination base = linuxHost())
{
    base.values = profile.values;
    base.files.clear();
    for (const auto &[path, bytes] : profile.files)
        base.files[path] = DestinationFile{bytes, fakeHash(bytes)};
    return base;
}

} // namespace

// ---- The tables against the legacy sources and the worksheet

TEST(SettingsImport, AliasTablesAreConfigConvertersTables)
{
    // ConfigConverter::CreateTable (ConfigConverter.cpp:20-501).
    const std::string src = readFile(HIKARI_LEGACY_DIR "/ConfigConverter.cpp");
    std::map<std::string, std::pair<std::string, std::string>> config;
    std::map<std::string, std::string> colours, keys;
    const std::regex configRe(
        R"re(convertConfig\[L"([^"]*)"\] = std::pair<wxString, wxString>\(L"([^"]*)", (emptyString|L"([^"]*)")\);)re");
    for (std::sregex_iterator it(src.begin(), src.end(), configRe), end; it != end; ++it) {
        std::string delim = (*it)[3] == "emptyString" ? std::string() : std::string((*it)[4]);
        if (delim == "\\f")
            delim = "\f";
        config[(*it)[1]] = {(*it)[2], delim};
    }
    const std::regex nameRe(R"re(convert(Colors|Hotkeys)\[L"([^"]*)"\] = L"([^"]*)";)re");
    for (std::sregex_iterator it(src.begin(), src.end(), nameRe), end; it != end; ++it)
        ((*it)[1] == "Colors" ? colours : keys)[(*it)[2]] = (*it)[3];

    // The worksheet's legacy-aliases.csv: 179 option, 133 colour, 162 shortcut.
    ASSERT_EQ(configAliases().size(), 179u);
    ASSERT_EQ(colourAliases().size(), 133u);
    ASSERT_EQ(hotkeyAliases().size(), 162u);
    EXPECT_EQ(config.size(), configAliases().size());
    for (const auto &a : configAliases()) {
        ASSERT_TRUE(config.contains(std::string(a.from))) << a.from;
        EXPECT_EQ(config.at(std::string(a.from)).first, a.to) << a.from;
        EXPECT_EQ(config.at(std::string(a.from)).second, a.delimiter) << a.from;
    }
    EXPECT_EQ(colours.size(), colourAliases().size());
    for (const auto &a : colourAliases())
        EXPECT_EQ(colours[std::string(a.from)], a.to) << a.from;
    EXPECT_EQ(keys.size(), hotkeyAliases().size());
    for (const auto &a : hotkeyAliases())
        EXPECT_EQ(keys[std::string(a.from)], a.to) << a.from;

    std::map<std::string, int> family;
    for (const auto &row : csvRows(HIKARI_MIGRATION_MAP "/legacy-aliases.csv"))
        ++family[row.at(0)];
    EXPECT_EQ(family["Config"], 179);
    EXPECT_EQ(family["Colors"], 133);
    EXPECT_EQ(family["Hotkeys"], 162);
}

TEST(SettingsImport, ThemeColourKeysAreConfigHsColors)
{
    // config.h CLR (config.h:279-420) and the worksheet's theme-exclusions.csv.
    const std::string h = readFile(HIKARI_LEGACY_DIR "/config.h");
    const auto begin = h.find("#define CLR(CR)");
    const auto end = h.find("DECLARE_ENUM(COLOR, CLR)");
    const std::string body = h.substr(begin, end - begin);
    std::vector<std::string> legacy;
    const std::regex cr(R"(CR\((\w+),)");
    for (std::sregex_iterator it(body.begin(), body.end(), cr), e; it != e; ++it)
        legacy.push_back((*it)[1]);
    ASSERT_EQ(legacy.size(), 139u);
    ASSERT_EQ(themeColourKeys().size(), 139u);
    for (std::size_t i = 0; i < legacy.size(); ++i)
        EXPECT_EQ(themeColourKeys()[i], legacy[i]);
    std::set<std::string> csv;
    for (const auto &row : csvRows(HIKARI_MIGRATION_MAP "/theme-exclusions.csv")) {
        csv.insert(row.at(0));
        EXPECT_EQ(row.at(2), "exclude-accepted-theme");
    }
    EXPECT_EQ(csv, std::set<std::string>(legacy.begin(), legacy.end()));
}

TEST(SettingsImport, DefaultKeysAreLoadDefaultConfigsAssignments)
{
    // config.cpp LoadDefaultConfig (346-) and LoadDefaultAudioConfig (806-).
    const std::string src = readFile(HIKARI_LEGACY_DIR "/config.cpp");
    auto assigned = [&](std::string_view function) {
        const auto begin = src.find(function);
        const auto end = src.find("\n}", begin);
        const std::string body = src.substr(begin, end - begin);
        std::vector<std::string> out;
        const std::regex re(R"(configTable\[(\w+)\] =)");
        for (std::sregex_iterator it(body.begin(), body.end(), re), e; it != e; ++it)
            if (std::ranges::find(out, (*it)[1].str()) == out.end())
                out.push_back((*it)[1]);
        return out;
    };
    const auto main = assigned("void config::LoadDefaultConfig");
    const auto audio = assigned("void config::LoadDefaultAudioConfig");
    EXPECT_EQ(std::vector<std::string>(mainDefaultKeys().begin(), mainDefaultKeys().end()), main);
    EXPECT_EQ(std::vector<std::string>(audioDefaultKeys().begin(), audioDefaultKeys().end()), audio);
}

TEST(SettingsImport, KnownSourcesAreLegacysFiles)
{
    std::vector<std::string> paths;
    for (const auto &s : knownSources())
        paths.emplace_back(s.path);
    EXPECT_EQ(paths, (std::vector<std::string>{"Config/Config.txt", "Config/AudioConfig.txt", "Config/Hotkeys.txt",
                                               "Config/AudioHotkeys.txt", "Config/Rules.txt",
                                               "Dictionary/UserDic.udic"}));
}

// ---- The migration worksheet: every row reaches a destination or a disposition

TEST(SettingsImport, EveryWorksheetOptionReachesADestinationOrADisposition)
{
    std::string mainBody, audioBody;
    for (const auto &def : settingDefinitions()) {
        if (def.legacyKey.empty())
            continue;
        (def.legacyAudioFile ? audioBody : mainBody) +=
            std::string(def.legacyKey) + "=" + sampleRaw(def) + "\r\n";
    }
    const Plan plan = buildPlan({config(mainBody), audioConfig(audioBody)}, linuxHost(), readBy(ReadBy::Windows));
    const auto rows = csvRows(HIKARI_MIGRATION_MAP "/options.csv");
    ASSERT_EQ(rows.size(), 207u);
    int audio = 0;
    for (const auto &csv : rows) {
        const std::string &key = csv.at(1);
        audio += csv.at(2) == "AudioConfig.txt";
        const SettingDefinition *def = findLegacySetting(key);
        ASSERT_NE(def, nullptr) << key;
        EXPECT_EQ(def->legacyAudioFile, csv.at(2) == "AudioConfig.txt") << key;
        const auto found = rowsKeyed(plan, key);
        ASSERT_EQ(found.size(), 1u) << key;
        const PlanRow &row = *found.front();
        EXPECT_EQ(row.destination, def->id) << key;
        EXPECT_EQ(row.source.substr(0, row.source.find(':')),
                  def->legacyAudioFile ? "Config/AudioConfig.txt" : "Config/Config.txt")
            << key;
        if (key == "PROGRAM_THEME") {
            EXPECT_EQ(row.disposition, Disposition::Excluded);
            EXPECT_EQ(csv.at(9), "exclude-accepted-theme");
        } else if (key == "TOOLBAR_IDS" || key == "TOOLBAR_ALIGNMENT") {
            EXPECT_EQ(row.disposition, Disposition::Retired) << key;
            EXPECT_NE(row.reason.find("no main toolbar"), std::string::npos);
        } else {
            EXPECT_EQ(row.disposition, Disposition::Change) << key << " " << row.reason;
            EXPECT_TRUE(row.selectable) << key;
            EXPECT_TRUE(row.proposedImport) << key;
            ASSERT_TRUE(row.value.has_value()) << key;
        }
    }
    EXPECT_EQ(audio, 34);

    // Importing writes each value to its setting, typed; the excluded and
    // retired ones write nothing.
    const Profile profile = applyPlan(plan, proposedRows(plan), linuxHost());
    for (const auto &def : settingDefinitions()) {
        if (def.legacyKey.empty())
            continue;
        const auto it = profile.values.find(def.id);
        if (def.legacyKey == "PROGRAM_THEME" || isRetiredOption(def.legacyKey)) {
            EXPECT_EQ(it, profile.values.end()) << def.id;
            continue;
        }
        ASSERT_NE(it, profile.values.end()) << def.id;
        switch (def.type) {
        case SettingType::Bool:
            EXPECT_EQ(it->second, SettingValue(!std::get<bool>(def.defaultValue))) << def.id;
            break;
        case SettingType::Int:
            EXPECT_EQ(it->second, SettingValue(std::get<std::int64_t>(def.defaultValue) + 7)) << def.id;
            break;
        case SettingType::String:
            EXPECT_EQ(it->second, SettingValue(std::get<std::string>(def.defaultValue) + "x")) << def.id;
            break;
        case SettingType::StringList:
            // GetTable (config.cpp:753-765): the entry "\t" is a token in
            // either mode, read as "" (SetRawOptions never keeps an empty
            // line in a block, so the modes read a file's tables alike).
            EXPECT_EQ(it->second, SettingValue(std::vector<std::string>{"a", "", "b"})) << def.id;
            break;
        }
    }
    EXPECT_EQ(legacyTable("{\n\ta\n\n\tb\n}", TableMode::Strtok), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(legacyTable("{\n\ta\n\n\tb\n}", TableMode::ReturnEmptyAll), (std::vector<std::string>{"a", "", "b"}));
}

TEST(SettingsImport, OptionsTheFilesLeaveOutAreMissingWithTheCurrentValueKept)
{
    const Plan plan =
        buildPlan({config(padding() + "GRID_FONT=Arial\r\n")}, linuxHost(), readBy(ReadBy::Windows));
    const PlanRow &font = rowOf(plan, "setting:grid.font");
    EXPECT_EQ(font.disposition, Disposition::Change);
    // Every other registry option has its row, missing, with nothing to write.
    for (const auto &def : settingDefinitions()) {
        if (def.legacyKey.empty() || def.legacyKey == "GRID_FONT" || def.legacyKey.starts_with("EDITBOX_TAG_BUTTON_VALUE"))
            continue;
        if (def.disposition == SettingDisposition::Excluded || isRetiredOption(def.legacyKey)) {
            EXPECT_EQ(plan.row("setting:" + std::string(def.id)), nullptr) << def.id;
            continue;
        }
        // Without AudioConfig.txt legacy loads the audio defaults
        // (config.cpp:868-870): missing as well.
        const PlanRow &row = rowOf(plan, "setting:" + std::string(def.id));
        EXPECT_EQ(row.disposition, Disposition::Missing) << def.id;
        EXPECT_FALSE(row.selectable) << def.id;
        EXPECT_FALSE(row.value.has_value()) << def.id;
    }
}

TEST(SettingsImport, EveryWorksheetActionReachesADestinationOrADisposition)
{
    // One binding per action in its own window (GetType), to a chord no
    // default has.
    std::string mainBody, audioBody;
    for (const auto &a : hotkeyActions()) {
        const int type = hotkeyType(a.id);
        std::string line = std::string(a.symbol) + " " + "GSEVA"[type] + "=Ctrl-Alt-Shift-F12\r\n";
        (type == AudioHotkey ? audioBody : mainBody) += line;
    }
    const Plan plan = buildPlan({hotkeys(mainBody), audioHotkeys(audioBody)}, linuxHost(), readBy(ReadBy::Windows));
    const auto rows = csvRows(HIKARI_MIGRATION_MAP "/actions.csv");
    ASSERT_EQ(rows.size(), 243u);
    for (const auto &csv : rows) {
        const std::string &symbol = csv.at(0);
        const int id = std::stoi(csv.at(1));
        ASSERT_EQ(hotkeyIdOf(symbol), id) << symbol;
        const int type = hotkeyType(id);
        const std::string rowId = "shortcut:" + symbol + ":" + std::string(1, "GSEVA"[type]);
        const PlanRow &row = rowOf(plan, rowId);
        EXPECT_EQ(row.binding, (HotkeyId{id, type})) << symbol;
        EXPECT_EQ(row.destination, symbol + " (" + std::string(hotkeyWindowName(type)) + ")");
        if (symbol == "GLOBAL_VIDEO_INDEXING") {
            EXPECT_EQ(row.disposition, Disposition::Retired);
            EXPECT_FALSE(row.selectable);
        } else {
            EXPECT_EQ(row.disposition, Disposition::Change) << symbol << " " << row.reason;
            EXPECT_EQ(row.value, SettingValue(std::string("Ctrl-Alt-Shift-F12"))) << symbol;
            EXPECT_TRUE(row.selectable) << symbol;
        }
    }
    // Imported, the bindings are SaveHkeys lines in the registry; the retired
    // action keeps no binding from the file.
    const Profile profile = applyPlan(plan, proposedRows(plan), linuxHost());
    HotkeyMap map;
    readHotkeyLines(map, std::get<std::vector<std::string>>(profile.values.at(std::string(kHotkeysSetting))));
    readHotkeyLines(map, std::get<std::vector<std::string>>(profile.values.at(std::string(kAudioHotkeysSetting))));
    int bound = 0;
    for (const auto &a : hotkeyActions()) {
        const auto it = map.find(HotkeyId{a.id, hotkeyType(a.id)});
        if (a.symbol == "GLOBAL_VIDEO_INDEXING") {
            EXPECT_EQ(it, map.end());
            continue;
        }
        ASSERT_NE(it, map.end()) << a.symbol;
        bound += it->second.accel == "Ctrl-Alt-Shift-F12";
    }
    EXPECT_EQ(bound, 242);
}

// ---- Config.txt / AudioConfig.txt readings

TEST(SettingsImport, ConfigRecordsAreReadAsSetRawOptionsReadsThem)
{
    // Lines trimmed, a label's trailing blanks dropped, the value as it is
    // after '=' (CatchValsLabs, config.cpp:259-268); blocks from "...{" to a
    // line "}" (config.cpp:129-145); empty lines skipped.
    const ConfigFile f = readConfigFile("[HikariSub v0.0.1]\n  GRID_FONT  = Arial \n\nSUBS_RECENT_FILES={\n\ta\n\tb\n}\n"
                                        "NO_EQUALS\n___Program Crashed___");
    EXPECT_EQ(f.header, "HikariSub v0.0.1");
    EXPECT_TRUE(f.crashed);
    EXPECT_FALSE(f.converted);
    ASSERT_EQ(f.records.size(), 3u);
    EXPECT_EQ(f.records[0].label, "GRID_FONT");
    EXPECT_EQ(f.records[0].value, " Arial");
    EXPECT_EQ(f.records[0].line, 2);
    EXPECT_EQ(f.records[1].label, "SUBS_RECENT_FILES");
    EXPECT_EQ(f.records[1].value, "{\n\ta\n\tb\n}");
    EXPECT_EQ(f.records[1].line, 4);
    EXPECT_EQ(f.records[2].label, "NO_EQUALS");
    EXPECT_EQ(f.records[2].value, "");
    EXPECT_EQ(f.count, 3);

    // A block whose "}" never comes is dropped.
    const ConfigFile open = readConfigFile("[x v0.0.1]\nSUBS_RECENT_FILES={\n\ta\n");
    EXPECT_TRUE(open.records.empty());
    EXPECT_EQ(open.unterminatedBlock, "SUBS_RECENT_FILES={\n\ta\n");
}

TEST(SettingsImport, CrlfIsReadAsEachPlatformsBuildReadIt)
{
    // R5-per-platform: the Windows build's text mode folds CRLF; the Linux
    // build keeps '\r', so a brace table's "}\r" is no closing line and the
    // block runs on (config.cpp:129-145).
    const std::string body = padding() + "SUBS_RECENT_FILES={\r\n\ta.ass\r\n}\r\nGRID_FONT=Arial\r\n";
    const Plan windows = buildPlan({config(body)}, linuxHost(), readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(windows, "setting:recent.subtitles").value,
              SettingValue(std::vector<std::string>{"a.ass"}));
    EXPECT_EQ(rowOf(windows, "setting:grid.font").value, SettingValue(std::string("Arial")));

    const auto text = decodeSettingsText(config(body).bytes, ReadBy::Linux);
    ASSERT_TRUE(text.text);
    const ConfigFile linux = readConfigFile(*text.text);
    // "SUBS_RECENT_FILES={\r" does not end with '{': a plain record "{".
    const auto recent = std::ranges::find(linux.records, std::string("SUBS_RECENT_FILES"), &ConfigRecord::label);
    ASSERT_NE(recent, linux.records.end());
    EXPECT_EQ(recent->value, "{");
}

TEST(SettingsImport, ShortConfigFileIsWhatEachBuildPutInEffect)
{
    const std::string body = "GRID_FONT=Arial\r\nSPELLCHECKER_ON=false\r\nGRID_HIDE_COLUMNS=4\r\n";
    // Linux: LoadDefaultConfig over a file of ten records or fewer
    // (config.cpp:569-574): the defaulted options lose their values, the
    // others keep them.
    const Plan linux = buildPlan({config(body)}, linuxHost(), readBy(ReadBy::Linux));
    EXPECT_EQ(rowOf(linux, "superseded:Config/Config.txt:2").disposition, Disposition::Superseded);
    EXPECT_EQ(rowOf(linux, "superseded:Config/Config.txt:3").disposition, Disposition::Superseded);
    EXPECT_EQ(linux.row("setting:grid.font"), nullptr);
    EXPECT_EQ(rowOf(linux, "setting:grid.hideColumns").disposition, Disposition::Change);
    EXPECT_NE(rowOf(linux, "file:Config/Config.txt").reason.find("10 or fewer"), std::string::npos);
    // Windows: legacy refused to start (config.cpp:575-577,
    // hikarisubApp.cpp:320): nothing from it was in effect.
    const Plan windows = buildPlan({config(body)}, linuxHost(), readBy(ReadBy::Windows));
    for (const int line : {2, 3, 4})
        EXPECT_EQ(rowOf(windows, "superseded:Config/Config.txt:" + std::to_string(line)).disposition,
                  Disposition::Superseded);
    EXPECT_EQ(windows.row("setting:grid.hideColumns"), nullptr);
    EXPECT_TRUE(proposedRows(windows).empty());
}

TEST(SettingsImport, LaterRecordsAndTheAudioResetSupersede)
{
    // The last record of a label is the one in effect.
    const Plan plan = buildPlan({config(padding() + "GRID_FONT=Arial\r\nGRID_FONT=Verdana\r\n")}, linuxHost(),
                                readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(plan, "setting:grid.font").value, SettingValue(std::string("Verdana")));
    EXPECT_EQ(rowOf(plan, "superseded:Config/Config.txt:13").disposition, Disposition::Superseded);

    // LoadAudioOpts: AudioConfig.txt of another build resets the audio
    // options (config.cpp:875), Config.txt's among them.
    const Plan reset = buildPlan({config(padding() + "AUDIO_DELAY=40\r\n"),
                                  audioConfig(padding() + "AUDIO_VOLUME=20\r\n", "[HikariSub v0.0.0]\r\n")},
                                 linuxHost(), readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(reset, "superseded:Config/Config.txt:13").disposition, Disposition::Superseded);
    EXPECT_EQ(rowOf(reset, "setting:audio.volume").value, SettingValue(std::int64_t{20}));
}

TEST(SettingsImport, MalformedValuesStayUnresolvedWithLegacysReading)
{
    const Plan plan = buildPlan({config(padding() + "SPELLCHECKER_ON=yes\r\nGRID_FONT_SIZE=12px\r\nSUBS_RECENT_FILES=a.ass\r\n"
                                                    "PROGRAM_LANGUAGE=1\r\n")},
                                linuxHost(), readBy(ReadBy::Windows));
    const PlanRow &spell = rowOf(plan, "setting:editor.spellchecker");
    EXPECT_EQ(spell.disposition, Disposition::Unresolved);
    EXPECT_FALSE(spell.selectable);
    EXPECT_EQ(spell.raw, "yes");
    EXPECT_NE(spell.reason.find("GetBool"), std::string::npos);
    const PlanRow &size = rowOf(plan, "setting:grid.fontSize");
    EXPECT_EQ(size.disposition, Disposition::Unresolved);
    EXPECT_EQ(size.value, SettingValue(std::int64_t{12})); // wxAtoi
    EXPECT_EQ(rowOf(plan, "setting:recent.subtitles").disposition, Disposition::Unresolved);
    // hikarisubApp.cpp:331-336: "1" is read as no language.
    const PlanRow &language = rowOf(plan, "setting:program.language");
    EXPECT_EQ(language.value, SettingValue(std::string()));
    EXPECT_NE(language.reason.find("hikarisubApp.cpp:331-336"), std::string::npos);
}

TEST(SettingsImport, OldHeadersAreReadThroughConfigConvertersRenames)
{
    // A build before 1142 (config.cpp:852-862): ConvertConfig's renames, its
    // delimiter tables and the header line it adds, with the file unchanged.
    EXPECT_TRUE(configNeedsConversion("Kainote v0.8.0.1100"));
    EXPECT_FALSE(configNeedsConversion("Kainote v0.9.0.1142"));
    EXPECT_FALSE(configNeedsConversion("HikariSub v0.0.1-rc.1"));
    EXPECT_TRUE(configNeedsConversion("HikariSub"));
    std::string body = padding();
    body += "GridFontName=Arial\r\nEditboxTagButton1=\\i1\fItalic\r\nToolbarIDs=PlayPause|NotAName\r\n"
            "AudioAutoCommit=false\r\n";
    const Plan plan = buildPlan({config(body, "[Kainote v0.8.0.1100]\r\n")}, linuxHost(), readBy(ReadBy::Windows));
    const PlanRow &font = rowOf(plan, "setting:grid.font");
    EXPECT_EQ(font.value, SettingValue(std::string("Arial")));
    EXPECT_NE(font.reason.find("renamed from GridFontName"), std::string::npos);
    // The '\f' delimiter becomes the brace table's entries.
    EXPECT_EQ(rowOf(plan, "setting:editor.tagButton1").raw, "{\n\t\\i1\n\tItalic\n}");
    // An audio option in Config.txt: without AudioConfig.txt legacy loads
    // the audio defaults over it (config.cpp:868-870).
    const auto commit = rowsKeyed(plan, "AUDIO_AUTO_COMMIT");
    ASSERT_EQ(commit.size(), 1u);
    EXPECT_EQ(commit.front()->disposition, Disposition::Superseded);
    // ToolbarIDs is converted through the hotkey names, then retired.
    const auto toolbar = rowsKeyed(plan, "TOOLBAR_IDS");
    ASSERT_EQ(toolbar.size(), 1u);
    EXPECT_EQ(toolbar.front()->raw, "{\n\tVIDEO_PLAY_PAUSE\n\tNotAName\n}");
    EXPECT_EQ(toolbar.front()->disposition, Disposition::Retired);
    EXPECT_NE(rowOf(plan, "file:Config/Config.txt").reason.find("ConfigConverter"), std::string::npos);

    // Under a current header an old name is unknown, not renamed.
    const Plan current = buildPlan({config(padding() + "AudioAutoCommit=false\r\n")}, linuxHost(),
                                   readBy(ReadBy::Windows));
    const auto old = rowsKeyed(current, "AudioAutoCommit");
    ASSERT_EQ(old.size(), 1u);
    EXPECT_EQ(old.front()->disposition, Disposition::Unresolved);
}

// ---- Themes, unknown keys and the wave-5 retirements

TEST(SettingsImport, ThemesAndColourKeysAreExcludedWithANotice)
{
    PlanOptions o = readBy(ReadBy::Windows);
    o.themeFiles = {"Themes/DarkSentro.txt", "Themes/Mine.txt"};
    const Plan plan = buildPlan({config(padding() + "PROGRAM_THEME=Mine\r\nWINDOW_BACKGROUND=#102030\r\n"
                                                    "WindowBackground=#000000\r\nNOT_AN_OPTION=1\r\n")},
                                linuxHost(), o);
    const auto theme = rowsKeyed(plan, "PROGRAM_THEME");
    ASSERT_EQ(theme.size(), 1u);
    EXPECT_EQ(theme.front()->disposition, Disposition::Excluded);
    EXPECT_FALSE(theme.front()->selectable);
    for (const auto *key : {"WINDOW_BACKGROUND", "WindowBackground"}) {
        const auto colour = rowsKeyed(plan, key);
        ASSERT_EQ(colour.size(), 1u) << key;
        EXPECT_EQ(colour.front()->disposition, Disposition::Excluded) << key;
        EXPECT_NE(colour.front()->reason.find("per-colour theme settings are gone"), std::string::npos);
    }
    for (const auto *file : {"file:Themes/DarkSentro.txt", "file:Themes/Mine.txt"}) {
        EXPECT_EQ(rowOf(plan, file).disposition, Disposition::Excluded);
        EXPECT_NE(rowOf(plan, file).reason.find("legacy themes are not imported"), std::string::npos);
    }
    const auto unknown = rowsKeyed(plan, "NOT_AN_OPTION");
    ASSERT_EQ(unknown.size(), 1u);
    EXPECT_EQ(unknown.front()->disposition, Disposition::Unresolved);
    EXPECT_EQ(unknown.front()->raw, "1");
    // Nothing of them is written.
    const Profile profile = applyPlan(plan, proposedRows(plan), linuxHost());
    EXPECT_FALSE(profile.values.contains("program.theme"));
}

TEST(SettingsImport, RetiredOptionsAndActionsAreDroppedWithANotice)
{
    const Plan plan = buildPlan({config(padding() + "TOOLBAR_IDS={\n\tGLOBAL_SAVE_SUBS\n}\r\nTOOLBAR_ALIGNMENT=2\r\n"),
                                 hotkeys("GLOBAL_VIDEO_INDEXING G=Ctrl-Alt-I\r\n5015 S=Ctrl-Alt-J\r\n")},
                                linuxHost(), readBy(ReadBy::Windows));
    for (const auto *key : {"TOOLBAR_IDS", "TOOLBAR_ALIGNMENT"}) {
        const PlanRow &row = rowOf(plan, std::string("setting:") + key);
        EXPECT_EQ(row.disposition, Disposition::Retired) << key;
        EXPECT_FALSE(row.selectable);
    }
    // By symbol and by number (Hotkeys.cpp:300-301).
    for (const auto *id : {"shortcut:GLOBAL_VIDEO_INDEXING:G", "shortcut:GLOBAL_VIDEO_INDEXING:S"}) {
        EXPECT_EQ(rowOf(plan, id).disposition, Disposition::Retired) << id;
        EXPECT_NE(rowOf(plan, id).reason.find("retired"), std::string::npos);
    }
    const Profile profile = applyPlan(plan, proposedRows(plan), linuxHost());
    EXPECT_FALSE(profile.values.contains("toolbar.actions"));
    EXPECT_FALSE(profile.values.contains("toolbar.alignment"));
    EXPECT_FALSE(profile.values.contains(std::string(kHotkeysSetting)));
}

// ---- Hotkeys.txt / AudioHotkeys.txt

TEST(SettingsImport, ShortHotkeyFileImportsItsValidBindings)
{
    // C04-short-file (approved 2026-09-27): four records, legacy loaded its
    // defaults over them (Hotkeys.cpp:309-315); the importer reads the valid
    // ones and keeps the others visibly unresolved.
    const Plan plan = buildPlan({hotkeys("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\nGRID_DUPLICATE_LINES S=Ctrl-D\r\n"
                                         "NOT_AN_ACTION G=Ctrl-K\r\nGLOBAL_UNDO G=\r\n")},
                                linuxHost(), readBy(ReadBy::Windows));
    const PlanRow &file = rowOf(plan, "file:Config/Hotkeys.txt");
    EXPECT_EQ(file.disposition, Disposition::Read);
    EXPECT_NE(file.reason.find("C04-short-file"), std::string::npos);
    const PlanRow &save = rowOf(plan, "shortcut:GLOBAL_SAVE_SUBS:G");
    EXPECT_EQ(save.disposition, Disposition::Change);
    EXPECT_EQ(save.current, SettingValue(std::string("Ctrl-S")));
    EXPECT_TRUE(save.proposedImport);
    EXPECT_EQ(rowOf(plan, "shortcut:GRID_DUPLICATE_LINES:S").disposition, Disposition::Unchanged);
    const PlanRow &unknown = rowOf(plan, "record:Config/Hotkeys.txt:4");
    EXPECT_EQ(unknown.disposition, Disposition::Unresolved);
    EXPECT_EQ(unknown.raw, "NOT_AN_ACTION G=Ctrl-K");
    // A blank record is no proof of unbinding.
    const PlanRow &blank = rowOf(plan, "record:Config/Hotkeys.txt:5");
    EXPECT_EQ(blank.disposition, Disposition::Unresolved);
    EXPECT_NE(blank.reason.find("no proof of unbinding"), std::string::npos);
    // The defaults the short file leaves out are offered to unbind, not
    // proposed: legacy added them.
    const PlanRow &undo = rowOf(plan, "shortcut:GLOBAL_UNDO:G");
    EXPECT_EQ(undo.disposition, Disposition::Missing);
    EXPECT_TRUE(undo.selectable);
    EXPECT_FALSE(undo.proposedImport);
    EXPECT_NE(undo.reason.find("legacy added its default Ctrl-Z"), std::string::npos);

    const Profile profile = applyPlan(plan, proposedRows(plan), linuxHost());
    HotkeyMap map;
    readHotkeyLines(map, std::get<std::vector<std::string>>(profile.values.at(std::string(kHotkeysSetting))));
    EXPECT_EQ(map.at(HotkeyId{hotkeyIdOf("GLOBAL_SAVE_SUBS"), GlobalHotkey}).accel, "Ctrl-Alt-S");
    EXPECT_EQ(map.at(HotkeyId{hotkeyIdOf("GLOBAL_UNDO"), GlobalHotkey}).accel, "Ctrl-Z");
    EXPECT_FALSE(profile.values.contains(std::string(kAudioHotkeysSetting)));
}

TEST(SettingsImport, EmptyAndMalformedHotkeyFiles)
{
    // Only the header: read, nothing in it.
    const Plan empty = buildPlan({hotkeys("")}, linuxHost(), readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(empty, "file:Config/Hotkeys.txt").disposition, Disposition::Read);
    EXPECT_TRUE(proposedRows(empty).empty());

    // No "[" header: LoadHkeys found it outdated and replaced it with its
    // defaults (Hotkeys.cpp:250-277); every record stays unresolved.
    const Plan noHeader = buildPlan({hotkeys("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n", "")}, linuxHost(),
                                    readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(noHeader, "file:Config/Hotkeys.txt").disposition, Disposition::Unresolved);
    EXPECT_EQ(rowOf(noHeader, "record:Config/Hotkeys.txt:1").disposition, Disposition::Unresolved);
    EXPECT_TRUE(proposedRows(noHeader).empty());

    // The semantic-version header the running legacy build writes
    // ("[HikariSub v0.0.1-rc.1]"): LoadHkeys reads the build number five
    // characters past the first '.', finds none (wxAtoi 0, not past 487) and
    // replaces the file with its defaults. Kept: unresolved (proposed
    // departure in the O3 report).
    const HotkeyFile own = readHotkeyFile("[HikariSub v0.0.1-rc.1]\nGLOBAL_SAVE_SUBS G=Ctrl-Alt-S\n");
    EXPECT_EQ(own.version, 0);
    EXPECT_FALSE(own.accepted);
    const Plan semver = buildPlan({hotkeys("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n", kHeader)}, linuxHost(),
                                  readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(semver, "record:Config/Hotkeys.txt:2").disposition, Disposition::Unresolved);

    // Mixed: a record without a window letter, an invalid key, a number.
    const Plan mixed = buildPlan({hotkeys("GLOBAL_SAVE_SUBS X=Ctrl-Alt-S\r\nGLOBAL_UNDO G=Ctrl-Bogus\r\n"
                                          "5000 S=Ctrl-Alt-W\r\n")},
                                 linuxHost(), readBy(ReadBy::Windows));
    EXPECT_NE(rowOf(mixed, "record:Config/Hotkeys.txt:2").reason.find("no window letter"), std::string::npos);
    const PlanRow &invalid = rowOf(mixed, "shortcut:GLOBAL_UNDO:G");
    EXPECT_EQ(invalid.disposition, Disposition::Unresolved);
    EXPECT_NE(invalid.reason.find("\"Bogus\""), std::string::npos);
    EXPECT_EQ(rowOf(mixed, "shortcut:GLOBAL_SAVE_SUBS:S").value, SettingValue(std::string("Ctrl-Alt-W")));
}

TEST(SettingsImport, OldHotkeyNamesAreConvertedAndAbsentTargetsStayUnresolved)
{
    // version < 1141: ConvertHotkeys' renames, N -> S, W -> V
    // (ConfigConverter.cpp:579-608).
    const Plan plan = buildPlan({hotkeys("PlayPause W=Space\r\nPlus5SecondG G=Ctrl-Alt-Right\r\n",
                                         "[Kainote v0.8.0.1100]\r\n")},
                                linuxHost(), readBy(ReadBy::Windows));
    const PlanRow &play = rowOf(plan, "shortcut:VIDEO_PLAY_PAUSE:V");
    EXPECT_NE(play.reason.find("renamed from PlayPause"), std::string::npos);
    EXPECT_EQ(play.disposition, Disposition::Unchanged); // Space is its default
    // Plus5SecondG -> GLOBAL_5_SECONDS_FORWARD, which is no action: no
    // guessed Video substitute (settings-migration-map.md).
    const PlanRow &absent = rowOf(plan, "record:Config/Hotkeys.txt:3");
    EXPECT_EQ(absent.disposition, Disposition::Unresolved);
    EXPECT_NE(absent.reason.find("GLOBAL_5_SECONDS_FORWARD"), std::string::npos);
}

TEST(SettingsImport, LaterBindingsSupersedeAndConflictsAreShown)
{
    // GLOBAL_SAVE_SUBS twice: the last is in effect. GLOBAL_OPEN_SUBS takes
    // Ctrl-S, which the default Save holds: legacy gave it Ctrl-S, so the
    // full file's missing Save default is proposed for unbinding.
    std::string body = "GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\nGLOBAL_SAVE_SUBS G=Ctrl-Alt-Q\r\nGLOBAL_UNDO G=Ctrl-S\r\n";
    const Plan plan = buildPlan({hotkeys(body)}, linuxHost(), readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(plan, "record:Config/Hotkeys.txt:2").disposition, Disposition::Superseded);
    EXPECT_EQ(rowOf(plan, "shortcut:GLOBAL_SAVE_SUBS:G").value, SettingValue(std::string("Ctrl-Alt-Q")));
    const PlanRow &undo = rowOf(plan, "shortcut:GLOBAL_UNDO:G");
    EXPECT_EQ(undo.disposition, Disposition::Change);
    EXPECT_EQ(undo.reason.find("the default binding of"), std::string::npos); // Save's own row moves it

    // Against a user's binding of the same keys: shown and not proposed.
    Destination users = linuxHost();
    HotkeyMap mine;
    loadDefaultHotkeys(mine, false);
    mine[HotkeyId{hotkeyIdOf("GLOBAL_HISTORY"), GlobalHotkey}].accel = "Ctrl-Alt-H";
    users.values[std::string(kHotkeysSetting)] = hotkeyLines(mine, false);
    const Plan conflict = buildPlan({hotkeys("GLOBAL_REDO G=Ctrl-Alt-H\r\n")}, users,
                                    readBy(ReadBy::Windows));
    const PlanRow &redo = rowOf(conflict, "shortcut:GLOBAL_REDO:G");
    EXPECT_FALSE(redo.proposedImport);
    EXPECT_NE(redo.reason.find("your binding of GLOBAL_HISTORY has these keys"), std::string::npos);
}

TEST(SettingsImport, MacroBindingsUseTheFileNameAndOrdinalAlias)
{
    // S44-macro-alias: "Script <file>-<k>" (Hotkeys.cpp:281-289), the
    // scripts not run; a problem the loaded scripts have stays visible.
    Destination d = linuxHost();
    d.macroProblem = [](std::string_view alias) {
        return alias == "dup.lua:1" ? std::string("two scripts are named dup.lua") : std::string();
    };
    const Plan plan = buildPlan({hotkeys("Script tools.lua-2=Ctrl-Shift-A\r\nScript dup.lua-1=Ctrl-Shift-B\r\n"
                                         "Script nameless=Ctrl-Shift-C\r\nScript blank.lua-1=\r\n")},
                                d, readBy(ReadBy::Windows));
    const PlanRow &tools = rowOf(plan, "macro:Script tools.lua-2");
    EXPECT_EQ(tools.kind, RowKind::Macro);
    EXPECT_EQ(tools.destination, "tools.lua:2");
    EXPECT_EQ(tools.value, SettingValue(std::string("Ctrl+Shift+A")));
    EXPECT_TRUE(tools.proposedImport);
    EXPECT_NE(rowOf(plan, "macro:Script dup.lua-1").reason.find("two scripts are named dup.lua"), std::string::npos);
    EXPECT_EQ(rowOf(plan, "record:Config/Hotkeys.txt:4").disposition, Disposition::Unresolved);
    EXPECT_EQ(rowOf(plan, "record:Config/Hotkeys.txt:5").disposition, Disposition::Unresolved);

    const Profile profile = applyPlan(plan, {"macro:Script tools.lua-2"}, d);
    EXPECT_EQ(profile.values.at(std::string(kAutomationHotkeysSetting)),
              SettingValue(std::vector<std::string>{"Script tools.lua-2\tCtrl+Shift+A\t\t"}));

    // A macro bound here already keeps its keys unless chosen.
    Destination bound = destinationOf(profile, d);
    const Plan again = buildPlan({hotkeys("Script tools.lua-2=Ctrl-Shift-Z\r\n")}, bound, readBy(ReadBy::Windows));
    EXPECT_FALSE(rowOf(again, "macro:Script tools.lua-2").proposedImport);
}

// ---- Paths

TEST(SettingsImport, ForeignAndMissingPathsAreReportedNotRepaired)
{
    EXPECT_TRUE(isForeignPath("D:\\media\\a.ass", false));
    EXPECT_TRUE(isForeignPath("\\\\server\\share", false));
    EXPECT_FALSE(isForeignPath("/home/u/a.ass", false));
    EXPECT_TRUE(isForeignPath("/home/u/a.ass", true));
    EXPECT_FALSE(isForeignPath("D:\\media\\a.ass", true));

    Destination d = linuxHost();
    d.pathExists = [](std::string_view p) { return p != "/gone/b.ass"; };
    const std::string body = padding() + "SUBS_RECENT_FILES={\n\tD:\\media\\a.ass\n\t/gone/b.ass\n\t/here/c.ass\n}\r\n"
                                         "EXTERNAL_FONTS_DIRECTORY=D:\\fonts\r\n";
    const Plan plan = buildPlan({config(body)}, d, readBy(ReadBy::Windows));
    const PlanRow &recent = rowOf(plan, "setting:recent.subtitles");
    EXPECT_EQ(recent.disposition, Disposition::Change);
    EXPECT_EQ(recent.unresolvedPaths, (std::vector<std::string>{"D:\\media\\a.ass", "/gone/b.ass"}));
    // The list keeps every entry as it was, in order.
    EXPECT_EQ(recent.value,
              SettingValue(std::vector<std::string>{"D:\\media\\a.ass", "/gone/b.ass", "/here/c.ass"}));
    const auto fonts = rowsKeyed(plan, "EXTERNAL_FONTS_DIRECTORY");
    ASSERT_EQ(fonts.size(), 1u);
    EXPECT_EQ(fonts.front()->disposition, Disposition::Unresolved);
    EXPECT_FALSE(fonts.front()->selectable);
    EXPECT_EQ(fonts.front()->unresolvedPaths, (std::vector<std::string>{"D:\\fonts"}));

    // A second import of the same files neither duplicates the recents nor
    // repairs the missing one.
    const Profile first = applyPlan(plan, proposedRows(plan), d);
    const Plan second = buildPlan({config(body)}, destinationOf(first, d), readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(second, "setting:recent.subtitles").disposition, Disposition::Unchanged);
    EXPECT_EQ(applyPlan(second, proposedRows(second), destinationOf(first, d)), first);
}

// ---- Keeping the destination, repeat import and later edits (S44-import)

TEST(SettingsImport, CurrentChoicesAreKeptUnlessChosen)
{
    Destination d = linuxHost();
    d.values["grid.font"] = std::string("Mine");
    const Plan plan = buildPlan({config(padding() + "GRID_FONT=Arial\r\nGRID_FONT_SIZE=12\r\n")}, d,
                                readBy(ReadBy::Windows));
    const PlanRow &font = rowOf(plan, "setting:grid.font");
    EXPECT_EQ(font.disposition, Disposition::Change);
    EXPECT_EQ(font.current, SettingValue(std::string("Mine")));
    EXPECT_FALSE(font.proposedImport);
    EXPECT_TRUE(rowOf(plan, "setting:grid.fontSize").proposedImport); // over the default
    // Chosen anyway, it is imported.
    auto chosen = proposedRows(plan);
    chosen.insert("setting:grid.font");
    EXPECT_EQ(applyPlan(plan, chosen, d).values.at("grid.font"), SettingValue(std::string("Arial")));
    EXPECT_EQ(applyPlan(plan, proposedRows(plan), d).values.at("grid.font"), SettingValue(std::string("Mine")));
}

TEST(SettingsImport, RepeatedImportIsANoOpAndLaterEditsAreKept)
{
    const std::vector<SourceFile> sources = {
        config(padding() + "GRID_FONT=Arial\r\nGRID_FONT_SIZE=12\r\n"),
        hotkeys("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n"),
        source(SourceKind::Rules, "Config/Rules.txt", "1\ta\tb\n"),
        source(SourceKind::UserDictionary, "Dictionary/UserDic.udic", "słowo\n"),
    };
    const Destination start = linuxHost();
    const Plan plan = buildPlan(sources, start, readBy(ReadBy::Windows));
    auto chosen = proposedRows(plan);
    chosen.erase("setting:grid.fontSize"); // the user keeps this one
    const Profile imported = applyPlan(plan, chosen, start);
    EXPECT_EQ(imported.files.at("Rules.txt"), "1\ta\tb\n");
    EXPECT_EQ(imported.files.at("Dictionary/UserDic.udic"), "słowo\n");
    const Receipt receipt = receiptOf(plan, chosen, "default");
    EXPECT_TRUE(receipt.kept.contains("setting:grid.fontSize"));

    // The same sources again: the same choices, nothing new.
    PlanOptions o = readBy(ReadBy::Windows);
    o.previous = &receipt;
    const Destination after = destinationOf(imported);
    const Plan again = buildPlan(sources, after, o);
    EXPECT_FALSE(rowOf(again, "setting:grid.fontSize").proposedImport);
    EXPECT_EQ(rowOf(again, "setting:grid.font").disposition, Disposition::Unchanged);
    EXPECT_EQ(rowOf(again, "collection:Rules.txt").disposition, Disposition::Unchanged);
    EXPECT_EQ(applyPlan(again, proposedRows(again), after), imported);
    EXPECT_TRUE(receiptOf(again, proposedRows(again), "default").sameIdentity(receipt));

    // A later edit is not overwritten by importing again.
    Destination edited = after;
    edited.values["grid.font"] = std::string("Edited");
    const Plan third = buildPlan(sources, edited, o);
    const PlanRow &font = rowOf(third, "setting:grid.font");
    EXPECT_EQ(font.disposition, Disposition::Change);
    EXPECT_FALSE(font.proposedImport);
    EXPECT_NE(font.reason.find("changed since the last import"), std::string::npos);

    // Changed sources: a fresh diff whose identity differs.
    std::vector<SourceFile> changed = sources;
    changed[0] = config(padding() + "GRID_FONT=Verdana\r\nGRID_FONT_SIZE=12\r\n");
    const Plan fresh = buildPlan(changed, after, o);
    const PlanRow &verdana = rowOf(fresh, "setting:grid.font");
    EXPECT_EQ(verdana.disposition, Disposition::Change);
    EXPECT_TRUE(verdana.proposedImport); // not edited since the import
    EXPECT_FALSE(receiptOf(fresh, proposedRows(fresh), "default").sameIdentity(receipt));
}

// ---- Authored collections and encodings

TEST(SettingsImport, CollectionsAreCarriedOverByteForByte)
{
    Destination d = linuxHost();
    d.files["Rules.txt"] = DestinationFile{"mine", fakeHash("mine")};
    d.bundledDictionaries = {"en_US"};
    const Plan plan = buildPlan({source(SourceKind::Rules, "Config/Rules.txt", "\xEF\xBB\xBFrules\r\n"),
                                 source(SourceKind::Dictionary, "Dictionary/en_US.dic", "dic"),
                                 source(SourceKind::Dictionary, "Dictionary/en_US.aff", "aff"),
                                 source(SourceKind::Dictionary, "Dictionary/pl_PL.dic", "dic")},
                                d, readBy(ReadBy::Windows));
    const PlanRow &rules = rowOf(plan, "collection:Rules.txt");
    EXPECT_EQ(rules.disposition, Disposition::Change);
    EXPECT_FALSE(rules.proposedImport); // the user's Rules.txt is kept
    const PlanRow &en = rowOf(plan, "collection:Dictionary/en_US");
    EXPECT_EQ(en.files, (std::vector<std::string>{"Dictionary/en_US.dic", "Dictionary/en_US.aff"}));
    EXPECT_NE(en.reason.find("would shadow"), std::string::npos);
    EXPECT_FALSE(en.proposedImport);
    EXPECT_EQ(rowOf(plan, "collection:Dictionary/pl_PL").disposition, Disposition::Unresolved);
    const Profile p = applyPlan(plan, {"collection:Rules.txt", "collection:Dictionary/en_US"}, d);
    EXPECT_EQ(p.files.at("Rules.txt"), "\xEF\xBB\xBFrules\r\n");
    EXPECT_EQ(p.files.at("Dictionary/en_US.aff"), "aff");
    EXPECT_FALSE(p.files.contains("Dictionary/pl_PL.dic"));
}

TEST(SettingsImport, AmbiguousEncodingNeedsAnExplicitInterpretation)
{
    // Not UTF-8 and no BOM: nothing is read until an interpretation is chosen.
    const SourceFile latin = config(padding() + "GRID_FONT=Caf\xE9\r\n");
    const Plan plan = buildPlan({latin}, linuxHost(), readBy(ReadBy::Windows));
    EXPECT_TRUE(plan.ambiguousEncoding);
    EXPECT_EQ(rowOf(plan, "file:Config/Config.txt").disposition, Disposition::Unresolved);
    EXPECT_EQ(plan.row("setting:grid.font"), nullptr);
    PlanOptions o = readBy(ReadBy::Windows);
    o.interpretation = Interpretation::Windows1252;
    const Plan chosen = buildPlan({latin}, linuxHost(), o);
    EXPECT_FALSE(chosen.ambiguousEncoding);
    EXPECT_EQ(rowOf(chosen, "setting:grid.font").value, SettingValue(std::string("Café")));
    EXPECT_NE(rowOf(chosen, "file:Config/Config.txt").reason.find("Windows-1252"), std::string::npos);

    // A UTF-16LE BOM is read as legacy's wxConvAuto read it.
    std::string utf16 = "\xFF\xFE";
    for (const char c : std::string(kHeader) + padding() + "GRID_FONT=Arial\r\n") {
        utf16 += c;
        utf16 += '\0';
    }
    const Plan wide = buildPlan({source(SourceKind::Config, "Config/Config.txt", utf16)}, linuxHost(),
                                readBy(ReadBy::Windows));
    EXPECT_EQ(rowOf(wide, "setting:grid.font").value, SettingValue(std::string("Arial")));
}

TEST(SettingsImport, PlanSourcesAreTheSnapshotsHashes)
{
    const std::vector<SourceFile> sources = {hotkeys("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n"),
                                             config(padding() + "GRID_FONT=Arial\r\n")};
    const Plan plan = buildPlan(sources, linuxHost(), readBy(ReadBy::Windows));
    EXPECT_EQ(plan.sources, (std::vector<std::pair<std::string, std::string>>{
                                {"Config/Config.txt", sources[1].sha256}, {"Config/Hotkeys.txt", sources[0].sha256}}));
    EXPECT_EQ(plan.mappingVersion, kMappingVersion);
}
