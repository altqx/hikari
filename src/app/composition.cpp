#include "hikari/app/composition.h"

#include "hikari/application/workspace.h"
#include "shell_controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
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

    // No Document is open at start: opening arrives with A2.
    application::Workspace workspace;
    ui::ShellController shell(workspace);
    shell.refresh(nullptr, nullptr);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("shell"), QVariant::fromValue(&shell)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    if (mode == StartupMode::ExitAfterWindowCreated)
        QTimer::singleShot(0, &app, [] { QCoreApplication::exit(0); });
    return app.exec();
}

} // namespace hikari::app
