#include "hikari/app/composition.h"
#include "docking.h"
#include "theme.h"

#include "hikari/app/application.h"
#include "hikari/app/crash_record.h"
#include "hikari/app/hang_watchdog.h"
#include "hikari/app/docs_export.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/single_instance.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/app/update_apply.h"
#include "hikari/app/user_folder.h"
#include "hikari/application/recent_files.h"
#include "hikari/application/update_check.h"
#include "hikari/backends/legacy_spelling.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
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

int crashTestSink = 0;

// CR1 (#452): a forced crash or freeze, to check the reports end to end
// (docs/crash-reports.md; release CI symbolizes the record it leaves).
// HIKARISUB_CRASH_TEST=segv or abort crashes once the handler is in; hang
// freezes the window 15 s, 3 s after it is up.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crashForTest(const QByteArray &how, const QString &record)
{
    std::fprintf(stderr, "HIKARISUB_CRASH_TEST=%s: record at %s\n", how.constData(), qPrintable(record));
    std::fflush(stderr);
    if (how == "abort")
        std::abort();
    if (how == "hang") {
        QTimer::singleShot(3000, [] { QThread::msleep(15000); });
        return;
    }
    // A null pointer the compiler cannot see is one.
    *static_cast<volatile int *>(qEnvironmentVariableIsSet("HIKARISUB_CRASH_TEST_NEVER_SET") ? &crashTestSink : nullptr) = 1;
}

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

// WS5: where the files a quit left wait for the next start (saveHandedOn),
// in `folder` (UD1: the cache folder): the session's own, as its instance is
// (another graphical session of the user does not take them). None for an
// elevated HikariSub on Windows: the user's unelevated programs could put a
// script there for it to run (its files go on the next start's command line
// instead).
QString handedOnPath(const QString &folder)
{
    const QString session = SingleInstance::forSession().server;
    if (folder.isEmpty() || session.isEmpty() || !SingleInstance::trustsUserFiles())
        return {};
    const QString tag =
        QString::fromLatin1(QCryptographicHash::hash(session.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    return folder + QStringLiteral("/HandedOn-") + tag + QStringLiteral(".txt");
}

// UD1: the folder holding the bundled files and their manifest: the
// program's, or in the build tree the one the automation tree is staged in.
QString bundledFolder()
{
    const QString program = QCoreApplication::applicationDirPath();
#ifdef HIKARI_BUILD_AUTOMATION
    if (!QFileInfo::exists(program + QStringLiteral("/Automation/automation")))
        return QFileInfo(QStringLiteral(HIKARI_BUILD_AUTOMATION)).absolutePath();
#endif
    return program;
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

// AN1: app_started's package: the install's kind; a "portable" start
// without the bundled Automation folder is the build tree.
QString usagePackage(application::PackageKind kind, bool portableCopy)
{
    switch (kind) {
    case application::PackageKind::NsisUser:
    case application::PackageKind::NsisMachine:
        return QStringLiteral("installer");
    case application::PackageKind::AppImage:
        return QStringLiteral("appimage");
    case application::PackageKind::Deb:
        return QStringLiteral("deb");
    case application::PackageKind::Rpm:
        return QStringLiteral("rpm");
    case application::PackageKind::Arch:
        return QStringLiteral("aur");
    case application::PackageKind::Portable:
        break;
    }
    return portableCopy ? QStringLiteral("portable") : QStringLiteral("build_tree");
}

int runWindow(int argc, char **argv, StartupMode mode, const application::StartupArguments &startup,
              UpdateInstaller::ExitAction &exitAction)
{
    QElapsedTimer started; // AN1: startup time to the first frame
    started.start();
    preferPulseAudioBackend(); // before Qt Multimedia picks its audio backend
    // WS5: the launcher's Wayland activation token, before Qt takes it.
    const QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
    QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));
    // UD1: the user folder (user_folder.h); a portable copy keeps it beside
    // the program. A start from the build tree (no Automation folder beside
    // it) is no portable copy.
    const UpdateEnvironment updateEnvironment = UpdateEnvironment::current();
    const auto folderEnvironment = UserFolder::Environment::current(
        updateEnvironment.package.kind == application::PackageKind::Portable &&
        QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/Automation/automation")));
    const UserFolder userFolder = UserFolder::locate(folderEnvironment);
    const UserFolder::OldFolders oldFolders = UserFolder::OldFolders::of(folderEnvironment);
    // The start's files, absolute: its arguments without Qt's options and
    // the program's own switches.
    // WS5: and the files a quit left for the next start, which this is (a
    // version before UD1 left them in its data folder).
    QStringList files = launchFiles(QCoreApplication::arguments().mid(1), QDir::currentPath());
    if (mode == StartupMode::Run)
        for (const QString &folder : {userFolder.cache(), oldFolders.data})
            if (const QString kept = handedOnPath(folder); !kept.isEmpty())
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
    // UD1: the Qt folders before the user folder move in once, in the start
    // that holds the instance (not one with HIKARISUB_USER_DIR; a portable
    // copy only names them, #397); then the bundled scripts, modules and
    // dictionaries are seeded in.
    if (folderEnvironment.userDirOverride.isEmpty())
        options.userFolderMove = userFolder.moveIn(oldFolders, folderEnvironment.portable);
    options.userFolderSeed = userFolder.seed(bundledFolder());
    options.userFolder = userFolder.root();
    options.gettingStarted = true; // FR1: the first start opens Getting started
    // The settings registry (O1).
    options.settingsFile = userFolder.settingsFile();
    options.recoveryDir = userFolder.recovery();
    // F3 / R2-hunspell: Hunspell (the Options default) over the user's
    // dictionaries (UserDic.udic, the seeded and added dictionaries), then
    // the bundled ones beside the executable (legacy's location).
    options.spellingBackend = backends::hunspellSpellingLoader();
    options.dictionaryDir = userFolder.dictionaries();
    options.bundledDictionaryDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
    options.catalogDir = userFolder.catalogs();
    options.fontCatalogDir = userFolder.catalogs();
    options.presetsDir = userFolder.presets();
    options.audioCacheDir = userFolder.audioCache();
    options.indexDir = userFolder.indices();
    // O5: the first start on a Polish system takes Polish (Application).
    options.systemUiLanguages = QLocale::system().uiLanguages();
    // ADR 0019: the install's package kind, the update cache and installers.
    options.updateEnvironment = updateEnvironment;
    // What scripts write and the user's own autoload and include scripts
    // are in the user folder (`?user`), the shipped ones in the program's
    // Automation folder.
    options.userAutomationDir = userFolder.root();
    // AN1 (#404): anonymous usage data, sent only by a build with a key
    // (release packages) while the setting is on and DO_NOT_TRACK is unset;
    // its queue and a crash's record are in the cache folder.
    QDir().mkpath(userFolder.cache());
    options.analytics.key = Analytics::compiledKey();
    options.analytics.queueFile = userFolder.cache() + QStringLiteral("/analytics-queue.json");
    // CR1 (#452): the records a crash or a freeze leaves, in the cache
    // folder; the report kept until it is answered, the Log's last lines and
    // an issue's attachments in its reports/, the user's alone.
    options.crashReports.crashRecord = userFolder.cache() + QStringLiteral("/crash-record.txt");
    options.crashReports.freezeRecord = userFolder.cache() + QStringLiteral("/hang-record.txt");
    options.crashReports.pendingFile = userFolder.cache() + QStringLiteral("/reports/crash-report.json");
    options.crashReports.logTail = userFolder.cache() + QStringLiteral("/reports/log-tail.txt");
    options.crashReports.attachments = userFolder.cache() + QStringLiteral("/reports/issue");
    options.crashReports.version = QStringLiteral(HIKARI_VERSION);
    options.analytics.version = QStringLiteral(HIKARI_VERSION);
    options.analytics.doNotTrack = Analytics::doNotTrack(qgetenv("DO_NOT_TRACK"));
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
    // CR1 (#452): the crash handler is always there; its record stays on
    // this machine until the user sends it (with usage data on, AN1's
    // $exception goes at the next start). The hang watchdog watches this
    // thread (HIKARISUB_HANG_WATCHDOG_MS: another threshold, 0 none).
    crash::install(options.crashReports.crashRecord, QCoreApplication::applicationDirPath(), QStringLiteral(HIKARI_VERSION));
    std::unique_ptr<HangWatchdog> watchdog;
    {
        bool set = false;
        const int threshold = qEnvironmentVariableIntValue("HIKARISUB_HANG_WATCHDOG_MS", &set);
        if (!set || threshold > 0) {
            HangWatchdog::Config c{options.crashReports.freezeRecord, QStringLiteral(HIKARI_VERSION)};
            if (set) {
                c.thresholdMs = threshold;
                c.tickMs = std::clamp(threshold / 10, 50, 1000);
            }
            watchdog = std::make_unique<HangWatchdog>(c);
            QObject::connect(watchdog.get(), &HangWatchdog::recovered, &application,
                             [&application](const crash::Record &record) {
                                 application.crashReports().freezeRecovered(record);
                             });
        }
    }
    if (const QByteArray test = qgetenv("HIKARISUB_CRASH_TEST"); !test.isEmpty())
        crashForTest(test, options.crashReports.crashRecord);
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
    // AN1: app_started once the window shows (its arrangement settled
    // first), with the time it took.
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
        QObject::connect(
            window, &QQuickWindow::frameSwapped, &application,
            [&application, &started, window, portableCopy = folderEnvironment.portable,
             kind = updateEnvironment.package.kind] {
                const qint64 elapsed = started.elapsed();
                QTimer::singleShot(0, &application, [&application, window, portableCopy, kind, elapsed] {
                    Analytics::StartFacts facts;
                    facts.package = usagePackage(kind, portableCopy);
                    facts.screens = int(QGuiApplication::screens().size());
                    facts.scale = window->devicePixelRatio();
                    facts.startupMs = elapsed;
                    // A saved layout goes as "saved", never by its name.
                    facts.layout = application.workspaceLayout().layout().isEmpty()
                                       ? application.workspaceLayout().preset()
                                       : QStringLiteral("saved");
                    application.analytics().appStarted(facts);
                    application.crashReports().started(facts.package, facts.screens, facts.scale, facts.layout);
                });
            },
            Qt::SingleShotConnection);
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
    // AN1: app_closed and a last, short send; the window is gone.
    application.analytics().quit();
    // WS5: the next start is the instance, an update's new version included.
    reclaim.stop();
    instance.release();
    // WS5: what was handed over and not opened (the quit came first) is
    // saved before an update can start a new version.
    const QStringList unopened = forwarded.takeUnopened();
    const QString kept = handedOnPath(userFolder.cache());
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
