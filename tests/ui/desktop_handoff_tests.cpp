// 38e: no test reaches the desktop's programs (tests/support/desktop/
// desktop_guard.h). The recording handlers take file, http and https URLs
// from QDesktopServices and from QML; the application's hand-offs
// (backends::desktop) are recorded; a URL of any other scheme is refused
// before the desktop's handler and fails the run. The shell's own hand-offs
// (Help > User guide, Help > Report an issue, the tab menu's folder
// items) go through the composition and the real QML.

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

// The child run of aRefusalFailsTheRun.
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

} // namespace

class DesktopHandoffTests : public QObject {
    Q_OBJECT

    QTemporaryDir home; // the settings folder
    QTemporaryDir dir;  // subtitles
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;

    void start()
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        o.spellingBackend = {}; // no spell checker, so no notice over the window
        application = new app::Application(o); // revealInFolder unset: backends::desktop
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
    // A menu item's Action, triggered as a click on the item triggers it.
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

    // Every executable starts with the handlers in place: QDesktopServices
    // hands them its file, http and https URLs.
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

    // QML's Qt.openUrlExternally is QDesktopServices::openUrl.
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

    // backends::desktop's routes: a URL through the recording handlers, a
    // file to show in its folder and a detached program recorded, with the
    // start's answer the test sets.
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

    // A scheme no handler records never reaches QDesktopServices: refused
    // and reported.
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

    // The guard: a run in which a URL was refused fails. The executable runs
    // again with only refuseInTheChild, which passes; the run still exits
    // with kUnhandledExitCode. The child's QtTest log goes to a file (on
    // Windows its standard output does not arrive; HikariTesting.cmake).
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
        // The refusal names the URL as it happens.
        QVERIFY2(out.contains(QStringLiteral("hikari-guard-probe:child was not opened")), qPrintable(out));
        QCOMPARE(child.exitCode(), recorded::kUnhandledExitCode);
    }
    void refuseInTheChild()
    {
        if (!qEnvironmentVariableIsSet(kChild))
            QSKIP("runs in aRefusalFailsTheRun's child");
        QVERIFY(!backends::desktop::openUrl(QUrl(QStringLiteral("hikari-guard-probe:child"))));
    }

    // The shell: Help > User guide (GLOBAL_HELP, DX1: a page of the user
    // guide, user_guide_tests) and Help > Report an issue (GLOBAL_ANSI) open
    // their pages in the browser (HikariSubFrame.cpp:360-362); the tab menu's
    // folder item shows the subtitles in their folder (SelectInFolder). All
    // recorded.
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
        QCOMPARE(recorded::opened().size(), 2);
        QCOMPARE(recorded::opened().last(), QUrl(QStringLiteral("https://github.com/altqx/hikari/issues")));
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
};

QTEST_MAIN(DesktopHandoffTests)
#include "desktop_handoff_tests.moc"
