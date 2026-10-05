#include "hikari/app/composition.h"
#include "docking.h"

#include "hikari/app/application.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/backends/legacy_spelling.h"

#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

// The UI module is a static QML module; its plugin must be imported explicitly.
Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace hikari::app {

int run(int argc, char **argv, StartupMode mode)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HikariSub"));
    QGuiApplication::setOrganizationName(QStringLiteral("HikariSub"));

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
    Application application(options);
    application.setStartedWithPaths(argc > 1); // P6: no session at start then
    // A path on the command line opens as the editing target.
    if (argc > 1)
        application.openFile(QString::fromLocal8Bit(argv[1]));

    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    attachStylePreview(engine, application.styleManager());
    engine.setInitialProperties(application.qmlProperties());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    if (mode == StartupMode::ExitAfterWindowCreated)
        QTimer::singleShot(0, &app, [] { QCoreApplication::exit(0); });
    else // legacy CallAfter(CheckOnStartup): once the window is up
        QTimer::singleShot(0, &application, [&application] { application.updates().checkOnStartup(); });
    return app.exec();
}

} // namespace hikari::app
