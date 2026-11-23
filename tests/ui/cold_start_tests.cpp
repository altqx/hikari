#include "hikari/app/application.h"
#include "docking.h"

#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
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
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    void start()
    {
        QVERIFY(dir.isValid());
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.recoveryDir = dir.filePath(QStringLiteral("Recovery"));
        options.spellingBackend = {};
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    QList<QObject *> titleBarMenus() const
    {
        QList<QObject *> menus;
        auto *bar = window->findChild<QObject *>(QStringLiteral("appMenus"));
        const int count = bar ? bar->property("count").toInt() : 0;
        for (int i = 0; i < count; ++i) {
            QQuickItem *title = nullptr;
            QMetaObject::invokeMethod(bar, "itemAt", Q_RETURN_ARG(QQuickItem *, title), Q_ARG(int, i));
            menus << title->property("menu").value<QObject *>();
        }
        return menus;
    }
    static bool isMenuRow(const QObject *object)
    {
        for (const QMetaObject *type = object->metaObject(); type; type = type->superClass())
            if (QByteArrayView(type->className()).startsWith("ShellMenuItem_QMLTYPE"))
                return true;
        return false;
    }
    static bool drawn(const QObject *row)
    {
        const auto *content = row->property("contentItem").value<QQuickItem *>();
        return content && content->property("item").value<QObject *>();
    }
    static bool inUnopenedMenu(const QObject *row)
    {
        const auto *menu = row->property("menu").value<QObject *>();
        return menu && menu->property("hasOpened") == false;
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }

    void theMediaBackendWaitsForTheOutputList()
    {
#if !defined(_WIN32) && !defined(Q_OS_LINUX)
        QSKIP("loaded modules are read from /proc or the Windows loader");
#endif
        QVERIFY(!mediaBackendLoaded());
        start();
        QTest::qWait(2000);
        QVERIFY2(!mediaBackendLoaded(), "Qt Multimedia's backend loaded before any media or output list");
        QCOMPARE(application->audioOutputName(), QStringLiteral("System default"));

        application->audioOutputMenu();
        QVERIFY2(mediaBackendLoaded(), "listing the outputs did not load a backend, so the check above proves nothing");
    }

    void titleBarMenusWaitForTheirFirstOpen()
    {
        start();
        const QList<QObject *> menus = titleBarMenus();
        QCOMPARE(menus.size(), 8);
        for (QObject *menu : menus)
            QVERIFY2(menu->property("count").toInt() == 0, qPrintable(menu->property("title").toString()));

        QVERIFY(QMetaObject::invokeMethod(window, "runGlobalHotkey", Q_ARG(QVariant, QStringLiteral("GLOBAL_ABOUT"))));
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("aboutDialog")));
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("aboutDialog"))->isVisible());
        for (QObject *menu : menus)
            QCOMPARE(menu->property("count").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(menus.first(), "open"));
        QTRY_VERIFY(menus.first()->property("count").toInt() > 0);
        QVERIFY(window->findChild<QObject *>(QStringLiteral("openFolderMenuItem")));
    }

    void menuRowsDrawOnTheirMenusFirstOpen()
    {
        start();
        const QList<QObject *> everything = window->findChildren<QObject *>();
        qInfo("%lld objects at start", static_cast<long long>(everything.size()));
        QList<QObject *> waiting;
        for (QObject *object : everything)
            if (isMenuRow(object) && inUnopenedMenu(object))
                waiting << object;
        QVERIFY2(waiting.size() > 300, qPrintable(QString::number(waiting.size())));
        for (QObject *row : waiting)
            QVERIFY2(!drawn(row), qPrintable(row->property("text").toString()));

        auto *menu = window->findChild<QObject *>(QStringLiteral("gridMenu"));
        QVERIFY(menu && menu->property("hasOpened") == false);
        QElapsedTimer opening;
        opening.start();
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        qInfo("gridMenu's first open held the window for %lld ms", static_cast<long long>(opening.elapsed()));
        QTRY_VERIFY(menu->property("opened").toBool());
        for (QObject *row : waiting)
            if (row->property("menu").value<QObject *>() == menu)
                QVERIFY2(drawn(row), qPrintable(row->property("text").toString()));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    }
};

QTEST_MAIN(ColdStartTests)
#include "cold_start_tests.moc"
