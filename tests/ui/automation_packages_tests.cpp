#include "hikari/app/application.h"

#include "fake_feed_server.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <map>
#include <set>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QByteArray kHello = "script_name = \"Hello\"\n"
                          "aegisub.register_macro(\"Hello\", \"\", function() end)\n";

QVariantMap entry(const app::ScriptPackages &packages, const QString &nameSpace)
{
    for (const QVariant &e : packages.entries())
        if (e.toMap().value(QStringLiteral("nameSpace")).toString() == nameSpace)
            return e.toMap();
    return {};
}

QString stateOf(const app::ScriptPackages &packages, const QString &nameSpace)
{
    return entry(packages, nameSpace).value(QStringLiteral("state")).toString();
}

}

class AutomationPackagesTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    testing::FakeFeedServer server;
    std::unique_ptr<app::Application> application;

    app::AutomationShell &automation() { return application->automation(); }
    QString user() const { return dir.filePath(QStringLiteral("user")); }
    QString autoload(const QString &file) const { return user() + QStringLiteral("/automation/autoload/") + file; }
    static bool samePath(const QString &a, const QString &b) { return QFileInfo(a) == QFileInfo(b); }
    bool loaded(const QString &path)
    {
        return std::ranges::any_of(automation().scripts(), [&](const auto &s) {
            return samePath(QString::fromStdString(s.path), path) && s.state == application::ScriptStatus::State::Ready;
        });
    }
    bool settles(const app::ScriptPackages &packages, const QString &nameSpace, const QString &state)
    {
        QDeadlineTimer deadline(60'000);
        while ((automation().running() || stateOf(packages, nameSpace) != state) && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return stateOf(packages, nameSpace) == state;
    }
    QString runState(int id)
    {
        const auto *run = automation().runs()->run(id);
        return run ? run->state : QString();
    }
    QString runMessage(int id)
    {
        const auto *run = automation().runs()->run(id);
        return run ? run->message : QString();
    }

    void publishHello() { server.publishMacro(QStringLiteral("test.Hello"), QStringLiteral("Hello"), kHello); }

    app::RecommendedScript script(const char *nameSpace, const char *name) const
    {
        const QString feed = server.url(QStringLiteral("/feed.json"));
        return {nameSpace, std::string(nameSpace) + ".lua", name, "Editing", "Says hello.", "Says hello.", "test",
                "MIT", "", "https://example.invalid/hello", feed.toStdString()};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        QVERIFY(server.listening());
        QVERIFY(QDir().mkpath(user() + QStringLiteral("/automation/autoload")));
        QVERIFY(QDir().mkpath(user() + QStringLiteral("/config")));
        QFile config(user() + QStringLiteral("/config/l0.DependencyControl.json"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(server.trustingConfig());
        config.close();
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.automationDir = QStringLiteral(HIKARI_AUTOMATION_ROOT);
        o.userAutomationDir = user();
        application = std::make_unique<app::Application>(o);
    }
    void cleanupTestCase() { application.reset(); }

    void theListIsInTheThirdPartyCorpus()
    {
        QFile file(QStringLiteral(HIKARI_THIRDPARTY_RUNS));
        QVERIFY(file.open(QIODevice::ReadOnly));
        std::set<QString> corpus;
        for (const QJsonValue &run : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("runs")).toArray())
            corpus.insert(run.toObject().value(QStringLiteral("script")).toString());
        QVERIFY(!corpus.empty());
        const auto &groups = app::recommendedScriptGroups();
        std::set<std::string> namespaces;
        std::pair<std::ptrdiff_t, std::string> previous{-1, {}};
        for (const auto &s : app::recommendedScripts()) {
            const QString fileName = QString::fromStdString(s.fileName);
            QVERIFY2(corpus.contains(fileName), qPrintable(fileName));
            QVERIFY2(fileName.startsWith(QString::fromStdString(s.nameSpace) + QLatin1Char('.')), qPrintable(fileName));
            QVERIFY(namespaces.insert(s.nameSpace).second);
            QVERIFY(!s.name.empty() && !s.line.empty() && !s.about.empty() && !s.author.empty() && !s.licence.empty());
            QVERIFY(s.project.starts_with("https://") && s.feed.starts_with("https://"));
            const auto group = std::ranges::find(groups, s.group);
            QVERIFY2(group != groups.end(), s.group.c_str());
            const std::pair<std::ptrdiff_t, std::string> at{group - groups.begin(), s.name};
            QVERIFY2(previous < at, s.name.c_str());
            previous = at;
        }
    }

    void installsLoadsAndUninstallsThroughDependencyControl()
    {
        publishHello();
        app::ScriptPackages packages(automation(), {script("test.Hello", "Hello")});
        QSignalSpy installed(&packages, &app::ScriptPackages::installed);
        QCOMPARE(stateOf(packages, QStringLiteral("test.Hello")), QStringLiteral("install"));

        packages.install(QStringLiteral("test.Hello"));
        QCOMPARE(stateOf(packages, QStringLiteral("test.Hello")), QStringLiteral("installing"));
        QVERIFY(automation().running());
        QVERIFY(settles(packages, QStringLiteral("test.Hello"), QStringLiteral("installed")));
        const int installRun = automation().runId();
        QCOMPARE(runMessage(installRun), QStringLiteral("Hello is installed."));
        QCOMPARE(runState(installRun), QStringLiteral("applied"));
        QCOMPARE(installed.count(), 1);
        QCOMPARE(installed.first().value(1).toString(), QStringLiteral("Hello"));
        const QString path = QDir::toNativeSeparators(autoload(QStringLiteral("test.Hello.lua")));
        QVERIFY(QFile::exists(path));
        QTRY_VERIFY_WITH_TIMEOUT(loaded(path), 30'000);
        QVERIFY(samePath(entry(packages, QStringLiteral("test.Hello")).value(QStringLiteral("path")).toString(), path));
        QVERIFY(app::dependencyControlScripts(user() + QStringLiteral("/config/l0.DependencyControl.json"))
                    .contains(QStringLiteral("test.Hello")));
        QVERIFY(QDir(user() + QStringLiteral("/temp")).entryList({QStringLiteral("hikari-*")}, QDir::Files).isEmpty());

        packages.uninstall(QStringLiteral("test.Hello"));
        QCOMPARE(stateOf(packages, QStringLiteral("test.Hello")), QStringLiteral("uninstalling"));
        QVERIFY(settles(packages, QStringLiteral("test.Hello"), QStringLiteral("install")));
        QCOMPARE(runMessage(automation().runId()), QStringLiteral("Hello is uninstalled."));
        QVERIFY(!QFile::exists(path));
        QVERIFY(std::ranges::none_of(automation().scripts(),
                                     [&](const auto &s) { return samePath(QString::fromStdString(s.path), path); }));
        QCOMPARE(installed.count(), 1);
    }

    void aFailedInstallKeepsItsReasonAndRun()
    {
        server.files.erase(QStringLiteral("/test.Hello.lua"));
        app::ScriptPackages packages(automation(), {script("test.Hello", "Hello")});
        packages.install(QStringLiteral("test.Hello"));
        QVERIFY(settles(packages, QStringLiteral("test.Hello"), QStringLiteral("failed")));
        const QVariantMap failed = entry(packages, QStringLiteral("test.Hello"));
        QCOMPARE(failed.value(QStringLiteral("error")).toString(), QStringLiteral("A file couldn’t be downloaded."));
        QCOMPARE(failed.value(QStringLiteral("runId")).toInt(), automation().runId());
        QCOMPARE(runState(automation().runId()), QStringLiteral("failed"));
        QVERIFY(!QFile::exists(autoload(QStringLiteral("test.Hello.lua"))));
        publishHello();
    }

    void aCopyPutThereByHandIsInstalledButNotUninstalled()
    {
        QFile copy(autoload(QStringLiteral("test.Manual.lua")));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(kHello);
        copy.close();
        app::ScriptPackages packages(automation(), {script("test.Manual", "Manual")});
        QCOMPARE(stateOf(packages, QStringLiteral("test.Manual")), QStringLiteral("manual"));
        packages.install(QStringLiteral("test.Manual"));
        packages.uninstall(QStringLiteral("test.Manual"));
        QCOMPARE(stateOf(packages, QStringLiteral("test.Manual")), QStringLiteral("manual"));
        QVERIFY(!automation().running());
        QVERIFY(QFile::remove(copy.fileName()));
        packages.refresh();
        QCOMPARE(stateOf(packages, QStringLiteral("test.Manual")), QStringLiteral("install"));
    }

    void installsWaitForARunningInstall()
    {
        app::ScriptPackages packages(automation(), {script("test.Hello", "Hello"), script("test.Other", "Other")});
        packages.install(QStringLiteral("test.Hello"));
        packages.install(QStringLiteral("test.Other"));
        QCOMPARE(stateOf(packages, QStringLiteral("test.Hello")), QStringLiteral("installing"));
        QCOMPARE(stateOf(packages, QStringLiteral("test.Other")), QStringLiteral("queued"));
        QVERIFY(settles(packages, QStringLiteral("test.Hello"), QStringLiteral("installed")));
        QVERIFY(settles(packages, QStringLiteral("test.Other"), QStringLiteral("failed")));
        packages.uninstall(QStringLiteral("test.Hello"));
        QVERIFY(settles(packages, QStringLiteral("test.Hello"), QStringLiteral("install")));
    }
};

QTEST_MAIN(AutomationPackagesTests)
#include "automation_packages_tests.moc"
