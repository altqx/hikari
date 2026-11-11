#include "hikari/app/application.h"
#include "docking.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#ifdef _WIN32
#include <windows.h>
#endif

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

bool mediaBackendLoaded()
{
#ifdef _WIN32
    for (const wchar_t *name : {L"ffmpegmediaplugin.dll", L"ffmpegmediaplugind.dll", L"windowsmediaplugin.dll"})
        if (GetModuleHandleW(name))
            return true;
    return false;
#else
    QFile maps(QStringLiteral("/proc/self/maps"));
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    return maps.readAll().contains("mediaplugin");
#endif
}

}

class ColdStartTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

private slots:
    void theMediaBackendWaitsForTheOutputList()
    {
#if !defined(_WIN32) && !defined(Q_OS_LINUX)
        QSKIP("loaded modules are read from /proc or the Windows loader");
#endif
        QVERIFY(dir.isValid());
        QVERIFY(!mediaBackendLoaded());
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.recoveryDir = dir.filePath(QStringLiteral("Recovery"));
        options.spellingBackend = {};
        auto application = std::make_unique<app::Application>(options);
        auto engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(2000);
        QVERIFY2(!mediaBackendLoaded(), "Qt Multimedia's backend loaded before any media or output list");
        QCOMPARE(application->audioOutputName(), QStringLiteral("System default"));

        application->audioOutputMenu();
        QVERIFY2(mediaBackendLoaded(), "listing the outputs did not load a backend, so the check above proves nothing");
        engine.reset();
        application.reset();
    }
};

QTEST_MAIN(ColdStartTests)
#include "cold_start_tests.moc"
