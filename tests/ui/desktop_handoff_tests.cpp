#include "hikari/app/application.h"
#include "hikari/backends/desktop_handoff.h"
#include "desktop_guard.h"
#include "docking.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
namespace recorded = hikari::test::desktop;

namespace {

constexpr char kChild[] = "HIKARI_DESKTOP_GUARD_CHILD";

QString writeAss(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n"
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,a\n");
    return path;
}

}

class DesktopHandoffTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    QTemporaryDir dir;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;

    void start(const QString &userFolder = {})
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.userFolder = userFolder;
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = new app::Application(o);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    QObject *named(const char *name) const
    {
        auto *root = engine->rootObjects().first();
        if (auto *found = root->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    void triggerAction(const char *item)
    {
        QObject *menuItem = named(item);
        QVERIFY2(menuItem, item);
        auto *action = menuItem->property("action").value<QObject *>();
        QVERIFY2(action, item);
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(home.isValid() && dir.isValid());
    }
    void init()
    {
        recorded::clear();
    }
    void cleanup()
    {
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        recorded::clear();
    }

    void theRecordingHandlersTakeFileHttpAndHttps()
    {
        QCOMPARE(recorded::recordedSchemes(),
                 (QStringList{QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")}));
        const QList<QUrl> urls{QUrl::fromLocalFile(dir.path()), QUrl(QStringLiteral("http://example.invalid/a")),
                               QUrl(QStringLiteral("https://example.invalid/b"))};
        for (const QUrl &url : urls)
            QVERIFY(QDesktopServices::openUrl(url));
        QCOMPARE(recorded::opened(), urls);
        QVERIFY(recorded::refused().isEmpty());
    }

    void qmlOpensThroughTheRecordingHandlers()
    {
        QQmlEngine qml;
        QQmlComponent component(&qml);
        component.setData("import QtQml\n"
                          "QtObject { Component.onCompleted: Qt.openUrlExternally(\"https://example.invalid/qml\") }\n",
                          QUrl());
        const std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(recorded::opened(), QList<QUrl>{QUrl(QStringLiteral("https://example.invalid/qml"))});
    }

    void theApplicationsHandOffsAreRecorded()
    {
        const QUrl page(QStringLiteral("https://example.invalid/c"));
        QVERIFY(backends::desktop::openUrl(page));
        QCOMPARE(recorded::opened(), QList<QUrl>{page});
        const QString file = QDir::toNativeSeparators(dir.filePath(QStringLiteral("y.ass")));
        backends::desktop::selectInFolder(file);
        QCOMPARE(recorded::revealed(), QStringList{file});
        const QStringList arguments{QStringLiteral("script.lua")};
        QVERIFY(backends::desktop::startDetached(QStringLiteral("editor"), arguments));
        recorded::setStartResult(false);
        QVERIFY(!backends::desktop::startDetached(QStringLiteral("editor"), arguments));
        const recorded::Started start{QStringLiteral("editor"), arguments};
        QCOMPARE(recorded::started(), (QList<recorded::Started>{start, start}));
        QVERIFY(recorded::refused().isEmpty());
    }

    void anotherSchemeIsRefusedBeforeTheDesktop()
    {
        QList<QUrl> reported;
        const auto previous = recorded::replaceRefusalReport([&](const QUrl &url) { reported << url; });
        const auto restore = qScopeGuard([&] { recorded::replaceRefusalReport(previous); });
        const QUrl mail(QStringLiteral("mailto:someone@example.invalid"));
        const QUrl custom(QStringLiteral("hikari-guard-probe:reported"));
        QVERIFY(!backends::desktop::openUrl(mail));
        QVERIFY(!backends::desktop::openUrl(custom));
        QCOMPARE(reported, (QList<QUrl>{mail, custom}));
        QCOMPARE(recorded::refused(), (QList<QUrl>{mail, custom}));
        QVERIFY(recorded::opened().isEmpty());
    }

    // The child logs to a file: on Windows its standard output does not arrive.
    void aRefusalFailsTheRun()
    {
        const QString log = dir.filePath(QStringLiteral("child-log.txt"));
        QProcess child;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QString::fromLatin1(kChild), QStringLiteral("1"));
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("refuseInTheChild"), QStringLiteral("-o"), log + QStringLiteral(",txt")});
        QVERIFY2(child.waitForFinished(60'000), qPrintable(child.errorString()));
        const QString err = QString::fromLocal8Bit(child.readAllStandardError());
        qInfo().noquote() << "the child's standard error:" << err.trimmed();
        QFile file(log);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(log));
        const QString out = QString::fromLocal8Bit(file.readAll());
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(out.contains(QStringLiteral("PASS   : DesktopHandoffTests::refuseInTheChild()")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("hikari-guard-probe:child was not opened")), qPrintable(out));
        QCOMPARE(child.exitCode(), recorded::kUnhandledExitCode);
    }
    void refuseInTheChild()
    {
        if (!qEnvironmentVariableIsSet(kChild))
            QSKIP("runs in aRefusalFailsTheRun's child");
        QVERIFY(!backends::desktop::openUrl(QUrl(QStringLiteral("hikari-guard-probe:child"))));
    }

    void theShellsHandOffsAreRecorded()
    {
        const QString file = writeAss(dir.filePath(QStringLiteral("tabs/a.ass")));
        QVERIFY(!file.isEmpty());
        start();
        triggerAction("userGuideMenuItem");
        QCOMPARE(recorded::opened().size(), 1);
        QVERIFY2(recorded::opened().first().toString().startsWith(QStringLiteral("https://hikarisub.altqx.com/en/docs")),
                 qPrintable(recorded::opened().first().toString()));
        triggerAction("reportIssueMenuItem");
        QCOMPARE(recorded::opened().size(), 1);
        auto *report = qobject_cast<QQuickWindow *>(named("reportIssue"));
        QVERIFY(report);
        QTRY_VERIFY(report->isVisible());
        named("reportIssueTitle")->setProperty("text", QStringLiteral("Handles jump"));
        named("reportIssueFirst")->setProperty("text", QStringLiteral("They jump."));
        QVERIFY(QMetaObject::invokeMethod(named("reportIssueOpen"), "click"));
        QCOMPARE(recorded::opened().size(), 2);
        const QString issue = recorded::opened().last().toString(QUrl::FullyEncoded);
        QVERIFY2(issue.startsWith(QStringLiteral("https://github.com/altqx/hikari/issues/new?template=bug.yml&title=Handles%20jump")),
                 qPrintable(issue));
        QVERIFY(QMetaObject::invokeMethod(named("reportIssueDone"), "click"));
        QTRY_VERIFY(!report->isVisible());
        QVERIFY(application->openFile(file));
        auto *bar = named("documentTabBar");
        QVERIFY(bar);
        QMetaObject::invokeMethod(bar, "openTabMenu", Q_ARG(QVariant, 0), Q_ARG(QVariant, QVariant::fromValue(bar)),
                                  Q_ARG(QVariant, 10), Q_ARG(QVariant, 5));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QObject *folder = named("tabMenuFolder_subtitles");
        QVERIFY(folder);
        QVERIFY(QMetaObject::invokeMethod(folder, "triggered"));
        QCOMPARE(recorded::revealed(), QStringList{QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())});
        if (auto *menu = named("documentTabMenu"))
            QMetaObject::invokeMethod(menu, "close");
        QCOMPARE(recorded::opened().size(), 2);
        QVERIFY(recorded::started().isEmpty());
        QVERIFY(recorded::refused().isEmpty());
    }

    void openUserFolderOpensItInTheFileManager()
    {
        const QString folder = dir.filePath(QStringLiteral("user folder"));
        start(folder);
        triggerAction("openUserFolderMenuItem");
        QCOMPARE(recorded::opened(), QList<QUrl>{QUrl::fromLocalFile(folder)});
        QVERIFY(QFileInfo(folder).isDir());
        QCOMPARE(application->userFolder(), QDir::toNativeSeparators(folder));
        auto *root = engine->rootObjects().first();
        QVariant settings;
        QVERIFY(QMetaObject::invokeMethod(root, "settingsWindow", Q_RETURN_ARG(QVariant, settings)));
        QObject *path = named("settingsUserFolderPath");
        QVERIFY(path);
        QCOMPARE(path->property("text").toString(), QDir::toNativeSeparators(folder));
        QObject *open = named("settingsOpenUserFolder");
        QVERIFY(open);
        QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
        QCOMPARE(recorded::opened(), (QList<QUrl>{QUrl::fromLocalFile(folder), QUrl::fromLocalFile(folder)}));
        if (auto *window = settings.value<QObject *>())
            QMetaObject::invokeMethod(window, "close");
        cleanup();
        start();
        QVERIFY(application->userFolder().isEmpty());
        triggerAction("openUserFolderMenuItem");
        QVERIFY(recorded::opened().isEmpty());
        QVERIFY(recorded::refused().isEmpty());
    }
};

QTEST_MAIN(DesktopHandoffTests)
#include "desktop_handoff_tests.moc"
