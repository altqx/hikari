// K2 / W1: the theme's palette reaches the popups from the start. A Qt Quick
// window makes its own palette, which its popups inherit, when it is first
// read, from the controls' theme; made after the theme's palette was set,
// it stayed light under Dark until the theme next changed, so every menu and
// dialog opened light (the video context menu and its DirectShow Filters
// submenu among them). Only the first engine of a process shows it, so this
// test loads the application window once, as the program does.

#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QColor>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

class ThemeStartupTests : public QObject {
    Q_OBJECT

private slots:
    void menusOpenOnTheThemesSurface()
    {
        ui::theme::forceSystemScheme(Qt::ColorScheme::Dark); // Dark, the default, by following the system
        app::Application::Options options;
        options.playbackAudio = false;
        app::Application application(options);
        ui::theme::chooseControlsStyle(); // as the program does (composition.cpp)
        QQmlApplicationEngine engine;
        ui::attachDocking(engine);
        engine.setInitialProperties(application.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const QColor field = ui::theme::current().roles.field;
        QVERIFY(field.lightness() < 128);
        const auto surface = [](QObject *menu) {
            return menu->property("background").value<QQuickItem *>()->property("color").value<QColor>();
        };
        // The first popup opened: the video's context menu.
        auto *videoMenu = window->findChild<QObject *>(QStringLiteral("videoContextMenu"));
        QVERIFY(videoMenu);
        QVERIFY(QMetaObject::invokeMethod(videoMenu, "openAt", Q_ARG(QVariant, QPointF(20, 20))));
        QTRY_VERIFY(videoMenu->property("opened").toBool());
        QTRY_COMPARE(surface(videoMenu), field);
        QCOMPARE(window->property("palette").value<QObject *>()->property("base").value<QColor>(), field);
        QVERIFY(QMetaObject::invokeMethod(videoMenu, "close"));
        QTRY_VERIFY(!videoMenu->property("visible").toBool());
        // A menu of the menu bar.
        auto *bar = window->findChild<QQuickItem *>(QStringLiteral("fileMenuBarItem"));
        QVERIFY(bar);
        auto *fileMenu = bar->property("menu").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(fileMenu, "popup", Q_ARG(QQuickItem *, bar),
                                          Q_ARG(QPointF, QPointF(0, bar->height()))));
        QTRY_VERIFY(fileMenu->property("opened").toBool());
        QCOMPARE(surface(fileMenu), field);
        QVERIFY(QMetaObject::invokeMethod(fileMenu, "close"));
        QTRY_VERIFY(!fileMenu->property("visible").toBool());
        ui::theme::forceSystemScheme(std::nullopt);
    }
};

QTEST_MAIN(ThemeStartupTests)
#include "theme_startup_tests.moc"
