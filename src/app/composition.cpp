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
    // A null pointer the compiler cannot prove, so the write is not optimised away.
    *static_cast<volatile int *>(qEnvironmentVariableIsSet("HIKARISUB_CRASH_TEST_NEVER_SET") ? &crashTestSink : nullptr) = 1;
}

#ifdef HIKARI_UPDATE_TEST_FEED
QString originOf(const QUrl &url)
{
    return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
}
#endif

std::function<void()> showInstalling(QQmlApplicationEngine &engine, QObject *settings, const QString &version)
{
    QQmlComponent component(&engine);
    component.loadFromModule("Hikari.Ui", "UpdateInstallingWindow");
    QObject *window = component.createWithInitialProperties(
        {{QStringLiteral("version"), version}, {QStringLiteral("settings"), QVariant::fromValue(settings)}});
    if (!window)
        return {};
    QGuiApplication::setQuitOnLastWindowClosed(false);
    return [window] { delete window; };
}

QString handedOnPath(const QString &folder)
{
    const QString session = SingleInstance::forSession().server;
    if (folder.isEmpty() || session.isEmpty() || !SingleInstance::trustsUserFiles())
        return {};
    const QString tag =
        QString::fromLatin1(QCryptographicHash::hash(session.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    return folder + QStringLiteral("/HandedOn-") + tag + QStringLiteral(".txt");
}

QString bundledFolder()
{
    const QString program = QCoreApplication::applicationDirPath();
#ifdef HIKARI_BUILD_AUTOMATION
    if (!QFileInfo::exists(program + QStringLiteral("/Automation/automation")))
        return QFileInfo(QStringLiteral(HIKARI_BUILD_AUTOMATION)).absolutePath();
#endif
    return program;
}

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
    QElapsedTimer started;
    started.start();
    preferPulseAudioBackend();
    // Read before QGuiApplication, which takes and unsets XDG_ACTIVATION_TOKEN.
    const QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
    QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));
    const UpdateEnvironment updateEnvironment = UpdateEnvironment::current();
    const auto folderEnvironment = UserFolder::Environment::current(
        updateEnvironment.package.kind == application::PackageKind::Portable &&
        QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/Automation/automation")));
    const UserFolder userFolder = UserFolder::locate(folderEnvironment);
    const UserFolder::OldFolders oldFolders = UserFolder::OldFolders::of(folderEnvironment);
    QStringList files = launchFiles(QCoreApplication::arguments().mid(1), QDir::currentPath());
    if (mode == StartupMode::Run)
        for (const QString &folder : {userFolder.cache(), oldFolders.data})
            if (const QString kept = handedOnPath(folder); !kept.isEmpty())
                files += takeHandedOn(kept);
#ifndef _WIN32
    if (mode == StartupMode::Run) {
        UpdateInstaller early(UpdateEnvironment::current(), QStringLiteral(HIKARI_VERSION));
        if (early.installAppImageAtStart(files))
            return 0;
    }
#endif
    SingleInstance instance(mode == StartupMode::Run ? SingleInstance::forSession() : SingleInstance::Location{});
    const SingleInstance::Role role = instance.claim({files, activationToken}, startup.healthFile.empty());
    if (role == SingleInstance::Role::Forwarded)
        return 0;
    QTimer reclaim;
    if (role == SingleInstance::Role::Alone && mode == StartupMode::Run) {
        QObject::connect(&reclaim, &QTimer::timeout, &instance, [&reclaim, &instance] {
            if (instance.reclaim())
                reclaim.stop();
        });
        reclaim.start(1000);
    }

    Application::Options options;
    options.autoload = true;
    if (folderEnvironment.userDirOverride.isEmpty())
        options.userFolderMove = userFolder.moveIn(oldFolders, folderEnvironment.portable);
    options.userFolderSeed = userFolder.seed(bundledFolder());
    options.userFolder = userFolder.root();
    options.gettingStarted = true;
    options.settingsFile = userFolder.settingsFile();
    options.recoveryDir = userFolder.recovery();
    options.spellingBackend = backends::hunspellSpellingLoader();
    options.dictionaryDir = userFolder.dictionaries();
    options.bundledDictionaryDir = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
    options.catalogDir = userFolder.catalogs();
    options.fontCatalogDir = userFolder.catalogs();
    options.presetsDir = userFolder.presets();
    options.audioCacheDir = userFolder.audioCache();
    options.indexDir = userFolder.indices();
    options.systemUiLanguages = QLocale::system().uiLanguages();
    options.updateEnvironment = updateEnvironment;
    options.userAutomationDir = userFolder.root();
    QDir().mkpath(userFolder.cache());
    options.analytics.key = Analytics::compiledKey();
    options.analytics.queueFile = userFolder.cache() + QStringLiteral("/analytics-queue.json");
    options.crashReports.crashRecord = userFolder.cache() + QStringLiteral("/crash-record.txt");
    options.crashReports.freezeRecord = userFolder.cache() + QStringLiteral("/hang-record.txt");
    options.crashReports.pendingFile = userFolder.cache() + QStringLiteral("/reports/crash-report.json");
    options.crashReports.logTail = userFolder.cache() + QStringLiteral("/reports/log-tail.txt");
    options.crashReports.attachments = userFolder.cache() + QStringLiteral("/reports/issue");
    options.crashReports.version = QStringLiteral(HIKARI_VERSION);
    options.analytics.version = QStringLiteral(HIKARI_VERSION);
    options.analytics.doNotTrack = Analytics::doNotTrack(qgetenv("DO_NOT_TRACK"));
#ifdef HIKARI_UPDATE_TEST_FEED
    if (const QString feed = qEnvironmentVariable("HIKARISUB_UPDATE_FEED"); !feed.isEmpty()) {
        options.updateFeed = UpdateFeed::local(QUrl(feed));
        const QString feedOrigin = originOf(QUrl(feed));
        options.updateEnvironment.allowRedirect = [feedOrigin](const QUrl &to) { return originOf(to) == feedOrigin; };
    }
#endif
    Application application(options);
    application.setStartedWithPaths(!files.isEmpty());
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
    const auto startKind = files.size() == 1 ? application::openKindOf(files.front().toStdString(), true)
                                             : application::OpenKind::Refused;
    const QString startPath =
        startKind == application::OpenKind::Subtitles || startKind == application::OpenKind::Video ? files.front()
                                                                                                   : QString();
    const bool startVideo = !startPath.isEmpty() && openStartFile(application, startPath);
    if (startPath.isEmpty() && !files.isEmpty())
        forwarded.take({files, {}});

    hikari::ui::theme::chooseControlsStyle();
    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    attachStylePreview(engine, application.styleManager());
    attachFontCatalogPreview(engine, application.fontCatalogs());
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
    QTimer::singleShot(0, &application, [&application] {
        application.updates().startUp();
        application.updates().checkOnStartup();
        // MUST KILL: after checkOnStartup(), whose list it shares.
        application.whatsNew().start();
    });
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
                    facts.layout = application.workspaceLayout().layout().isEmpty()
                                       ? application.workspaceLayout().preset()
                                       : QStringLiteral("saved");
                    application.analytics().appStarted(facts);
                    application.crashReports().started(facts.package, facts.screens, facts.scale, facts.layout);
                });
            },
            Qt::SingleShotConnection);
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
    application.analytics().quit();
    reclaim.stop();
    instance.release();
    const QStringList unopened = forwarded.takeUnopened();
    const QString kept = handedOnPath(userFolder.cache());
    const bool saved = unopened.isEmpty() || (!kept.isEmpty() && saveHandedOn(kept, unopened));
    const QStringList carried = saved ? QStringList() : unopened;
#ifdef _WIN32
    const bool install = code == 0 && carried.isEmpty();
#else
    const bool install = code == 0;
#endif
    const bool updateStarts = install && application.updates().installer().restartRequested();
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

}

int run(int argc, char **argv, StartupMode mode)
{
#ifdef _WIN32
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
        QCoreApplication core(argc, argv);
        std::printf("%s (%s)\n", HIKARI_VERSION, qPrintable(packageKindToken(currentPackageKind().kind)));
        return 0;
    }
    if (startup.exportDocs) {
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
    if (update_apply::setupRunning()) {
        QCoreApplication core(argc, argv);
        const auto kind = currentPackageKind().kind;
        if (kind == application::PackageKind::NsisUser || kind == application::PackageKind::NsisMachine) {
            update_apply::askStartAfterSetup();
            return 0;
        }
    }
    // MUST KILL: handle leaked on purpose; the installer waits while this mutex exists.
    CreateMutexW(nullptr, FALSE, L"Global\\io.github.altqx.HikariSub.Running");
#endif
    UpdateInstaller::ExitAction exitAction;
    const int code = runWindow(argc, argv, mode, startup, exitAction);
#ifndef _WIN32
    if (!exitAction.program.isEmpty()) {
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

}
