#include "hikari/app/composition.h"
#include "docking.h"
#include "theme.h"

#include "hikari/app/application.h"
#include "hikari/app/docs_export.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/single_instance.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/update_apply.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/update_check.h"
#include "hikari/backends/legacy_spelling.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
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

void preferPulseAudioBackend()
{
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsSet("QT_AUDIO_BACKEND"))
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
#endif
}

namespace {

#ifdef HIKARI_UPDATE_TEST_FEED
QString originOf(const QUrl &url)
{
    return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
}
#endif

// ADR 0019: "Installing update..." while pkexec's helper runs, after the
// main window has closed; the returned function hides it. PU7 (#309): it
// names the version and reads the settings' title bar choice.
std::function<void()> showInstalling(QQmlApplicationEngine &engine, QObject *settings, const QString &version)
{
    QQmlComponent component(&engine);
    component.loadFromModule("Hikari.Ui", "UpdateInstallingWindow");
    QObject *window = component.createWithInitialProperties(
        {{QStringLiteral("version"), version}, {QStringLiteral("settings"), QVariant::fromValue(settings)}});
    if (!window)
        return {};
    // The notice cannot end the wait: closing it is refused, and no window
    // closing quits the application meanwhile.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    return [window] { delete window; };
}

// WS5: where the files a quit left wait for the next start (saveHandedOn):
// the session's own, as its instance is (another graphical session of the
// user does not take them). None for an elevated HikariSub on Windows:
// the user's unelevated programs could put a script there for it to run
// (its files go on the next start's command line instead).
QString handedOnPath()
{
    const QString session = SingleInstance::forSession().server;
    if (session.isEmpty() || !SingleInstance::trustsUserFiles())
        return {};
    const QString tag =
        QString::fromLatin1(QCryptographicHash::hash(session.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/HandedOn-") + tag +
           QStringLiteral(".txt");
}

// WS5: the files a quit left (saved), or a start without files that came
// during the quit, are taken up by the next start that opens a window: the
// new version an update execs (deb, rpm) or starts (Restart now; an
// AppImage's new image), the start an update's setup makes
// once it is done when asked (Windows), else a start of this program made
// for them (for an AppImage the image, whose mount goes with this process).
// `files` are those that could not be saved: they go on the command line,
// of the deb and rpm exec (exitAction.arguments) or of a start made for
// them, which hands them to an AppImage's new image, running by now. On
// Windows the update then waits (runWindow): the start its setup makes
// cannot take them.
void startForHandedOn(const UpdateInstaller::ExitAction &exitAction, bool updateStarts, const QStringList &files)
{
    if (!exitAction.program.isEmpty())
        return;
#ifdef _WIN32
    if (updateStarts)
        return;
    if (update_apply::setupRunning()) {
        update_apply::askStartAfterSetup();
        return;
    }
#else
    if (updateStarts && files.isEmpty())
        return;
#endif
    QString program = qEnvironmentVariable("APPIMAGE");
    if (program.isEmpty())
        program = QCoreApplication::applicationFilePath();
    QProcess::startDetached(program, files);
}

int runWindow(int argc, char **argv, StartupMode mode, const application::StartupArguments &startup,
              UpdateInstaller::ExitAction &exitAction)
{
    preferPulseAudioBackend(); // before Qt Multimedia picks its audio backend
    // WS5: the launcher's Wayland activation token, before Qt takes it.
    const QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
    QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));
    // The start's files, absolute: its arguments without Qt's options and
    // the program's own switches.
    // WS5: and the files a quit left for the next start, which this is.
    QStringList files = launchFiles(QCoreApplication::arguments().mid(1), QDir::currentPath());
    if (const QString kept = mode == StartupMode::Run ? handedOnPath() : QString(); !kept.isEmpty())
        files += takeHandedOn(kept);
#ifndef _WIN32
    // ADR 0019: an AppImage update that an ordinary quit left installs now,
    // before anything of this version starts, with the new image watched as
    // after Restart now; this start's files go to whichever version runs.
    if (mode == StartupMode::Run) {
        UpdateInstaller early(UpdateEnvironment::current(), QStringLiteral(HIKARI_VERSION));
        if (early.installAppImageAtStart(files))
            return 0;
    }
#endif
    // WS5: one instance per user session. A later start hands its files to
    // it and leaves; a new version's first start after an update must show
    // its own window to the old one's watchdog, so it never hands over.
    SingleInstance instance(mode == StartupMode::Run ? SingleInstance::forSession() : SingleInstance::Location{});
    const SingleInstance::Role role = instance.claim({files, activationToken}, startup.healthFile.empty());
    if (role == SingleInstance::Role::Forwarded)
        return 0;
    // A start that runs alone (an update's watched start, or one whose
    // holder did not answer) becomes the instance once the lock is free.
    QTimer reclaim;
    if (role == SingleInstance::Role::Alone && mode == StartupMode::Run) {
        QObject::connect(&reclaim, &QTimer::timeout, &instance, [&reclaim, &instance] {
            if (instance.reclaim())
                reclaim.stop();
        });
        reclaim.start(1000);
    }

    Application::Options options;
    options.autoload = true; // legacy: the Autoload scripts load at start
    options.gettingStarted = true; // FR1: the first start opens Getting started
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
    application.setStartedWithPaths(!files.isEmpty()); // P6: no session at start then
    ForwardedLaunches forwarded(application);
    QObject::connect(&instance, &SingleInstance::requested, &forwarded, &ForwardedLaunches::take);
    // A path on the command line opens as the editing target. V5: a video
    // opens as legacy OpenFiles opens one file (hikarisubApp.cpp:483-487),
    // fullscreen with video.fullScreenOnStart, once the window is up.
    // Several open together once the window is up, as legacy OnInit's
    // OpenFiles(paths) and as files handed over (the files a quit left go
    // to the next start this way), and so does one that is neither
    // subtitles nor a video: a script goes to Automation, keyframes to the
    // tab, a refused file is refused (OpenFiles sorted them so).
    const auto startKind = files.size() == 1 ? application::openKindOf(files.front().toStdString(), true)
                                             : application::OpenKind::Refused;
    const QString startPath =
        startKind == application::OpenKind::Subtitles || startKind == application::OpenKind::Video ? files.front()
                                                                                                   : QString();
    const bool startVideo = !startPath.isEmpty() && openStartFile(application, startPath);
    if (startPath.isEmpty() && !files.isEmpty())
        forwarded.take({files, {}});

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
    forwarded.setWindow(qobject_cast<QWindow *>(engine.rootObjects().first()));
    if (mode == StartupMode::ExitAfterWindowCreated) {
        QTimer::singleShot(0, &app, [] { QCoreApplication::exit(0); });
        return app.exec();
    }
    // legacy CallAfter(CheckOnStartup): once the window is up; first what an
    // earlier update left (its record, its failure).
    QTimer::singleShot(0, &application, [&application] {
        application.updates().startUp();
        application.updates().checkOnStartup();
        application.whatsNew().start(); // SH3: after the check, whose list it shares
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
    // WS5: the next start is the instance, an update's new version included.
    reclaim.stop();
    instance.release();
    // WS5: what was handed over and not opened (the quit came first) is
    // saved before an update can start a new version.
    const QStringList unopened = forwarded.takeUnopened();
    const QString kept = handedOnPath();
    const bool saved = unopened.isEmpty() || (!kept.isEmpty() && saveHandedOn(kept, unopened));
    const QStringList carried = saved ? QStringList() : unopened;
    // ADR 0019: a ready update installs after a normal quit, every document
    // reviewed and the main window closed.
#ifdef _WIN32
    // WS5: the start an update's setup makes cannot take files, so with
    // files that could not be kept (an elevated HikariSub keeps none) the
    // update waits for the next quit and this version starts for them.
    const bool install = code == 0 && carried.isEmpty();
#else
    const bool install = code == 0;
#endif
    const bool updateStarts = install && application.updates().installer().restartRequested();
    // WS5: whichever start comes after the quit (this program's, an
    // update's exec or new image) inherits the activation token of the
    // start that came during it, to come forward on Wayland.
    if (const QString token = forwarded.activationTokenAfterQuit();
        !token.isEmpty() && (!unopened.isEmpty() || forwarded.startAfterQuit()))
        qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
    if (install)
        exitAction = application.updates().installer().installAtExit(
            [&engine, &application] { return showInstalling(engine, application.settingsStore(), application.updates().installer().readyVersion());
            });
    exitAction.arguments += carried;
    if (!unopened.isEmpty() || forwarded.startAfterQuit())
        startForHandedOn(exitAction, updateStarts, carried);
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
    if (startup.exportDocs) {
        // DX1 (#367): the docs export, on the offscreen platform whatever
        // the desktop: no window is shown and no display is needed.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
        QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));
        if (startup.exportDocs->empty()) {
            std::fprintf(stderr, "--export-docs needs a folder\n");
            return 2;
        }
        QString error;
        if (!exportDocs(QString::fromLocal8Bit(*startup.exportDocs), &error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
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
