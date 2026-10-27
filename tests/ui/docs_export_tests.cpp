// DX1 (#367): `HikariSub --export-docs <dir>` against the live registries:
// every command the Hotkeys page lists and every setting the Settings window
// binds is there once, with the defaults a new profile has; the menu places
// come from the real menus; two exports give the same bytes; the program's
// own switch writes the files without a display and exits.

#include "hikari/app/application.h"
#include "hikari/app/docs_export.h"
#include "hikari/application/guide_links.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/settings.h"
#include "settings_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QStringList kFiles{QStringLiteral("meta.json"), QStringLiteral("commands.json"), QStringLiteral("settings.json"),
                         QStringLiteral("guide-links.json")};

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject json(const QTemporaryDir &dir, const QString &name)
{
    return QJsonDocument::fromJson(read(dir.filePath(name))).object();
}

// The first line where two exports differ, for the failure message.
QByteArray difference(const QString &name, const QByteArray &a, const QByteArray &b)
{
    const QList<QByteArray> x = a.split('\n'), y = b.split('\n');
    for (qsizetype i = 0; i < qMax(x.size(), y.size()); ++i)
        if (x.value(i) != y.value(i))
            return name.toUtf8() + ':' + QByteArray::number(i + 1) + ": " + x.value(i) + " | " + y.value(i);
    return name.toUtf8();
}

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

} // namespace

class DocsExportTests : public QObject {
    Q_OBJECT

    QTemporaryDir first, second;

private slots:
    void initTestCase()
    {
        QVERIFY(first.isValid() && second.isValid());
        QString error;
        QVERIFY2(app::exportDocs(first.path(), &error), qPrintable(error));
        QVERIFY2(app::exportDocs(second.path(), &error), qPrintable(error));
    }

    void twoExportsGiveTheSameBytes()
    {
        for (const QString &name : kFiles) {
            const QByteArray a = read(first.filePath(name));
            QVERIFY2(!a.isEmpty(), qPrintable(name));
            const QByteArray b = read(second.filePath(name));
            QVERIFY2(a == b, difference(name, a, b).constData());
            QVERIFY2(QJsonDocument::fromJson(a).isObject(), qPrintable(name));
        }
    }

    void metaNamesTheBuild()
    {
        const QJsonObject meta = json(first, QStringLiteral("meta.json"));
        QCOMPARE(meta.value(QStringLiteral("schema")).toInt(), app::kDocsSchema);
        QCOMPARE(meta.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(meta.value(QStringLiteral("version")).toString(), QStringLiteral(HIKARI_VERSION));
        QCOMPARE(meta.value(QStringLiteral("site")).toString(), QStringLiteral("https://hikarisub.altqx.com"));
        QCOMPARE(meta.value(QStringLiteral("files")).toArray().size(), 3);
    }

    // Every listed command once, by id, with its name, window and defaults.
    void everyCommandIsThere()
    {
        const QJsonArray commands = json(first, QStringLiteral("commands.json")).value(QStringLiteral("commands")).toArray();
        QMap<int, QJsonObject> byId;
        int previous = -1;
        for (const QJsonValue &v : commands) {
            const QJsonObject c = v.toObject();
            const int id = c.value(QStringLiteral("id")).toInt();
            QVERIFY2(!byId.contains(id), qPrintable(QString::number(id)));
            QVERIFY(id > previous); // by id
            previous = id;
            byId.insert(id, c);
        }
        int listed = 0;
        for (const application::HotkeyAction &action : application::hotkeyActions()) {
            const std::string name = application::hotkeyName(action.id);
            if (name.empty() || application::isRetiredHotkey(action.id)) {
                QVERIFY2(!byId.contains(action.id), action.symbol.data());
                continue;
            }
            ++listed;
            QVERIFY2(byId.contains(action.id), action.symbol.data());
            const QJsonObject c = byId.value(action.id);
            QCOMPARE(c.value(QStringLiteral("symbol")).toString(), qs(action.symbol));
            QCOMPARE(c.value(QStringLiteral("name")).toString(), qs(name));
            QCOMPARE(c.value(QStringLiteral("context")).toString(),
                     qs(application::hotkeyWindowName(application::hotkeyType(action.id))));
        }
        QCOMPARE(byId.size(), listed);
        // The defaults, per window, as a new profile has them.
        int bound = 0;
        for (const auto &[key, hotkey] : application::defaultHotkeys()) {
            if (hotkey.accel.empty() || !byId.contains(key.id))
                continue;
            ++bound;
            const QJsonObject expected{{QStringLiteral("context"), qs(application::hotkeyWindowName(key.type))},
                                       {QStringLiteral("keys"), qs(application::qtKeysOfAccel(hotkey.accel))}};
            QVERIFY2(byId.value(key.id).value(QStringLiteral("defaultKeys")).toArray().contains(expected),
                     qPrintable(QString::number(key.id)));
        }
        int exported = 0;
        for (const QJsonObject &c : byId)
            exported += c.value(QStringLiteral("defaultKeys")).toArray().size();
        QCOMPARE(exported, bound);

        // User guide (DX1): GLOBAL_HELP, F1, Help > User guide.
        const QJsonObject help = byId.value(application::hotkeyIdOf("GLOBAL_HELP"));
        QCOMPARE(help.value(QStringLiteral("id")).toInt(), 5112);
        QCOMPARE(help.value(QStringLiteral("name")).toString(), QStringLiteral("User guide"));
        QCOMPARE(help.value(QStringLiteral("defaultKeys")).toArray(),
                 (QJsonArray{QJsonObject{{QStringLiteral("context"), QStringLiteral("Global")},
                                         {QStringLiteral("keys"), QStringLiteral("F1")}}}));
        QCOMPARE(help.value(QStringLiteral("menus")).toArray(), QJsonArray{QStringLiteral("Help > User guide")});
    }

    // The places come from the menus as built: the menu bar's and the Grid's.
    void menusAreTheRealOnes()
    {
        const QJsonArray commands = json(first, QStringLiteral("commands.json")).value(QStringLiteral("commands")).toArray();
        QMap<QString, QStringList> menus;
        for (const QJsonValue &v : commands)
            for (const QJsonValue &m : v.toObject().value(QStringLiteral("menus")).toArray())
                menus[v.toObject().value(QStringLiteral("symbol")).toString()] << m.toString();
        QCOMPARE(menus.value(QStringLiteral("GLOBAL_SAVE_SUBS")), QStringList{QStringLiteral("File > Save")});
        QVERIFY(menus.value(QStringLiteral("GLOBAL_CONVERT_TO_SRT")).contains(QStringLiteral("Subtitles > Conversion > Convert to SRT")));
        QVERIFY(menus.value(QStringLiteral("GRID_INSERT_BEFORE")).contains(QStringLiteral("Grid menu > Insert > Insert before")));
        // A text with its keys after a tab is shown without them.
        for (const QStringList &places : menus)
            for (const QString &place : places) {
                QVERIFY2(!place.contains(QLatin1Char('\t')) && !place.contains(QLatin1Char('&')), qPrintable(place));
                QVERIFY2(place.count(QStringLiteral(" > ")) >= 1, qPrintable(place));
            }
    }

    // Every bound control's setting once, on a known page, labelled, with
    // the value a new profile has.
    void everySettingIsThere()
    {
        const QJsonObject doc = json(first, QStringLiteral("settings.json"));
        QSet<QString> pages;
        for (const QJsonValue &p : doc.value(QStringLiteral("pages")).toArray())
            pages.insert(p.toObject().value(QStringLiteral("id")).toString());
        QVERIFY(pages.contains(QStringLiteral("general")) && pages.contains(QStringLiteral("appearance")));

        QTemporaryDir profile;
        app::Application::Options o;
        o.settingsFile = profile.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.systemUiLanguages = {QStringLiteral("en")};
        app::Application fresh(o);

        QMap<QString, QJsonObject> byKey;
        for (const QJsonValue &v : doc.value(QStringLiteral("settings")).toArray()) {
            const QJsonObject s = v.toObject();
            const QString key = s.value(QStringLiteral("key")).toString();
            QVERIFY2(!byKey.contains(key), qPrintable(key));
            byKey.insert(key, s);
            QVERIFY2(application::findSetting(key.toStdString()), qPrintable(key));
            QVERIFY2(pages.contains(s.value(QStringLiteral("page")).toString()), qPrintable(key));
            QVERIFY2(!s.value(QStringLiteral("label")).toString().isEmpty(), qPrintable(key));
            QVERIFY2(!s.value(QStringLiteral("section")).toString().isEmpty(), qPrintable(key));
            QCOMPARE(s.value(QStringLiteral("default")), QJsonValue::fromVariant(fresh.settingsStore()->value(key)));
        }
        for (const application::OptionsBinding &binding : application::optionsBindings()) {
            QVERIFY2(byKey.contains(qs(binding.setting)), binding.setting.data());
            if (!binding.sizeSetting.empty())
                QVERIFY2(byKey.contains(qs(binding.sizeSetting)), binding.sizeSetting.data());
        }
        for (const char *key : {"appearance.theme", "appearance.followSystem", "appearance.titleBar", "program.font",
                                "program.fontSize"})
            QVERIFY2(byKey.contains(QLatin1String(key)), key);
        // Every theme's settings, not only those of the theme shown.
        for (const application::SettingDefinition &definition : application::settingDefinitions())
            if (definition.id.starts_with("appearance."))
                QVERIFY2(byKey.contains(qs(definition.id)), definition.id.data());
        QCOMPARE(byKey.value(QStringLiteral("appearance.lightAccent")).value(QStringLiteral("label")).toString(),
                 QStringLiteral("Accent (Light)"));
        QCOMPARE(byKey.value(QStringLiteral("appearance.highContrastBlack.border")).value(QStringLiteral("label")).toString(),
                 QStringLiteral("Border color (High contrast black)"));
        QCOMPARE(byKey.value(QStringLiteral("appearance.highContrastBlack.border")).value(QStringLiteral("section")).toString(),
                 QStringLiteral("UI colors"));
        QVERIFY(!byKey.value(QStringLiteral("appearance.darkAccent")).value(QStringLiteral("values")).toArray().isEmpty());
        // Windows' own rows, on every platform, marked.
        const QJsonObject player = byKey.value(QStringLiteral("video.playbackPlayer"));
        QCOMPARE(player.value(QStringLiteral("platform")).toString(), QStringLiteral("windows"));
        QCOMPARE(player.value(QStringLiteral("page")).toString(), QStringLiteral("video"));
        QCOMPARE(player.value(QStringLiteral("values")).toArray().size(), 2);
        const QJsonObject api = byKey.value(QStringLiteral("audio.outputHostApi"));
        QCOMPARE(api.value(QStringLiteral("platform")).toString(), QStringLiteral("windows"));
        QCOMPARE(api.value(QStringLiteral("hint")).toString(), QStringLiteral("Applies when the output opens again."));
        QCOMPARE(api.value(QStringLiteral("values")).toArray().at(1).toObject().value(QStringLiteral("label")).toString(),
                 QStringLiteral("DirectSound"));

        const QJsonObject tabs = byKey.value(QStringLiteral("program.tabTextMaxChars"));
        QCOMPARE(tabs.value(QStringLiteral("page")).toString(), QStringLiteral("general"));
        QCOMPARE(tabs.value(QStringLiteral("section")).toString(), QStringLiteral("Tabs"));
        QCOMPARE(tabs.value(QStringLiteral("label")).toString(), QStringLiteral("Tab name length"));
        QCOMPARE(tabs.value(QStringLiteral("control")).toString(), QStringLiteral("number"));
        QCOMPARE(tabs.value(QStringLiteral("min")).toInt(), 20);
        QCOMPARE(tabs.value(QStringLiteral("max")).toInt(), 150);
        QCOMPARE(tabs.value(QStringLiteral("hint")).toString(),
                 QStringLiteral("Number of tab name characters. Range from 20 to 150"));
        const QJsonObject language = byKey.value(QStringLiteral("program.language"));
        QCOMPARE(language.value(QStringLiteral("control")).toString(), QStringLiteral("choice"));
        QCOMPARE(language.value(QStringLiteral("values")).toArray().first(),
                 QJsonValue(QJsonObject{{QStringLiteral("value"), QStringLiteral("en")},
                                        {QStringLiteral("label"), QStringLiteral("English")}}));
        QCOMPARE(byKey.value(QStringLiteral("subtitles.openInNewTab")).value(QStringLiteral("control")).toString(),
                 QStringLiteral("check"));
    }

    void guideLinksAreTheTable()
    {
        const QJsonObject doc = json(first, QStringLiteral("guide-links.json"));
        QCOMPARE(doc.value(QStringLiteral("home")).toString(), QStringLiteral("/en/docs"));
        const QJsonArray links = doc.value(QStringLiteral("links")).toArray();
        QCOMPARE(links.size(), qsizetype(application::guideLinks().size()));
        qsizetype i = 0;
        for (const application::GuideLink &link : application::guideLinks()) {
            const QJsonObject o = links.at(i++).toObject();
            QCOMPARE(o.value(QStringLiteral("kind")).toString(), qs(application::guideSurfaceName(link.kind)));
            QCOMPARE(o.value(QStringLiteral("id")).toString(), qs(link.id));
            QCOMPARE(o.value(QStringLiteral("path")).toString(), qs(link.path));
        }
    }

    // The program's switch: no display needed, the files written, exit 0,
    // the same bytes twice; a missing folder is refused.
    void theSwitchWritesTheFiles()
    {
        QTemporaryDir out;
        QVERIFY(out.isValid());
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const char *name : {"DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"})
            environment.remove(QLatin1String(name));
        environment.insert(QStringLiteral("HOME"), out.filePath(QStringLiteral("home")));
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), out.filePath(QStringLiteral("home/config")));
        environment.insert(QStringLiteral("XDG_DATA_HOME"), out.filePath(QStringLiteral("home/data")));
        QProcess program;
        program.setProcessEnvironment(environment);
        for (const char *folder : {"docs", "again"}) {
            program.start(QStringLiteral(HIKARI_PROGRAM), {QStringLiteral("--export-docs"), out.filePath(QLatin1String(folder))});
            QVERIFY(program.waitForFinished(120000));
            QVERIFY2(program.exitStatus() == QProcess::NormalExit && program.exitCode() == 0,
                     program.readAllStandardError().constData());
        }
        for (const QString &name : kFiles) {
            const QByteArray a = read(out.filePath(QStringLiteral("docs/") + name));
            const QByteArray again = read(out.filePath(QStringLiteral("again/") + name));
            QVERIFY2(a == again, difference(name, a, again).constData());
            // The Interface language's entries are the catalogs beside the
            // program, which this test's folder has not: the keys are the same.
            const QByteArray b = read(first.filePath(name));
            if (name != QLatin1String("settings.json"))
                QVERIFY2(a == b, difference(name, a, b).constData());
        }
        const auto keys = [](const QByteArray &bytes) {
            QStringList list;
            for (const QJsonValue &v : QJsonDocument::fromJson(bytes).object().value(QStringLiteral("settings")).toArray())
                list << v.toObject().value(QStringLiteral("key")).toString();
            return list;
        };
        QCOMPARE(keys(read(out.filePath(QStringLiteral("docs/settings.json")))), keys(read(first.filePath(QStringLiteral("settings.json")))));
        // Nothing of the user's: the profile was a temporary one.
        QVERIFY(!QFile::exists(out.filePath(QStringLiteral("home/config/HikariSub/hikari.ini"))));

        program.start(QStringLiteral(HIKARI_PROGRAM), {QStringLiteral("--export-docs")});
        QVERIFY(program.waitForFinished(60000));
        QCOMPARE(program.exitCode(), 2);
    }
};

QTEST_MAIN(DocsExportTests)
#include "docs_export_tests.moc"
