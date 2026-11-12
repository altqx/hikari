#include "hikari/app/plugin_manager.h"
#include "hikari/api/api.h"
#include "hikari/backends/desktop_handoff.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::PluginManager;

namespace {

const QString kKeyA = QStringLiteral("EFCDAB8967452301");
const QString kKeyB = QStringLiteral("1032547698BADCFE");

QUrl fixture(const char *name)
{
    return QUrl::fromLocalFile(QStringLiteral(HIKARI_PLUGIN_FIXTURES "/") + QString::fromLatin1(name));
}

bool touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
}

}

class PluginManagerTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    PluginManager::Folders folders() const
    {
        return {m_dir.filePath(QStringLiteral("user/plugins")), m_dir.filePath(QStringLiteral("user/plugin-data")),
                m_dir.filePath(QStringLiteral("cache/plugins")), m_dir.filePath(QStringLiteral("user/plugins.json"))};
    }
    QString installed(const char *file) const
    {
        return m_dir.filePath(QStringLiteral("user/plugins/com.example.hello/") + QString::fromLatin1(file));
    }
    static QVariantMap only(const PluginManager &manager)
    {
        const QVariantList rows = manager.plugins();
        return rows.size() == 1 ? rows.first().toMap() : QVariantMap();
    }
    static QString refusal(PluginManager &manager, const char *name)
    {
        const QVariantMap review = manager.review(fixture(name));
        return review.value(QStringLiteral("ok")).toBool() ? QString() : review.value(QStringLiteral("message")).toString();
    }

private slots:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
    }

    void aSignedPackageInstallsUpdatesTurnsOffAndUninstalls()
    {
        PluginManager manager(folders());
        QVERIFY(manager.available());
        QVERIFY(manager.plugins().isEmpty());
        QSignalSpy changed(&manager, &PluginManager::pluginsChanged);

        QVariantMap review = manager.review(fixture("hello-1.0.0.hikariplugin"));
        QVERIFY2(review.value(QStringLiteral("ok")).toBool(), qPrintable(review.value(QStringLiteral("message")).toString()));
        QCOMPARE(review.value(QStringLiteral("change")).toString(), QStringLiteral("install"));
        QCOMPARE(review.value(QStringLiteral("name")).toString(), QStringLiteral("Hello"));
        QCOMPARE(review.value(QStringLiteral("keyId")).toString(), kKeyA);
        QVERIFY(!review.value(QStringLiteral("needsRetrust")).toBool());
        QCOMPARE(manager.install(false), QString());
        QCOMPARE(changed.count(), 1);
        QVERIFY(QFile::exists(installed("main.lua")));
        QVariantMap row = only(manager);
        QCOMPARE(row.value(QStringLiteral("version")).toString(), QStringLiteral("1.0.0"));
        QCOMPARE(row.value(QStringLiteral("description")).toString(), QStringLiteral("Says hello"));
        QCOMPARE(row.value(QStringLiteral("keyId")).toString(), kKeyA);
        QVERIFY(row.value(QStringLiteral("enabled")).toBool());

        const QString notes = m_dir.filePath(QStringLiteral("user/plugin-data/com.example.hello/notes.txt"));
        QVERIFY(touch(notes));
        QVERIFY(touch(m_dir.filePath(QStringLiteral("cache/plugins/com.example.hello/index.bin"))));
        QCOMPARE(manager.setEnabled(QStringLiteral("com.example.hello"), false), QString());
        QVERIFY(!only(manager).value(QStringLiteral("enabled")).toBool());

        review = manager.review(fixture("hello-1.1.0.hikariplugin"));
        QCOMPARE(review.value(QStringLiteral("change")).toString(), QStringLiteral("update"));
        QCOMPARE(review.value(QStringLiteral("installedVersion")).toString(), QStringLiteral("1.0.0"));
        QCOMPARE(manager.install(false), QString());
        row = only(manager);
        QCOMPARE(row.value(QStringLiteral("version")).toString(), QStringLiteral("1.1.0"));
        QVERIFY(!row.value(QStringLiteral("enabled")).toBool());
        QVERIFY(QFile::exists(notes));
        QVERIFY(!QDir(m_dir.filePath(QStringLiteral("cache/plugins/com.example.hello"))).exists());

        QCOMPARE(manager.review(fixture("hello-1.1.0.hikariplugin")).value(QStringLiteral("change")).toString(),
                 QStringLiteral("reinstall"));
        manager.cancelReview();
        QVERIFY(!manager.install(false).isEmpty());

        QVERIFY(manager.hasData(QStringLiteral("com.example.hello")));
        QCOMPARE(manager.uninstall(QStringLiteral("com.example.hello"), false), QString());
        QVERIFY(manager.plugins().isEmpty());
        QVERIFY(!QFile::exists(installed("main.lua")));
        QVERIFY(QFile::exists(notes));

        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());
        QCOMPARE(manager.uninstall(QStringLiteral("com.example.hello"), true), QString());
        QVERIFY(!QFile::exists(notes));
        QVERIFY(!manager.hasData(QStringLiteral("com.example.hello")));
    }

    void aKeyChangeOnUpdateIsRefusedUntilRetrusted()
    {
        PluginManager manager(folders());
        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());

        const QVariantMap review = manager.review(fixture("hello-2.0.0-other-key.hikariplugin"));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("needsRetrust")).toBool());
        QCOMPARE(review.value(QStringLiteral("pinnedKeyId")).toString(), kKeyA);
        QCOMPARE(review.value(QStringLiteral("keyId")).toString(), kKeyB);
        QVERIFY(manager.install(false).contains(QStringLiteral("signing key changed")));
        QCOMPARE(only(manager).value(QStringLiteral("version")).toString(), QStringLiteral("1.0.0"));

        QVERIFY(manager.review(fixture("hello-2.0.0-other-key.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(true), QString());
        QCOMPARE(only(manager).value(QStringLiteral("keyId")).toString(), kKeyB);
        QCOMPARE(only(manager).value(QStringLiteral("version")).toString(), QStringLiteral("2.0.0"));
    }

    void anUnsignedPackageIsUnverifiedAndCannotQuietlyReplaceASignedOne()
    {
        PluginManager manager(folders());
        QVariantMap review = manager.review(fixture("hello-1.2.0-unsigned.hikariplugin"));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QCOMPARE(review.value(QStringLiteral("keyId")).toString(), QString());
        QVERIFY(!review.value(QStringLiteral("needsRetrust")).toBool());
        manager.cancelReview();

        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());
        review = manager.review(fixture("hello-1.2.0-unsigned.hikariplugin"));
        QVERIFY(review.value(QStringLiteral("needsRetrust")).toBool());
        QCOMPARE(review.value(QStringLiteral("pinnedKeyId")).toString(), kKeyA);
        QVERIFY(!manager.install(false).isEmpty());
        QVERIFY(manager.review(fixture("hello-1.2.0-unsigned.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(true), QString());
        QCOMPARE(only(manager).value(QStringLiteral("keyId")).toString(), QString());
    }

    void refusalsSayWhy()
    {
        PluginManager manager(folders());
        QVERIFY(refusal(manager, "hello-tampered.hikariplugin").contains(QStringLiteral("changed after signing")));
        QCOMPARE(refusal(manager, "needs-api-99.hikariplugin"),
                 QStringLiteral("This plugin needs Hikari API 99, and this HikariSub has API %1. Update HikariSub to install it.").arg(hikari::api::kApiVersion));
        QVERIFY(refusal(manager, "bad-schema.hikariplugin").startsWith(QStringLiteral("hikari-plugin.json fails the plugin schema at /id: ")));
        QVERIFY(refusal(manager, "not-a-zip.hikariplugin").contains(QStringLiteral("not a plugin package")));
        QCOMPARE(refusal(manager, "bad-icon.hikariplugin"),
                 QStringLiteral("The package breaks the media rules: icon.png is 128x128 px, not 256x256 px."));
        QVERIFY(refusal(manager, "missing.hikariplugin").contains(QStringLiteral("can’t read")));
        QCOMPARE(refusal(manager, "board-restricted-qml.hikariplugin"), QStringLiteral("Only Full trust plugins can ship QML panels."));
        QCOMPARE(refusal(manager, "board-ui-import.hikariplugin"),
                 QStringLiteral("qml/parts/Badge.qml imports Hikari.Ui, which plugin panels can’t use."));
        QVERIFY(manager.plugins().isEmpty());
        QVERIFY(!QDir(folders().plugins).exists());
    }

    void aPluginWithQmlPanelsSaysItShowsItsOwnPanels()
    {
        PluginManager manager(folders());
        const QVariantMap review = manager.review(fixture("board-1.0.0-qml.hikariplugin"));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QCOMPARE(review.value(QStringLiteral("permissions")).toStringList(),
                 (QStringList{QStringLiteral("Full trust: it may run native code outside HikariSub’s limits."),
                              QStringLiteral("Shows its own panels inside HikariSub")}));
        QCOMPARE(manager.install(true), QString());
        QCOMPARE(only(manager).value(QStringLiteral("permissions")).toStringList().size(), 2);
    }

    void detailsListWhatAPluginAsksForAddsAndKeeps()
    {
        PluginManager manager(folders());
        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());
        QFile manifest(installed("hikari-plugin.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0", "api": 1, "main": "main.lua",
            "authors": ["Ana", "Bo"], "icon": "icon.png",
            "permissions": {"documents.all": "It checks every tab."},
            "contributes": {"commands": [{"id": "greet", "title": "Say hello"}],
                            "panels": [{"id": "notes", "title": "Notes"}],
                            "settings": [{"id": "count", "title": "Count", "type": "integer", "default": 1, "maximum": 5}]}})");
        manifest.close();
        PluginManager reread(folders());
        const QVariantMap row = only(reread);
        QCOMPARE(row.value(QStringLiteral("authors")).toString(), QStringLiteral("Ana, Bo"));
        QCOMPARE(row.value(QStringLiteral("icon")).toUrl(), QUrl::fromLocalFile(installed("icon.png")));
        QCOMPARE(row.value(QStringLiteral("adds")).toStringList(),
                 (QStringList{QStringLiteral("Command: Say hello"), QStringLiteral("Panel: Notes")}));
        QCOMPARE(row.value(QStringLiteral("permissions")).toStringList().size(), 1);
        QVERIFY(row.value(QStringLiteral("permissions")).toStringList().first().contains(QStringLiteral("It checks every tab.")));
        QUrl opened;
        const auto routes = hikari::backends::desktop::replaceRoutes({[&](const QUrl &url) { opened = url; return true; }, {}, {}});
        QVERIFY(reread.openDataFolder(QStringLiteral("com.example.hello")));
        hikari::backends::desktop::replaceRoutes(routes);
        QCOMPARE(opened, QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("user/plugin-data/com.example.hello"))));
        QVERIFY(QDir(m_dir.filePath(QStringLiteral("user/plugin-data/com.example.hello"))).exists());
        QVariantMap count = row.value(QStringLiteral("settings")).toList().value(0).toMap();
        QCOMPARE(count.value(QStringLiteral("id")).toString(), QStringLiteral("count"));
        QCOMPARE(count.value(QStringLiteral("type")).toString(), QStringLiteral("integer"));
        QCOMPARE(count.value(QStringLiteral("value")).toInt(), 1);

        QSignalSpy kept(&reread, &PluginManager::settingChanged);
        QCOMPARE(reread.setSetting(QStringLiteral("com.example.hello"), QStringLiteral("count"), QStringLiteral("4")), QString());
        QCOMPARE(kept.count(), 1);
        QCOMPARE(kept.first().at(2).toString(), QStringLiteral("4"));
        count = only(reread).value(QStringLiteral("settings")).toList().value(0).toMap();
        QCOMPARE(count.value(QStringLiteral("value")).toInt(), 4);
        QVERIFY(!reread.setSetting(QStringLiteral("com.example.hello"), QStringLiteral("count"), QStringLiteral("9")).isEmpty());
        QCOMPARE(kept.count(), 1);
    }

    void rememberedFoldersAreListedAndCanBeForgotten()
    {
        PluginManager manager(folders());
        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());
        QFile manifest(installed("hikari-plugin.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0", "api": 1, "main": "main.lua",
            "authors": ["Ana"],
            "permissions": {"files": [{"id": "fonts", "title": "Font folder", "writable": false, "reason": "It measures lines."}]}})");
        manifest.close();
        QFile registry(folders().registry);
        QVERIFY(registry.open(QIODevice::ReadOnly));
        QJsonObject json = QJsonDocument::fromJson(registry.readAll()).object();
        registry.close();
        QJsonObject hello = json.value(QStringLiteral("plugins")).toObject().value(QStringLiteral("com.example.hello")).toObject();
        const QString fonts = m_dir.filePath(QStringLiteral("fonts"));
        hello.insert(QStringLiteral("grants"),
                     QJsonObject{{QStringLiteral("fonts"), QJsonObject{{QStringLiteral("path"), fonts}, {QStringLiteral("writable"), true}}}});
        QJsonObject plugins = json.value(QStringLiteral("plugins")).toObject();
        plugins.insert(QStringLiteral("com.example.hello"), hello);
        json.insert(QStringLiteral("plugins"), plugins);
        QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Truncate));
        registry.write(QJsonDocument(json).toJson());
        registry.close();

        PluginManager reread(folders());
        const QVariantList rows = only(reread).value(QStringLiteral("folders")).toList();
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.first().toMap();
        QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("fonts"));
        QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Font folder"));
        QCOMPARE(row.value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(fonts));
        QVERIFY(!row.value(QStringLiteral("writable")).toBool());

        QSignalSpy forgotten(&reread, &PluginManager::folderForgotten);
        QVERIFY(!reread.forgetFolder(QStringLiteral("com.example.hello"), QStringLiteral("home")).isEmpty());
        QCOMPARE(forgotten.count(), 0);
        QCOMPARE(reread.forgetFolder(QStringLiteral("com.example.hello"), QStringLiteral("fonts")), QString());
        QCOMPARE(forgotten.count(), 1);
        QCOMPARE(forgotten.first().at(0).toString(), QStringLiteral("com.example.hello"));
        QVERIFY(only(reread).value(QStringLiteral("folders")).toList().isEmpty());
        QVERIFY(only(PluginManager(folders())).value(QStringLiteral("folders")).toList().isEmpty());
    }

    void anUpdateFromFileTakesOnlyTheSamePlugin()
    {
        PluginManager manager(folders());
        QVERIFY(manager.review(fixture("hello-1.0.0.hikariplugin")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(manager.install(false), QString());
        const QVariantMap other = manager.reviewUpdate(fixture("hello-1.1.0.hikariplugin"), QStringLiteral("com.example.other"));
        QVERIFY(!other.value(QStringLiteral("ok")).toBool());
        QVERIFY(other.value(QStringLiteral("message")).toString().contains(QStringLiteral("com.example.other")));
        QVERIFY(!manager.install(false).isEmpty());
        const QVariantMap same = manager.reviewUpdate(fixture("hello-1.1.0.hikariplugin"), QStringLiteral("com.example.hello"));
        QCOMPARE(same.value(QStringLiteral("change")).toString(), QStringLiteral("update"));
    }

    void anUnreadablePluginsFileIsReported()
    {
        QVERIFY(touch(folders().registry));
        PluginManager manager(folders());
        QVERIFY(manager.problem().contains(QStringLiteral("plugins.json")));
        QVERIFY(refusal(manager, "hello-1.0.0.hikariplugin").contains(QStringLiteral("plugins.json")));
    }

    void withoutAUserFolderNothingInstalls()
    {
        PluginManager manager({});
        QVERIFY(!manager.available());
        QVERIFY(!refusal(manager, "hello-1.0.0.hikariplugin").isEmpty());
    }
};

QTEST_GUILESS_MAIN(PluginManagerTest)
#include "plugin_manager_tests.moc"
