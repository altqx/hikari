#include "hikari/app/application.h"
#include "hikari/backends/desktop_handoff.h"

#include "docking.h"
#include "fake_feed_server.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;

namespace {

const QByteArray kHello = "script_name = \"Hello\"\n"
                          "aegisub.register_macro(\"Hello\", \"\", function() end)\n";

QQuickItem *find(QQuickItem *from, const QString &name)
{
    if (from->objectName() == name)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *found = find(child, name))
            return found;
    return nullptr;
}

bool write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
}

QJsonObject record(const QString &feed, const char *version = "0.0.1")
{
    return {{QStringLiteral("recordType"), QStringLiteral("managed")},
            {QStringLiteral("feed"), feed},
            {QStringLiteral("name"), QStringLiteral("Hello")},
            {QStringLiteral("version"), QLatin1String(version)}};
}

}

class AutomationResetUiTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir;
    testing::FakeFeedServer server;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *popup = nullptr;

    QString user() const { return dir->filePath(QStringLiteral("user")); }
    QString backup() const { return user() + QStringLiteral("/automation-backup-2026-11-12"); }

    void prepare()
    {
        dir = std::make_unique<QTemporaryDir>();
        QVERIFY(dir->isValid());
        QVERIFY(QDir().mkpath(user() + QStringLiteral("/automation/autoload")));
        QVERIFY(write(user() + QStringLiteral("/config/l0.DependencyControl.json"), server.trustingConfig()));
    }
    void start(app::AutomationReset reset)
    {
        if (!dir)
            prepare();
        reset.backup = backup();
        if (reset.outcome == app::AutomationReset::Outcome::Moved)
            QVERIFY(QDir().mkpath(backup() + QStringLiteral("/automation/autoload")));
        app::Application::Options options;
        options.settingsFile = dir->filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        options.automationDir = QStringLiteral(HIKARI_AUTOMATION_ROOT);
        options.userAutomationDir = user();
        options.automationReset = std::move(reset);
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *loader = window->findChild<QObject *>(QStringLiteral("automationResetDialogLoader"));
        QVERIFY(loader);
        QTRY_VERIFY(loader->property("item").value<QObject *>());
        popup = qobject_cast<QQuickWindow *>(loader->property("item").value<QObject *>());
        QVERIFY(popup);
        QTRY_VERIFY(popup->isVisible());
    }

    QQuickItem *item(const QString &name) { return find(popup->contentItem(), name); }
    QString text(const QString &name)
    {
        QQuickItem *found = item(name);
        return found ? found->property("text").toString() : QStringLiteral("<missing %1>").arg(name);
    }
    bool shown(const QString &name)
    {
        QQuickItem *found = item(name);
        return found && found->isVisible();
    }
    void click(const QString &name)
    {
        QQuickItem *button = item(name);
        QVERIFY2(button && button->isVisible(), qPrintable(name));
        QVERIFY(QMetaObject::invokeMethod(button, "click"));
    }
    QString scriptState()
    {
        const QVariantList scripts = application->automationReset().scripts();
        return scripts.isEmpty() ? QString() : scripts.first().toMap().value(QStringLiteral("state")).toString();
    }
    bool settles(const QString &state)
    {
        QDeadlineTimer deadline(60'000);
        while ((application->automation().running() || scriptState() != state) && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return scriptState() == state;
    }

private slots:
    void cleanup()
    {
        if (application)
            application->endSession();
        engine.reset();
        application.reset();
        dir.reset();
        popup = nullptr;
    }

    void thePopupSaysWhyListsTheMoveAndReinstalls()
    {
        QVERIFY(server.listening());
        const QString feed = server.url(QStringLiteral("/feed.json"));
        server.files.clear();
        app::AutomationReset reset;
        reset.outcome = app::AutomationReset::Outcome::Moved;
        reset.moved = {QStringLiteral("automation/autoload"), QStringLiteral("config")};
        const QJsonObject macros{{QStringLiteral("test.Hello"), record(feed)},
                                 {QStringLiteral("arch.Line2Fbf"), record(feed)},
                                 {QStringLiteral("a-mo.Aegisub-Motion"), record(feed)}};
        QList<QUrl> opened;
        const backends::desktop::Routes before = backends::desktop::replaceRoutes({[&](const QUrl &url) {
            opened << url;
            return true;
        }, {}, {}});
        prepare();
        QVERIFY(write(backup() + QStringLiteral("/config/l0.DependencyControl.json"),
                      QJsonDocument(QJsonObject{{QStringLiteral("macros"), macros}}).toJson()));
        start(reset);

        QCOMPARE(popup->title(), QStringLiteral("Automation folder reset"));
        QCOMPARE(text(QStringLiteral("automationResetWhy")),
                 QStringLiteral("The karaoke templater and several scripts are now built into HikariSub. Old copies "
                                "could replace or break them, so your automation folder was reset."));
        QCOMPARE(text(QStringLiteral("automationResetMoved")), QStringLiteral("automation/autoload\nconfig"));
        QCOMPARE(text(QStringLiteral("automationResetBackup")), QDir::toNativeSeparators(backup()));
        QCOMPARE(text(QStringLiteral("automationResetBuiltIn_arch.Line2Fbf")),
                 QStringLiteral("arch.Line2Fbf: use Split lines into frames"));
        QVERIFY(!item(QStringLiteral("automationResetBuiltIn_a-mo.Aegisub-Motion")));
        QCOMPARE(text(QStringLiteral("automationResetReinstall")), QStringLiteral("Reinstall 1 script(s)"));

        click(QStringLiteral("automationResetOpenBackup"));
        QCOMPARE(opened, (QList<QUrl>{QUrl::fromLocalFile(backup())}));

        click(QStringLiteral("automationResetReinstall"));
        QTRY_VERIFY(!shown(QStringLiteral("automationResetReinstall")));
        QVERIFY(settles(QStringLiteral("failed")));
        const QString reason = application->automationReset().scripts().first().toMap().value(QStringLiteral("error")).toString();
        QVERIFY(!reason.isEmpty());
        QTRY_COMPARE(text(QStringLiteral("automationResetScriptText_test.Hello")), QStringLiteral("Hello: ") + reason);
        QVERIFY(shown(QStringLiteral("automationResetRetry_test.Hello")));
        QVERIFY(!QFileInfo::exists(user() + QStringLiteral("/automation/autoload/test.Hello.lua")));

        server.publishMacro(QStringLiteral("test.Hello"), QStringLiteral("Hello"), kHello, QStringLiteral("1.2.0"));
        click(QStringLiteral("automationResetRetry_test.Hello"));
        QVERIFY(settles(QStringLiteral("installed")));
        QTRY_COMPARE(text(QStringLiteral("automationResetScriptText_test.Hello")), QStringLiteral("Hello is installed."));
        QVERIFY(!shown(QStringLiteral("automationResetRetry_test.Hello")));
        QVERIFY(QFileInfo::exists(user() + QStringLiteral("/automation/autoload/test.Hello.lua")));
        QFile records(user() + QStringLiteral("/config/l0.DependencyControl.json"));
        QVERIFY(records.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(records.readAll())
                     .object()
                     .value(QStringLiteral("macros"))
                     .toObject()
                     .value(QStringLiteral("test.Hello"))
                     .toObject()
                     .value(QStringLiteral("version"))
                     .toString(),
                 QStringLiteral("1.2.0"));

        click(QStringLiteral("automationResetOk"));
        QTRY_VERIFY(!popup->isVisible());
        backends::desktop::replaceRoutes(before);
    }

    void withNothingToReinstallTheButtonIsHidden()
    {
        app::AutomationReset reset;
        reset.outcome = app::AutomationReset::Outcome::Moved;
        reset.moved = {QStringLiteral("automation/autoload")};
        start(reset);
        QVERIFY(!shown(QStringLiteral("automationResetReinstall")));
        QVERIFY(shown(QStringLiteral("automationResetOpenBackup")));
        QCOMPARE(application->automationReset().reinstallCount(), 0);
    }

    void aFailedResetSaysWhy()
    {
        app::AutomationReset reset;
        reset.outcome = app::AutomationReset::Outcome::Failed;
        reset.reason = QStringLiteral("Permission denied");
        start(reset);
        QCOMPARE(popup->title(), QStringLiteral("Automation folder not reset"));
        QCOMPARE(text(QStringLiteral("automationResetWhy")),
                 QStringLiteral("Your automation folder wasn’t reset: Permission denied. Nothing was moved or deleted. "
                                "HikariSub tries again at the next start."));
        QVERIFY(!shown(QStringLiteral("automationResetReinstall")));
        QVERIFY(!shown(QStringLiteral("automationResetOpenBackup")));
        QVERIFY(!shown(QStringLiteral("automationResetMoved")));
    }

    void noResetNoPopup()
    {
        app::Application::Options options;
        QTemporaryDir here;
        options.settingsFile = here.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        app::Application quiet(options);
        QQmlApplicationEngine qml;
        QVERIFY(ui::attachDocking(qml));
        qml.setInitialProperties(quiet.qmlProperties());
        qml.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(200);
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("automationResetDialogLoader"))->property("active").toBool());
        quiet.endSession();
    }
};

QTEST_MAIN(AutomationResetUiTests)
#include "automation_reset_ui_tests.moc"
