#include "hikari/app/composition.h"
#include "docking.h"
#include "theme.h"

#include "hikari/app/application.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/update_apply.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/update_check.h"
#include "hikari/backends/legacy_spelling.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// The UI module is a static QML module; its plugin must be imported explicitly.
Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace hikari::app {

bool openStartFile(Application &application, const QString &path)
{
    if (application::openKindOf(path.toStdString(), true) == application::OpenKind::Video)
        return true;
    application.openFile(path);
    return false;
}

void openStartVideo(QObject *mainWindow, const QString &path)
{
    QMetaObject::invokeMethod(mainWindow, "openSingleVideo", Q_ARG(QVariant, path));
}

namespace {

#ifdef HIKARI_UPDATE_TEST_FEED
QString originOf(const QUrl &url)
{
    return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
}
#endif

// ADR 0019: "Installing update..." while pkexec's helper runs, after the
// main window has closed; the returned function hides it.
std::function<void()> showInstalling(QQmlApplicationEngine &engine)
{
    QQmlComponent component(&engine);
    component.loadFromModule("Hikari.Ui", "UpdateInstallingWindow");
    QObject *window = component.create();
    if (!window)
        return {};
    // The notice cannot end the wait: closing it is refused, and no window
    // closing quits the application meanwhile.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    return [window] { delete window; };
}

int runWindow(int argc, char **argv, StartupMode mode, const application::StartupArguments &startup,
              UpdateInstaller::ExitAction &exitAction)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
    QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));
#ifndef _WIN32
    // ADR 0019: an AppImage update that an ordinary quit left installs now,
    // before anything of this version starts, with the new image watched as
    // after Restart now; this start's files go to whichever version runs.
    if (mode == StartupMode::Run) {
        QStringList files;
        for (const auto &argument : startup.rest) {
            const QString path = QString::fromLocal8Bit(argument);
            files << (QFileInfo::exists(path) ? QFileInfo(path).absoluteFilePath() : path);
        }
        UpdateInstaller early(UpdateEnvironment::current(), QStringLiteral(HIKARI_VERSION));
        if (early.installAppImageAtStart(files))
            return 0;
    }
#endif

    Application::Options options;
    options.autoload = true; // legacy: the Autoload scripts load at start
    // The settings registry (O1).
    options.settingsFile =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/hikari.ini");
    options.recoveryDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Recovery");
    // F3 / R2-hunspell: Hunspell (the Options default) over the user's
    // Dictionary folder beside hikari.ini (UserDic.udic, added dictionaries),
    // then the bundled one beside the executable (legacy's location).
    options.spellingBackend = backends::hunspellSpellingLoader();
    options.bundledDictionaryDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
    // O5: the first start on a Polish system takes Polish (Application).
    options.systemUiLanguages = QLocale::system().uiLanguages();
    // ADR 0019: the install's package kind, the update cache and installers.
    options.updateEnvironment = UpdateEnvironment::current();
    // Installers spec 1.7: an installed copy cannot write its program folder,
    // so what scripts write, and the user's own autoload and include
    // scripts, are in the data folder's Automation, laid out as the shipped
    // one. A portable copy keeps them beside the program.
    if (options.updateEnvironment.package.kind != application::PackageKind::Portable)
        options.userAutomationDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Automation");
#ifdef HIKARI_UPDATE_TEST_FEED
    // Test builds only: a local release feed (tools/update-feed).
    if (const QString feed = qEnvironmentVariable("HIKARISUB_UPDATE_FEED"); !feed.isEmpty()) {
        options.updateFeed = UpdateFeed::local(QUrl(feed));
        const QString feedOrigin = originOf(QUrl(feed));
        options.updateEnvironment.allowRedirect = [feedOrigin](const QUrl &to) { return originOf(to) == feedOrigin; };
    }
#endif
    Application application(options);
    application.setStartedWithPaths(!startup.rest.empty()); // P6: no session at start then
    // A path on the command line opens as the editing target. V5: a video
    // opens as legacy OpenFiles opens one file (hikarisubApp.cpp:483-487),
    // fullscreen with video.fullScreenOnStart, once the window is up.
    const QString startPath = startup.rest.empty() ? QString() : QString::fromLocal8Bit(startup.rest.front());
    const bool startVideo = !startPath.isEmpty() && openStartFile(application, startPath);

    // K2: the controls draw with the theme layer's palette.
    hikari::ui::theme::chooseControlsStyle();
    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    attachStylePreview(engine, application.styleManager());
    attachFontCatalogPreview(engine, application.fontCatalogs()); // Y6
    engine.setInitialProperties(application.qmlProperties());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    if (startVideo)
        openStartVideo(engine.rootObjects().first(), startPath);
    if (mode == StartupMode::ExitAfterWindowCreated) {
        QTimer::singleShot(0, &app, [] { QCoreApplication::exit(0); });
        return app.exec();
    }
    // legacy CallAfter(CheckOnStartup): once the window is up; first what an
    // earlier update left (its record, its failure).
    QTimer::singleShot(0, &application, [&application] {
        application.updates().startUp();
        application.updates().checkOnStartup();
    });
    // An AppImage's new version reports its window to the old one's watchdog.
    if (!startup.healthFile.empty())
        application.updates().installer().expectHealthReport();
    if (!startup.healthFile.empty())
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
            QObject::connect(
                window, &QQuickWindow::frameSwapped, &application,
                [&application, health = QString::fromLocal8Bit(startup.healthFile)] {
                    application.updates().installer().reportHealthy(health);
                },
                Qt::SingleShotConnection);
    const int code = app.exec();
    // ADR 0019: a ready update installs after a normal quit, every document
    // reviewed and the main window closed.
    if (code == 0)
        exitAction = application.updates().installer().installAtExit([&engine] { return showInstalling(engine); });
    return code;
}

} // namespace

int run(int argc, char **argv, StartupMode mode)
{
#ifdef _WIN32
    // ADR 0019: the installing side of an update, before anything of the
    // program starts (no settings, no window, no mutex): it checks the
    // installer against the release's SHA256SUMS and starts it.
    if (argc >= 2 && std::string_view(argv[1]) == application::kApplyUpdateSwitch) {
        QCoreApplication core(argc, argv);
        std::string feed;
#ifdef HIKARI_UPDATE_TEST_FEED
        feed = qEnvironmentVariable("HIKARISUB_UPDATE_FEED").toStdString();
#endif
        return update_apply::runFromCommandLine(
            HIKARI_VERSION, currentPackageKind().kind == application::PackageKind::NsisMachine, feed);
    }
#endif
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.emplace_back(argv[i]);
    const auto startup = application::parseStartupArguments(arguments);
    if (startup.printVersion) {
        // 8.1: "<version> (<kind>)" without a display.
        QCoreApplication core(argc, argv);
        std::printf("%s (%s)\n", HIKARI_VERSION, qPrintable(packageKindToken(currentPackageKind().kind)));
        return 0;
    }
#ifdef _WIN32
    // An update's setup is replacing this installed copy: this start would
    // hold the program open and stop it, so it leaves, and the setup starts
    // HikariSub when it is done (distribution.md).
    if (update_apply::setupRunning()) {
        QCoreApplication core(argc, argv);
        const auto kind = currentPackageKind().kind;
        if (kind == application::PackageKind::NsisUser || kind == application::PackageKind::NsisMachine) {
            update_apply::askStartAfterSetup();
            return 0;
        }
    }
    // Held until exit: the installer waits while it exists. A failure is ignored.
    CreateMutexW(nullptr, FALSE, L"Global\\io.github.altqx.HikariSub.Running");
#endif
    UpdateInstaller::ExitAction exitAction;
    const int code = runWindow(argc, argv, mode, startup, exitAction);
#ifndef _WIN32
    if (!exitAction.program.isEmpty()) {
        // deb and rpm: the new version (or, after a failure, the one still
        // installed) takes this process's place.
        std::vector<QByteArray> held{QFile::encodeName(exitAction.program)};
        for (const QString &argument : exitAction.arguments)
            held.push_back(argument.toLocal8Bit());
        std::vector<char *> execArguments;
        for (QByteArray &a : held)
            execArguments.push_back(a.data());
        execArguments.push_back(nullptr);
        ::execv(execArguments.front(), execArguments.data());
    }
#endif
    return code;
}

} // namespace hikari::app
