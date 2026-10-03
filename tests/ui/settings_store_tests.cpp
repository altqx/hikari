// O1: the settings registry over the INI file. Every setting round-trips
// through the file; the INI keys the rewrite used before the registry move
// onto their settings once ("registry/schema"), and the controllers that
// used them read the moved values.

#include "colour_picker_controller.h"
#include "grid_filter_controller.h"
#include "settings_store.h"
#include "shift_times_controller.h"
#include "tag_buttons_controller.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
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

} // namespace

class SettingsStoreTests : public QObject {
    Q_OBJECT

private slots:
    void everySettingRoundTripsThroughTheFile()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        {
            SettingsStore store(ini);
            for (const auto &s : application::settingDefinitions())
                if (s.disposition != application::SettingDisposition::Excluded)
                    QVERIFY2(store.settings().set(s.id, sampleFor(s)), s.id.data());
        }
        SettingsStore again(ini);
        for (const auto &s : application::settingDefinitions()) {
            if (s.disposition == application::SettingDisposition::Excluded) {
                QVERIFY(!again.settings().isSet(s.id));
                continue;
            }
            QVERIFY2(again.settings().isSet(s.id), s.id.data());
            QVERIFY2(again.settings().value(s.id) == sampleFor(s), s.id.data());
        }
        // Under "<scope>/<id>" in the file.
        const QSettings file(ini, QSettings::IniFormat);
        QCOMPARE(file.value(QStringLiteral("profile/grid.hideColumns")).toLongLong(),
                 std::get<std::int64_t>(sampleFor(*application::findSetting("grid.hideColumns"))));
        QVERIFY(file.contains(QStringLiteral("collections/editor.tagButton1")));
        QVERIFY(file.contains(QStringLiteral("workspace/workspace.windowSize")));
        QVERIFY(!file.contains(QStringLiteral("profile/program.theme")));
        QCOMPARE(file.value(QStringLiteral("registry/schema")).toInt(), SettingsStore::kSchema);
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

    void interimKeysMoveOnceOntoTheirSettings()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
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
            s.setValue(QStringLiteral("Unrelated/Key"), QStringLiteral("kept"));
        }
        SettingsStore store(ini);
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
        {
            const QSettings s(ini, QSettings::IniFormat);
            for (const auto &k : application::interimKeys())
                QVERIFY2(!s.contains(QString::fromUtf8(k.key.data(), qsizetype(k.key.size()))), k.key.data());
            QVERIFY(!s.contains(QStringLiteral("ShiftTimes/forward")));
            QVERIFY(s.childGroups().contains(QStringLiteral("AutomationHotkeys")) == false);
            QCOMPARE(s.value(QStringLiteral("Unrelated/Key")).toString(), QStringLiteral("kept"));
            QCOMPARE(s.value(QStringLiteral("registry/schema")).toInt(), 1);
        }
        // The controllers that used the interim keys read the moved values.
        ui::TagButtonsController buttons(ini);
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.button(1).tag, QStringLiteral("\\blur2"));
        QCOMPARE(buttons.button(1).name, QStringLiteral("Blur"));
        ui::GridFilterController filter(ini);
        QCOMPARE(filter.filterBy(), 5);
        QVERIFY(filter.inverted());
        ui::ColourPickerController picker(ini);
        QCOMPARE(picker.recent().at(0).toMap().value(QStringLiteral("r")).toInt(), 255);
        ui::ShiftTimesController shift(ini);
        QVERIFY(!shift.settings().forward);
        QVERIFY(shift.settings().byFrames);
        QVERIFY(shift.settings().tagTimes);
        QVERIFY(!shift.settings().moveToVideoTime);
        QCOMPARE(shift.settings().timeMs, 1500);
        QCOMPARE(shift.settings().postprocessor, 19);
        QCOMPARE(shift.profileNames(), QStringList{QStringLiteral("p1")});
        // Once only: interim keys written later are left alone.
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QStringLiteral("Grid/HiddenColumns"), 1);
        }
        SettingsStore third(ini);
        QCOMPARE(third.integer("grid.hideColumns"), 8193);
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value(QStringLiteral("Grid/HiddenColumns")).toInt(), 1);
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
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        {
            SettingsStore store(ini);
            store.set("shiftTimes.options", 128 | 1);
        }
        ui::ShiftTimesController shift(ini);
        QVERIFY(shift.settings().forward);
        auto map = shift.settingsMap();
        map.insert(QStringLiteral("forward"), false);
        map.insert(QStringLiteral("moveToVideoTime"), true);
        shift.setSettingsMap(map);
        QCOMPARE(SettingsStore(ini).integer("shiftTimes.options"), 128 | 4);
    }
};

QTEST_GUILESS_MAIN(SettingsStoreTests)
#include "settings_store_tests.moc"
