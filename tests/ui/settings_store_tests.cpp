// O1: the settings registry over its files. CF1 (#488): every setting
// round-trips through settings.toml, hotkeys.toml or state.json, which hold
// only values that differ from the default; hikari.ini converts into them
// once (the interim keys the rewrite used before the registry moved onto
// their settings on the way) and becomes hikari.ini.old; a file that does
// not read is kept as <name>.broken and its settings take their defaults.

#include "colour_picker_controller.h"
#include "grid_filter_controller.h"
#include "settings_files.h"
#include "settings_store.h"
#include "shift_times_controller.h"
#include "tag_buttons_controller.h"

#include "hikari/application/hotkeys.h"
#include "toml_file.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using hikari::ui::SettingsFiles;
using hikari::ui::SettingsStore;

namespace {

application::SettingValue sampleFor(const application::SettingDefinition &setting)
{
    using application::SettingType;
    switch (setting.type) {
    case SettingType::Bool:
        return !std::get<bool>(setting.defaultValue);
    case SettingType::Int:
        return std::get<std::int64_t>(setting.defaultValue) + 1234567890123;
    case SettingType::String:
        return std::get<std::string>(setting.defaultValue) + reinterpret_cast<const char *>(u8"ż, \"x\"\n\t{y}");
    case SettingType::StringList:
        return std::vector<std::string>{"a, b", "", reinterpret_cast<const char *>(u8"ąć"), "x\ty"};
    }
    return false;
}

// The shortcuts are stored as changes from the defaults, so only real
// SaveHkeys lines and macro rows come back as they were.
bool shortcutSetting(std::string_view id)
{
    return id == application::kHotkeysSetting || id == application::kAudioHotkeysSetting ||
           id == application::kAutomationHotkeysSetting;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

toml::table tomlOf(const QString &path)
{
    auto parsed = ui::parseTomlFile(readAll(path).toStdString(), 1);
    if (auto *table = std::get_if<toml::table>(&parsed))
        return *table;
    return {};
}

QJsonObject jsonOf(const QString &path)
{
    return QJsonDocument::fromJson(readAll(path)).object();
}

QStringList qLines(const std::vector<std::string> &lines)
{
    QStringList out;
    for (const auto &l : lines)
        out << QString::fromStdString(l);
    return out;
}

// The INI store's writing of a value (SettingsStore::toVariant).
QVariant iniVariant(const application::SettingValue &value)
{
    return SettingsStore::toVariant(value);
}

QString iniKey(const application::SettingDefinition &s)
{
    return QString::fromUtf8(application::scopeName(s.scope).data()) + QLatin1Char('/') +
           QString::fromUtf8(s.id.data(), qsizetype(s.id.size()));
}

// Main bindings with one changed, one copied to another window and one taken
// away; audio bindings with one changed.
application::HotkeyMap changedHotkeys()
{
    using namespace application;
    HotkeyMap map = defaultHotkeys();
    map[HotkeyId{hotkeyIdOf("GLOBAL_SAVE_SUBS"), GlobalHotkey}].accel = "Ctrl-Shift-W";
    map[HotkeyId{hotkeyIdOf("GLOBAL_SAVE_SUBS"), GridHotkey}] = Hotkey{"", "F7"};
    map[HotkeyId{hotkeyIdOf("GLOBAL_UNDO"), GlobalHotkey}].accel.clear();
    map[HotkeyId{hotkeyIdOf("AUDIO_COMMIT"), AudioHotkey}].accel = "Num Enter";
    return map;
}

} // namespace

class SettingsStoreTests : public QObject {
    Q_OBJECT

private slots:
    void everySettingRoundTripsThroughTheFiles()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            SettingsStore store(file);
            for (const auto &s : application::settingDefinitions())
                if (s.disposition != application::SettingDisposition::Excluded && !shortcutSetting(s.id))
                    QVERIFY2(store.settings().set(s.id, sampleFor(s)), s.id.data());
        }
        SettingsStore again(file);
        for (const auto &s : application::settingDefinitions()) {
            if (s.disposition == application::SettingDisposition::Excluded || shortcutSetting(s.id)) {
                QVERIFY(!again.settings().isSet(s.id));
                continue;
            }
            QVERIFY2(again.settings().isSet(s.id), s.id.data());
            QVERIFY2(again.settings().value(s.id) == sampleFor(s), s.id.data());
        }
        // A dotted id is tables and a key; the [workspace] keys and what the
        // app remembers are in state.json.
        const toml::table settings = tomlOf(file);
        QCOMPARE(settings["grid"]["hideColumns"].value_or(std::int64_t{0}),
                 std::get<std::int64_t>(sampleFor(*application::findSetting("grid.hideColumns"))));
        QVERIFY(settings["appearance"]["highContrastBlack"]["accent"].is_string());
        QVERIFY(settings["toolbar"]["actions"].is_array());
        QVERIFY(settings["editor"]["tagButton"].is_array());
        QVERIFY(!settings["program"]["theme"]);
        QVERIFY(!settings["workspace"]["windowSize"]);
        QVERIFY(settings["workspace"]["shiftTimesOn"].is_boolean()); // a profile setting
        QVERIFY(!settings["recent"]);
        QVERIFY(readAll(file).startsWith("version = 1\n"));
        const QJsonObject state = jsonOf(dir.filePath(QStringLiteral("state.json")));
        QCOMPARE(state.value(QLatin1String("version")).toInt(), 1);
        QVERIFY(state.value(QLatin1String("workspace")).toObject().contains(QLatin1String("windowSize")));
        QVERIFY(state.value(QLatin1String("recent")).toObject().value(QLatin1String("subtitles")).isArray());
        QVERIFY(state.value(QLatin1String("privacy")).toObject().contains(QLatin1String("usageDataId")));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("hikari.ini"))));
    }

    void onlyValuesThatDifferFromTheDefaultAreWritten()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            SettingsStore store(file);
            store.set("audio.cacheFilesLimit", 20);
            store.set("audio.volume", 50); // its default
            store.set("recent.subtitles", QStringList{QStringLiteral("/a.ass")});
            store.set("workspace.settingsPage", QStringLiteral("general")); // its default
        }
        QCOMPARE(readAll(file), QByteArray("version = 1\n\n[audio]\ncacheFilesLimit = 20\n"));
        QCOMPARE(jsonOf(dir.filePath(QStringLiteral("state.json"))).keys(),
                 (QStringList{QStringLiteral("recent"), QStringLiteral("version")}));
        {
            SettingsStore store(file);
            QVERIFY(!store.contains("audio.volume"));
            QCOMPARE(store.integer("audio.cacheFilesLimit"), 20);
            store.set("audio.cacheFilesLimit", 10); // back to the default: the key and its table go
            store.reset("recent.subtitles");
        }
        QCOMPARE(readAll(file), QByteArray("version = 1\n"));
        QCOMPARE(readAll(dir.filePath(QStringLiteral("state.json"))), QByteArray("{\n    \"version\": 1\n}\n"));
    }

    void withoutAFileValuesStayInMemory()
    {
        SettingsStore store;
        QSignalSpy changed(&store, &SettingsStore::changed);
        QVERIFY(!store.boolean("subtitles.saveWithVideoName"));
        QVERIFY(store.setValue(QStringLiteral("subtitles.saveWithVideoName"), true));
        QVERIFY(store.boolean("subtitles.saveWithVideoName"));
        store.set("subtitles.saveWithVideoName", true); // no change, no signal
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toString(), QStringLiteral("subtitles.saveWithVideoName"));
        store.resetAll();
        QVERIFY(!store.boolean("subtitles.saveWithVideoName"));
        QCOMPARE(changed.count(), 2);
        QVERIFY(!store.setValue(QStringLiteral("program.theme"), QStringLiteral("LightSentro")));
    }

    // Stores on one folder share its files, as they shared QSettings' cache.
    void storesOnOneFolderShareTheFiles()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        SettingsStore a(file);
        SettingsStore b(file);
        a.set("audio.volume", 70);
        QCOMPARE(b.integer("audio.volume"), 70);
        QVERIFY(!QFileInfo::exists(file)); // kept until the next event loop pass
        QCoreApplication::processEvents();
        QVERIFY(QFileInfo::exists(file));
    }

    // hotkeys.toml: the changes from the defaults per window, Qt's key text,
    // "" for a default taken away, each with its command's name; the
    // controller still sees SaveHkeys lines.
    void hotkeysStoreOnlyTheirChanges()
    {
        using namespace application;
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        const HotkeyMap map = changedHotkeys();
        const auto main = hotkeyLines(map, false);
        const auto audio = hotkeyLines(map, true);
        const QStringList macros{QStringLiteral("Script Karaoke Templater-Apply\tCtrl+Alt+K\tApply\tabc123"),
                                 QStringLiteral("Script b.lua-0\tF9\tB macro\t")};
        {
            SettingsStore store(file);
            store.set(kHotkeysSetting.data(), qLines(main));
            store.set(kAudioHotkeysSetting.data(), qLines(audio));
            store.set(kAutomationHotkeysSetting.data(), macros);
        }
        const QByteArray text = readAll(dir.filePath(QStringLiteral("hotkeys.toml")));
        QCOMPARE(text, QByteArray("version = 1\n"
                                  "\n[global]\n"
                                  "GLOBAL_SAVE_SUBS = \"Ctrl+Shift+W\"  # Save\n"
                                  "GLOBAL_UNDO = \"\"  # Undo\n"
                                  "\n[grid]\n"
                                  "GLOBAL_SAVE_SUBS = \"F7\"  # Save\n"
                                  "\n[audio]\n"
                                  "AUDIO_COMMIT = \"Num+Enter\"  # Commit\n"
                                  "\n[automation]\n"
                                  "\"Script Karaoke Templater-Apply\" = { keys = \"Ctrl+Alt+K\", macro = \"Apply\", "
                                  "sha256 = \"abc123\" }\n"
                                  "\"Script b.lua-0\" = { keys = \"F9\", macro = \"B macro\", sha256 = \"\" }\n"));
        SettingsStore again(file);
        HotkeyMap read;
        readHotkeyLines(read, again.settings().list(kHotkeysSetting));
        readHotkeyLines(read, again.settings().list(kAudioHotkeysSetting));
        HotkeyMap expected;
        readHotkeyLines(expected, main);
        readHotkeyLines(expected, audio);
        QVERIFY(read == expected);
        QCOMPARE(again.list(kAutomationHotkeysSetting.data()), macros);
        // The defaults again: nothing is stored.
        again.set(kHotkeysSetting.data(), qLines(hotkeyLines(defaultHotkeys(), false)));
        QVERIFY(!again.contains(kHotkeysSetting.data()));
        QVERIFY(again.contains(kAudioHotkeysSetting.data()));
    }

    // Every default binding's key comes back the same through Qt's text.
    void defaultHotkeysRoundTripThroughTheirText()
    {
        using namespace application;
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        // Each default moved to the Video window, so each is written out.
        HotkeyMap moved;
        for (const auto &[at, hotkey] : defaultHotkeys())
            if (at.type != AudioHotkey)
                moved[HotkeyId{at.id, at.type == VideoHotkey ? GlobalHotkey : VideoHotkey}] = hotkey;
        {
            SettingsStore store(file);
            store.set(kHotkeysSetting.data(), qLines(hotkeyLines(moved, false)));
        }
        SettingsStore again(file);
        HotkeyMap read;
        readHotkeyLines(read, again.settings().list(kHotkeysSetting));
        HotkeyMap expected;
        readHotkeyLines(expected, hotkeyLines(moved, false));
        QVERIFY(read == expected);
    }

    // The model has one shortcut per command and window: an array's first is
    // used and the rest are named as ignored.
    void aHotkeyArrayGivesItsFirstShortcut()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        writeAll(dir.filePath(QStringLiteral("hotkeys.toml")),
                 "version = 1\n[global]\nGLOBAL_SAVE_SUBS = [\"Ctrl+Shift+W\", \"F2\"]\nGLOBAL_UNDO = [\"F3\"]\n"
                 "LATER_COMMAND = \"F4\"  # a later version's\n");
        SettingsStore store(file);
        application::HotkeyMap read;
        application::readHotkeyLines(read, store.settings().list(application::kHotkeysSetting));
        QCOMPARE(read.at({application::hotkeyIdOf("GLOBAL_SAVE_SUBS"), application::GlobalHotkey}).accel,
                 std::string("Ctrl-Shift-W"));
        QCOMPARE(read.at({application::hotkeyIdOf("GLOBAL_UNDO"), application::GlobalHotkey}).accel, std::string("F3"));
        QVERIFY(store.ignored() == std::vector<std::string>{"global.GLOBAL_SAVE_SUBS: only the first shortcut is used"});
        // Written again (with one more change): one key each, the later
        // version's entry kept.
        read[{application::hotkeyIdOf("GLOBAL_REDO"), application::GlobalHotkey}].accel = "F6";
        store.set(application::kHotkeysSetting.data(), qLines(application::hotkeyLines(read, false)));
        store.sync();
        const toml::table hotkeys = tomlOf(dir.filePath(QStringLiteral("hotkeys.toml")));
        QCOMPARE(hotkeys["global"]["GLOBAL_SAVE_SUBS"].value_or(std::string()), std::string("Ctrl+Shift+W"));
        QCOMPARE(hotkeys["global"]["LATER_COMMAND"].value_or(std::string()), std::string("F4"));
    }

    // Collections and rows as TOML and JSON structures, not tab-joined text.
    void listsAreArraysAndTables()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        const QString odd = QStringLiteral("{\n\t\\b1\n\tx\n\tBold\n}"); // a type that is no number
        const QStringList answers{QStringLiteral("k2\t{\"answered\":{\"blur\":2},\"button\":1,\"sent\":{}}"),
                                  QStringLiteral("k1\t{\"answered\":{},\"button\":0,\"sent\":{\"a\":\"b\"}}")};
        const QStringList presets{
            QStringLiteral("k1\t[{\"name\":\"Soft\",\"values\":{\"blur\":2.5,\"mode\":\"fade\",\"on\":true}}]")};
        {
            SettingsStore store(file);
            store.set("editor.tagButtons", 3);
            store.set("editor.tagButton1", QStringLiteral("{\n\t\\b1\n\t0\n\tBold\n\ttag-bold\n}"));
            store.set("editor.tagButton3", odd);
            store.set("editor.tagButton5", QStringLiteral("{\n\t\\i1\n\t1\n\tItalic\n}")); // past the count: kept
            store.set("grid.filterStyles", QStringList{QStringLiteral("Sign"), QStringLiteral("Default")});
            store.set("workspace.dialogGeometry",
                      QStringList{QStringLiteral("settings\t120,80,900,640"), QStringLiteral("find\t-5,7,400,300")});
            store.set("workspace.pickerFolders", QStringList{QStringLiteral("openSubtitles\tfile:///home/me/subs")});
            store.set("automation.dialogAnswers", answers);
            store.set("automation.dialogPresets", presets);
        }
        const toml::table settings = tomlOf(file);
        const auto *slotList = settings["editor"]["tagButton"].as_array();
        QVERIFY(slotList);
        QCOMPARE(int(slotList->size()), 5);
        QCOMPARE((*slotList)[0].as_table()->at("tag").value_or(std::string()), std::string("\\b1"));
        QCOMPARE((*slotList)[0].as_table()->at("icon").value_or(std::string()), std::string("tag-bold"));
        QVERIFY((*slotList)[1].as_table()->empty());
        QCOMPARE((*slotList)[2].as_table()->at("text").value_or(std::string()), odd.toStdString());
        QCOMPARE((*slotList)[4].as_table()->at("name").value_or(std::string()), std::string("Italic"));
        QCOMPARE(int(settings["grid"]["filterStyles"].as_array()->size()), 2);
        QCOMPARE(settings["automation"]["dialogPresets"]["k1"][0]["values"]["blur"].value_or(0.0), 2.5);
        const QJsonObject state = jsonOf(dir.filePath(QStringLiteral("state.json")));
        const QJsonObject workspace = state.value(QLatin1String("workspace")).toObject();
        QCOMPARE(workspace.value(QLatin1String("dialogGeometry")).toObject().value(QLatin1String("find")).toArray(),
                 (QJsonArray{-5, 7, 400, 300}));
        QCOMPARE(workspace.value(QLatin1String("pickerFolders")).toObject().value(QLatin1String("openSubtitles")),
                 QJsonValue(QStringLiteral("file:///home/me/subs")));
        const QJsonArray answerList =
            state.value(QLatin1String("automation")).toObject().value(QLatin1String("dialogAnswers")).toArray();
        QCOMPARE(answerList.at(0).toObject().value(QLatin1String("key")).toString(), QStringLiteral("k2"));
        QCOMPARE(answerList.at(0).toObject().value(QLatin1String("button")).toInt(), 1);

        SettingsStore again(file);
        QCOMPARE(again.text("editor.tagButton1"), QStringLiteral("{\n\t\\b1\n\t0\n\tBold\n\ttag-bold\n}"));
        QVERIFY(!again.contains("editor.tagButton2"));
        QCOMPARE(again.text("editor.tagButton3"), odd);
        QCOMPARE(again.text("editor.tagButton5"), QStringLiteral("{\n\t\\i1\n\t1\n\tItalic\n}"));
        QCOMPARE(again.list("workspace.dialogGeometry"),
                 (QStringList{QStringLiteral("find\t-5,7,400,300"), QStringLiteral("settings\t120,80,900,640")}));
        QCOMPARE(again.list("automation.dialogAnswers"), answers); // newest first, as written
        QCOMPARE(again.list("automation.dialogPresets"), presets);
        ui::TagButtonsController buttons(again);
        QCOMPARE(buttons.count(), 3);
        QCOMPARE(buttons.button(0).icon, QStringLiteral("tag-bold"));
        // Taking the last slots away leaves the empty ones out.
        again.reset("editor.tagButton5");
        again.reset("editor.tagButton3");
        again.sync();
        QCOMPARE(int(tomlOf(file)["editor"]["tagButton"].as_array()->size()), 1);
    }

    // Keys this build does not know (a later version's, a typo) stay when
    // the app writes the file; comments do not.
    void unknownKeysStay()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        writeAll(file, "version = 1 # comment\n[audio]\nfutureOption = \"x\"\n[later]\nthing = 3\n");
        {
            SettingsStore store(file);
            store.set("audio.volume", 70);
        }
        const toml::table settings = tomlOf(file);
        QCOMPARE(settings["audio"]["futureOption"].value_or(std::string()), std::string("x"));
        QCOMPARE(settings["later"]["thing"].value_or(std::int64_t{0}), std::int64_t{3});
        QCOMPARE(settings["audio"]["volume"].value_or(std::int64_t{0}), std::int64_t{70});
        QVERIFY(!readAll(file).contains("comment"));
    }

    // A hand edit's value of another type reads as the INI store read text.
    void handEditedValuesConvert()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        writeAll(file, "version = 1\n[audio]\ncacheFilesLimit = \"20\"\nautoCommit = 0\nvolume = { x = 1 }\n"
                       "[grid]\nfilterStyles = \"Sign\"\n");
        SettingsStore store(file);
        QCOMPARE(store.integer("audio.cacheFilesLimit"), 20);
        QVERIFY(!store.boolean("audio.autoCommit"));
        QCOMPARE(store.integer("audio.volume"), 50); // a table: unset
        QCOMPARE(store.list("grid.filterStyles"), QStringList{QStringLiteral("Sign")});
    }

    // CF1: hikari.ini converts once into the three files and becomes
    // hikari.ini.old; every value reads back as the INI store read it.
    void hikariIniConvertsIntoTheThreeFiles()
    {
        using namespace application;
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        const HotkeyMap map = changedHotkeys();
        std::map<std::string_view, SettingValue> expected;
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("registry/schema"), 1);
            for (const auto &setting : settingDefinitions()) {
                if (setting.disposition == SettingDisposition::Excluded || shortcutSetting(setting.id))
                    continue;
                SettingValue v = sampleFor(setting);
                // Real rows where the files give them structure.
                if (setting.id == "workspace.dialogGeometry")
                    v = std::vector<std::string>{"settings\t120,80,900,640", "find\t1,2,3,4"};
                else if (setting.id == "workspace.pickerFolders")
                    v = std::vector<std::string>{"openSubtitles\tfile:///x"};
                else if (setting.id == "automation.dialogAnswers")
                    v = std::vector<std::string>{"k1\t{\"answered\":{\"a\":1},\"button\":0,\"sent\":{}}"};
                else if (setting.id == "automation.dialogPresets")
                    v = std::vector<std::string>{"k1\t[{\"name\":\"P\",\"values\":{\"a\":\"b\"}}]"};
                else if (setting.id == "editor.tagButton2")
                    v = std::string("{\n\t\\blur2\n\t1\n\tBlur\n}");
                s.setValue(iniKey(setting), iniVariant(v));
                expected[setting.id] = v;
            }
            s.setValue(iniKey(*findSetting(kHotkeysSetting)), qLines(hotkeyLines(map, false)));
            s.setValue(iniKey(*findSetting(kAudioHotkeysSetting)), qLines(hotkeyLines(map, true)));
            s.setValue(iniKey(*findSetting(kAutomationHotkeysSetting)),
                       QStringList{QStringLiteral("Script a.lua-0\tCtrl+K\tA\tsha")});
        }
        const QByteArray before = readAll(ini);
        {
            SettingsStore store(file);
            QVERIFY(store.converted());
            QVERIFY(store.problems().empty());
            for (const auto &[id, v] : expected) {
                auto want = v;
                if (id == "workspace.dialogGeometry") // rows come back in key order
                    want = std::vector<std::string>{"find\t1,2,3,4", "settings\t120,80,900,640"};
                QVERIFY2(store.settings().value(id) == want, id.data());
            }
            HotkeyMap read, stored;
            readHotkeyLines(read, store.settings().list(kHotkeysSetting));
            readHotkeyLines(read, store.settings().list(kAudioHotkeysSetting));
            readHotkeyLines(stored, hotkeyLines(map, false));
            readHotkeyLines(stored, hotkeyLines(map, true));
            QVERIFY(read == stored);
            QCOMPARE(store.list(kAutomationHotkeysSetting.data()), QStringList{QStringLiteral("Script a.lua-0\tCtrl+K\tA\tsha")});
        }
        QVERIFY(!QFileInfo::exists(ini));
        QCOMPARE(readAll(ini + QStringLiteral(".old")), before); // never written
        for (const auto *name : {"settings.toml", "hotkeys.toml", "state.json"})
            QVERIFY2(QFileInfo::exists(dir.filePath(QLatin1String(name))), name);
        // The tag button slot is a table, the geometry an array of numbers.
        QCOMPARE(tomlOf(file)["editor"]["tagButton"][1]["tag"].value_or(std::string()), std::string("\\blur2"));
        QVERIFY(jsonOf(dir.filePath(QStringLiteral("state.json")))
                    .value(QLatin1String("workspace"))
                    .toObject()
                    .value(QLatin1String("dialogGeometry"))
                    .toObject()
                    .value(QLatin1String("find"))
                    .isArray());
        // Once only: an hikari.ini put back later is left alone.
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("profile/audio.volume"), 3);
        }
        SettingsStore again(file);
        QVERIFY(!again.converted());
        QCOMPARE(again.integer("audio.volume"), int(std::get<std::int64_t>(expected.at("audio.volume"))));
        QVERIFY(QFileInfo::exists(ini));
    }

    // The interim keys the rewrite used before the registry move onto their
    // settings as hikari.ini converts, and the controllers read them.
    void interimKeysConvertOntoTheirSettings()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            // As the interim code wrote them.
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("SelectLines/Options"), 1289);
            s.setValue(QStringLiteral("SelectLines/Recent"), QStringList{QStringLiteral("b"), QStringLiteral("a")});
            s.setValue(QStringLiteral("Subtitles/SaveWithVideoName"), true);
            s.setValue(QStringLiteral("Video/DontAskForBadResolution"), true);
            s.setValue(QStringLiteral("Convert/fps"), QStringLiteral("25"));
            s.setValue(QStringLiteral("Convert/prefix"), QStringLiteral("{\\an8}"));
            s.setValue(QStringLiteral("Convert/timePerCharacter"), 90);
            s.setValue(QStringLiteral("Updates/AutoCheck"), true);
            s.setValue(QStringLiteral("Updates/NextCheck"), qint64(1790000000));
            s.setValue(QStringLiteral("ShiftTimes/forward"), false);
            s.setValue(QStringLiteral("ShiftTimes/byFrames"), true);
            s.setValue(QStringLiteral("ShiftTimes/tagTimes"), true);
            s.setValue(QStringLiteral("ShiftTimes/timeMs"), 1500);
            s.setValue(QStringLiteral("ShiftTimes/styles"), QStringLiteral("Default,Sign"));
            s.setValue(QStringLiteral("ShiftTimes/postprocessor"), 19);
            s.setValue(QStringLiteral("ShiftTimes/Profiles"), QStringList{QStringLiteral("p1: 1000;0;0;0;0;0;0;0;0")});
            s.setValue(QStringLiteral("ColorPicker/Recent"), QStringLiteral("&H000000FF& &H0000FF00&"));
            s.setValue(QStringLiteral("ScriptInfo/LinkResolutions"), true);
            s.setValue(QStringLiteral("Grid/FilterBy"), 5);
            s.setValue(QStringLiteral("Grid/FilterStyles"), QStringList{QStringLiteral("Sign")});
            s.setValue(QStringLiteral("Grid/FilterInverted"), true);
            s.setValue(QStringLiteral("Grid/HiddenColumns"), 8193);
            s.setValue(QStringLiteral("Grid/CopyColumns"), 6);
            s.setValue(QStringLiteral("Editor/TagButtons"), 2);
            s.setValue(QStringLiteral("Editor/TagButton2"), QStringLiteral("{\n\t\\blur2\n\t1\n\tBlur\n}"));
            s.setValue(QStringLiteral("Recent/Subtitles"), QStringList{QStringLiteral("/a.ass"), QStringLiteral("/b.ass")});
            s.setValue(QStringLiteral("Recovery/Capacity"), 7);
            s.setValue(QStringLiteral("AutomationHotkeys/Script 1-0"),
                       QStringList{QStringLiteral("Ctrl+Alt+B"), QStringLiteral("Blur"), QStringLiteral("abc")});
            // Already under the registry: it wins over the interim key.
            s.setValue(QStringLiteral("Grid/PasteColumns"), 3);
            s.setValue(QStringLiteral("profile/grid.pasteColumns"), 9);
            s.setValue(QStringLiteral("Unrelated/Key"), QStringLiteral("dropped"));
        }
        SettingsStore store(file);
        QCOMPARE(store.integer("selectLines.options"), 1289);
        QCOMPARE(store.list("selectLines.recentSelections"), (QStringList{QStringLiteral("b"), QStringLiteral("a")}));
        QVERIFY(store.boolean("subtitles.saveWithVideoName"));
        QVERIFY(store.boolean("video.dontAskForBadResolution"));
        QCOMPARE(store.text("convert.fps"), QStringLiteral("25"));
        QCOMPARE(store.text("convert.assTagsToInsertInLine"), QStringLiteral("{\\an8}"));
        QCOMPARE(store.integer("convert.timePerCharacter"), 90);
        QCOMPARE(store.text("convert.style"), QStringLiteral("Default")); // never stored: the legacy default
        QVERIFY(store.boolean("updater.autoCheck"));
        QVERIFY(store.boolean("updater.checkForStable"));
        QCOMPARE(store.integer64("updater.nextCheck"), qint64(1790000000));
        QCOMPARE(store.integer("shiftTimes.options"), 16 | 32); // backward, frames, tag times
        QCOMPARE(store.integer("shiftTimes.time"), 1500);
        QCOMPARE(store.text("shiftTimes.styles"), QStringLiteral("Default,Sign"));
        QCOMPARE(store.integer("postprocessor.on"), 19);
        QCOMPARE(store.list("shiftTimes.profiles").size(), 1);
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H000000FF& &H0000FF00&"));
        QVERIFY(store.boolean("scriptProperties.linkResolutions"));
        QCOMPARE(store.integer("grid.filterBy"), 5);
        QCOMPARE(store.list("grid.filterStyles"), QStringList{QStringLiteral("Sign")});
        QVERIFY(store.boolean("grid.filterInverted"));
        QCOMPARE(store.integer("grid.hideColumns"), 8193);
        QCOMPARE(store.integer("grid.copyColumns"), 6);
        QCOMPARE(store.integer("grid.pasteColumns"), 9);
        QCOMPARE(store.integer("editor.tagButtons"), 2);
        QCOMPARE(store.list("recent.subtitles"), (QStringList{QStringLiteral("/a.ass"), QStringLiteral("/b.ass")}));
        QCOMPARE(store.integer("autosave.maxFiles"), 7);
        QCOMPARE(store.list(application::kAutomationHotkeysSetting.data()),
                 QStringList{QStringLiteral("Script 1-0\tCtrl+Alt+B\tBlur\tabc")});
        QVERIFY(!readAll(file).contains("Unrelated"));
        // The controllers that used the interim keys read the moved values.
        ui::TagButtonsController buttons(file);
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.button(1).tag, QStringLiteral("\\blur2"));
        QCOMPARE(buttons.button(1).name, QStringLiteral("Blur"));
        ui::GridFilterController filter(file);
        QCOMPARE(filter.filterBy(), 5);
        QVERIFY(filter.inverted());
        ui::ColourPickerController picker(file);
        QCOMPARE(picker.recent().at(0).toMap().value(QStringLiteral("r")).toInt(), 255);
        ui::ShiftTimesController shift(file);
        QVERIFY(!shift.settings().forward);
        QVERIFY(shift.settings().byFrames);
        QVERIFY(shift.settings().tagTimes);
        QVERIFY(!shift.settings().moveToVideoTime);
        QCOMPARE(shift.settings().timeMs, 1500);
        QCOMPARE(shift.settings().postprocessor, 19);
        QCOMPARE(shift.profileNames(), QStringList{QStringLiteral("p1")});
    }

    // K2: the colour settings K2 withdrew (A2's audio.spectrum* colours,
    // K1's icons.<appearance>.<state> colours) and WS3's editor switch
    // (workspace.editorOn) do not come into the new files. Other values do.
    void withdrawnColourSettingsLeaveTheProfile()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("registry/schema"), 1);
            for (const auto id : application::retiredSettings())
                s.setValue(QStringLiteral("profile/") + QString::fromLatin1(id.data(), qsizetype(id.size())),
                           QStringLiteral("#112233"));
            s.setValue(QStringLiteral("profile/appearance.highContrastBlack.accent"), QStringLiteral("#00FF00"));
            s.setValue(QStringLiteral("profile/audio.spectrumOn"), true);
            s.setValue(QStringLiteral("workspace/workspace.editorOn"), false);
            s.setValue(QStringLiteral("workspace/workspace.audioBoxHeight"), 230);
        }
        {
            SettingsStore store(file);
            QCOMPARE(store.text("appearance.highContrastBlack.accent"), QStringLiteral("#00FF00"));
            QVERIFY(store.boolean("audio.spectrumOn"));
            QCOMPARE(store.integer("workspace.audioBoxHeight"), 230);
        }
        const QByteArray text = readAll(file) + readAll(dir.filePath(QStringLiteral("state.json")));
        QVERIFY(!text.contains("spectrumBackground"));
        QVERIFY(!text.contains("icons"));
        QVERIFY(!text.contains("editorOn"));
        QVERIFY(text.contains("audioBoxHeight"));
    }

    // A broken file is never overwritten: kept as <name>.broken (numbered when
    // one is there), its settings at their defaults, the other files read.
    void aBrokenFileIsKeptAndItsSettingsAreDefaults()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        const QByteArray broken = "version = 1\n[audio]\nvolume = 70\ncacheFilesLimit = = 3\n";
        writeAll(file, broken);
        writeAll(dir.filePath(QStringLiteral("settings.toml.broken")), "older");
        writeAll(dir.filePath(QStringLiteral("state.json")), "{\"version\": 1, \"recent\": {\"video\": [\"/v.mkv\"]}}");
        {
            SettingsStore store(file);
            QCOMPARE(store.integer("audio.volume"), 50);
            QCOMPARE(store.list("recent.video"), QStringList{QStringLiteral("/v.mkv")});
            QCOMPARE(int(store.problems().size()), 1);
            const auto problem = store.problems().front();
            QCOMPARE(problem.failure.line, 4);
            QVERIFY(problem.failure.column > 0);
            QVERIFY(!problem.failure.message.empty());
            QCOMPARE(QString::fromStdU16String(problem.keptAs.u16string()),
                     dir.filePath(QStringLiteral("settings.toml.broken-2")));
            QVERIFY(!QFileInfo::exists(file)); // nothing changed yet
            store.set("audio.delay", 5);
        }
        QCOMPARE(readAll(dir.filePath(QStringLiteral("settings.toml.broken-2"))), broken);
        QCOMPARE(readAll(dir.filePath(QStringLiteral("settings.toml.broken"))), QByteArray("older"));
        QCOMPARE(readAll(file), QByteArray("version = 1\n\n[audio]\ndelay = 5\n"));
    }

    void aNewerVersionIsBroken_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("settings newer") << QStringLiteral("settings.toml") << QByteArray("version = 2\n[audio]\nvolume = 7\n");
        QTest::newRow("settings no version") << QStringLiteral("settings.toml") << QByteArray("[audio]\nvolume = 7\n");
        QTest::newRow("hotkeys text version") << QStringLiteral("hotkeys.toml") << QByteArray("version = \"1\"\n");
        QTest::newRow("state newer") << QStringLiteral("state.json") << QByteArray("{\"version\": 2}");
        QTest::newRow("state bad json") << QStringLiteral("state.json") << QByteArray("{\"version\": 1,\n \"recent\": }");
    }

    void aNewerVersionIsBroken()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, text);
        QTemporaryDir dir;
        writeAll(dir.filePath(name), text);
        SettingsStore store(dir.filePath(QStringLiteral("settings.toml")));
        QCOMPARE(int(store.problems().size()), 1);
        QCOMPARE(QFileInfo(QString::fromStdU16String(store.problems().front().file.u16string())).fileName(), name);
        QVERIFY(!store.contains("audio.volume"));
        QCOMPARE(readAll(dir.filePath(name + QStringLiteral(".broken"))), text);
        if (name == QLatin1String("state.json") && text.contains("recent")) {
            QCOMPARE(store.problems().front().failure.line, 2);
        }
    }

    // The panel reads the legacy options (SHIFT_TIMES_TIME 2000, backward
    // when SHIFT_TIMES_OPTIONS is unset) and stores nothing until it changes.
    void shiftTimesPanelStartsFromTheLegacyOptions()
    {
        SettingsStore store;
        ui::ShiftTimesController shift(store);
        QCOMPARE(shift.settings().timeMs, 2000);
        QVERIFY(!shift.settings().forward);
        QVERIFY(!store.contains("shiftTimes.time"));
        QVERIFY(!store.contains("shiftTimes.options"));
        QVERIFY(!store.contains("postprocessor.on"));
        QVERIFY(!store.contains("shiftTimes.profiles"));
        auto map = shift.settingsMap();
        map.insert(QStringLiteral("forward"), true);
        shift.setSettingsMap(map);
        QCOMPARE(store.integer("shiftTimes.options"), 1);
        QCOMPARE(store.integer("shiftTimes.time"), 2000);
    }

    void shiftTimesKeepsBitsLegacyDoesNotKnow()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            SettingsStore store(file);
            store.set("shiftTimes.options", 128 | 1);
        }
        ui::ShiftTimesController shift(file);
        QVERIFY(shift.settings().forward);
        auto map = shift.settingsMap();
        map.insert(QStringLiteral("forward"), false);
        map.insert(QStringLiteral("moveToVideoTime"), true);
        shift.setSettingsMap(map);
        QCOMPARE(SettingsStore(file).integer("shiftTimes.options"), 128 | 4);
    }

    // TS1: the panel's collapsed sections are kept apart from the shift's
    // options: all open by default, stored only when one closes, read back.
    void shiftTimesRemembersCollapsedSections()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("settings.toml"));
        {
            SettingsStore store(file);
            ui::ShiftTimesController shift(store);
            QVERIFY(shift.collapsedSections().isEmpty());
            QSignalSpy spy(&shift, &ui::ShiftTimesController::collapsedSectionsChanged);
            shift.setCollapsedSections({QStringLiteral("amount"), QStringLiteral("snap")});
            QCOMPARE(spy.count(), 1);
            shift.setCollapsedSections({QStringLiteral("amount"), QStringLiteral("snap")});
            QCOMPARE(spy.count(), 1); // unchanged
            QVERIFY(!store.contains("shiftTimes.options"));
            QVERIFY(!store.contains("shiftTimes.time"));
        }
        ui::ShiftTimesController shift(file);
        QCOMPARE(shift.collapsedSections(), (QStringList{QStringLiteral("amount"), QStringLiteral("snap")}));
    }
};

QTEST_GUILESS_MAIN(SettingsStoreTests)
#include "settings_store_tests.moc"
