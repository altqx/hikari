#include "window_frame.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace hikari;
using ui::MainWindowFrame;
namespace wf = ui::windowframe;
using wf::Desktop;

Q_DECLARE_METATYPE(wf::Desktop)

class WindowFrameTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { wf::overrideDesktop(std::nullopt); }

    void desktopsAreTold_data()
    {
        QTest::addColumn<QString>("platform");
        QTest::addColumn<QString>("current");
        QTest::addColumn<QString>("session");
        QTest::addColumn<QStringList>("markers");
        QTest::addColumn<QStringList>("globals");
        QTest::addColumn<Desktop>("desktop");
        const QStringList none;
        QTest::newRow("windows") << "windows" << "" << "" << none << none << Desktop::Windows;
        QTest::newRow("plasma wayland") << "wayland" << "KDE" << "plasma" << none << none << Desktop::Kde;
        QTest::newRow("plasma x11") << "xcb" << "KDE" << "plasmax11" << none << none << Desktop::Kde;
        QTest::newRow("gnome") << "wayland" << "GNOME" << "gnome" << none << none << Desktop::Gnome;
        QTest::newRow("ubuntu") << "wayland" << "ubuntu:GNOME" << "ubuntu" << none << none << Desktop::Gnome;
        QTest::newRow("gnome classic") << "xcb" << "GNOME-Classic:GNOME" << "gnome-classic" << none << none
                                       << Desktop::Gnome;
        QTest::newRow("xfce") << "xcb" << "XFCE" << "xfce" << none << none << Desktop::Other;
        QTest::newRow("nothing set") << "xcb" << "" << "" << none << none << Desktop::Other;
        QTest::newRow("sway") << "wayland" << "sway" << "sway" << QStringList{"SWAYSOCK"} << none << Desktop::Tiling;
        QTest::newRow("hyprland") << "wayland" << "Hyprland" << "hyprland" << none << none << Desktop::Tiling;
        QTest::newRow("niri") << "wayland-egl" << "niri" << "" << none << none << Desktop::Tiling;
        QTest::newRow("i3") << "xcb" << "i3" << "i3" << none << none << Desktop::Tiling;
        QTest::newRow("plasma with i3") << "xcb" << "KDE" << "plasma" << QStringList{"I3SOCK"} << none
                                        << Desktop::Tiling;
        QTest::newRow("hyprland unnamed") << "wayland" << "" << "" << none
                                          << QStringList{"wl_compositor", "hyprland_focus_grab_manager_v1"}
                                          << Desktop::Tiling;
        QTest::newRow("river unnamed") << "wayland" << "" << "" << none
                                       << QStringList{"zriver_control_v1"} << Desktop::Tiling;
        QTest::newRow("offscreen") << "offscreen" << "KDE" << "plasma" << none << none << Desktop::Unsupported;
        QTest::newRow("minimal") << "minimal" << "" << "" << none << none << Desktop::Unsupported;
    }
    void desktopsAreTold()
    {
        QFETCH(QString, platform);
        QFETCH(QString, current);
        QFETCH(QString, session);
        QFETCH(QStringList, markers);
        QFETCH(QStringList, globals);
        QFETCH(Desktop, desktop);
        QCOMPARE(wf::classify(wf::Environment{platform, current, session, markers, globals}), desktop);
    }

    void systemTitleBarByDesktopAndSetting()
    {
        for (Desktop d : {Desktop::Windows, Desktop::Kde, Desktop::Gnome}) {
            QVERIFY(!wf::systemTitleBarByDefault(d));
            QVERIFY(!wf::usesSystemTitleBar(QStringLiteral("auto"), d));
            QVERIFY(!wf::usesSystemTitleBar(QStringLiteral(""), d));
            QVERIFY(wf::usesSystemTitleBar(QStringLiteral("system"), d));
            QVERIFY(!wf::usesSystemTitleBar(QStringLiteral("custom"), d));
        }
        for (Desktop d : {Desktop::Tiling, Desktop::Other}) {
            QVERIFY(wf::systemTitleBarByDefault(d));
            QVERIFY(wf::usesSystemTitleBar(QStringLiteral("auto"), d));
            QVERIFY(!wf::usesSystemTitleBar(QStringLiteral("custom"), d));
        }
        QVERIFY(wf::usesSystemTitleBar(QStringLiteral("custom"), Desktop::Unsupported));
        ui::WindowFrame frame;
        QSignalSpy changed(&frame, &ui::WindowFrame::desktopChanged);
        wf::overrideDesktop(Desktop::Gnome);
        QCOMPARE(changed.count(), 1);
        QVERIFY(frame.customAvailable());
        QVERIFY(!frame.systemTitleBarByDefault());
        QVERIFY(!frame.usesSystemTitleBar(QStringLiteral("auto")));
        wf::overrideDesktop(Desktop::Unsupported);
        QVERIFY(!frame.customAvailable());
        QVERIFY(frame.usesSystemTitleBar(QStringLiteral("custom")));
    }

    void frameIsFramelessWithTheSystemsHints()
    {
        QQuickWindow window;
        window.resize(400, 300);
        MainWindowFrame frame;
        frame.setWindow(&window);
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));
        frame.setCustom(true);
        const Qt::WindowFlags hints = Qt::FramelessWindowHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                                      | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint;
        QCOMPARE(window.flags() & hints, hints);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        frame.setCustom(false);
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(window.isVisible());
        frame.setCustom(true);
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(window.isVisible());
        frame.toggleMaximized();
        QTRY_COMPARE(window.visibility(), QWindow::Maximized);
        frame.toggleMaximized();
        QTRY_COMPARE(window.visibility(), QWindow::Windowed);
    }

    void titleBarPartsAsTheWindowSystemIsTold()
    {
        QQuickWindow window;
        window.resize(400, 300);
        auto *bar = new QQuickItem(window.contentItem());
        bar->setSize(QSizeF(400, 32));
        auto *moveArea = new QQuickItem(bar);
        moveArea->setSize(bar->size());
        moveArea->setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
        auto *icon = new QQuickItem(bar);
        icon->setSize(QSizeF(32, 32));
        auto *row = new QQuickItem(bar);
        row->setPosition(QPointF(32, 4));
        row->setSize(QSizeF(120, 24));
        auto *menu = new QQuickItem(row);
        menu->setSize(QSizeF(40, 24));
        menu->setAcceptedMouseButtons(Qt::LeftButton);
        auto *hidden = new QQuickItem(row);
        hidden->setPosition(QPointF(60, 0));
        hidden->setSize(QSizeF(40, 24));
        hidden->setAcceptedMouseButtons(Qt::LeftButton);
        hidden->setVisible(false);
        auto *maximize = new QQuickItem(bar);
        maximize->setPosition(QPointF(308, 0));
        maximize->setSize(QSizeF(46, 32));
        maximize->setAcceptedMouseButtons(Qt::LeftButton);
        auto *closeButton = new QQuickItem(bar);
        closeButton->setPosition(QPointF(354, 0));
        closeButton->setSize(QSizeF(46, 32));
        closeButton->setAcceptedMouseButtons(Qt::LeftButton);

        MainWindowFrame frame;
        frame.setWindow(&window);
        frame.setTitleBar(bar);
        frame.setMoveArea(moveArea);
        frame.setMaximizeButton(maximize);
        frame.setWindowMenuButton(icon);
        QCOMPARE(frame.hitAt(QPointF(200, 16)), MainWindowFrame::Client);
        frame.setCustom(true);
        QCOMPARE(frame.hitAt(QPointF(200, 16)), MainWindowFrame::Caption);
        QCOMPARE(frame.hitAt(QPointF(52, 16)), MainWindowFrame::Client);
        QCOMPARE(frame.hitAt(QPointF(112, 16)), MainWindowFrame::Caption);
        QCOMPARE(frame.hitAt(QPointF(330, 16)), MainWindowFrame::MaximizeButton);
        QCOMPARE(frame.hitAt(QPointF(16, 16)), MainWindowFrame::WindowMenuButton);
        QCOMPARE(frame.hitAt(QPointF(380, 16)), MainWindowFrame::Client);
        QCOMPARE(frame.hitAt(QPointF(200, 100)), MainWindowFrame::Client);
        QSignalSpy asked(&frame, &MainWindowFrame::windowMenuRequested);
        frame.showWindowMenu(QPointF(16, 32));
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().first().toPointF(), QPointF(16, 32));
    }

    void altAloneIsATap()
    {
        QQuickWindow window;
        window.resize(200, 100);
        MainWindowFrame frame;
        frame.setWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy tapped(&frame, &MainWindowFrame::altTapped);
        QTest::keyPress(&window, Qt::Key_Alt);
        QVERIFY(frame.altHeld());
        QTest::keyRelease(&window, Qt::Key_Alt);
        QVERIFY(!frame.altHeld());
        QCOMPARE(tapped.count(), 1);
        QTest::keyPress(&window, Qt::Key_Alt);
        QTest::keyClick(&window, Qt::Key_X, Qt::AltModifier);
        QTest::keyRelease(&window, Qt::Key_Alt);
        QCOMPARE(tapped.count(), 1);
        QTest::keyPress(&window, Qt::Key_Alt);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::AltModifier, QPoint(10, 10));
        QTest::keyRelease(&window, Qt::Key_Alt);
        QCOMPARE(tapped.count(), 1);
        auto *focus = new QQuickItem(window.contentItem());
        focus->forceActiveFocus();
        QTest::keyPress(&window, Qt::Key_Alt);
        QKeyEvent overrideN(QEvent::ShortcutOverride, Qt::Key_N, Qt::AltModifier, QStringLiteral("n"));
        QCoreApplication::sendEvent(focus, &overrideN);
        QTest::keyRelease(&window, Qt::Key_Alt);
        QCOMPARE(tapped.count(), 1);
        QTest::keyPress(&window, Qt::Key_Control);
        QTest::keyPress(&window, Qt::Key_Alt, Qt::ControlModifier);
        QTest::keyRelease(&window, Qt::Key_Alt, Qt::ControlModifier);
        QTest::keyRelease(&window, Qt::Key_Control);
        QCOMPARE(tapped.count(), 1);
        QTest::keyClick(&window, Qt::Key_Alt);
        QCOMPARE(tapped.count(), 2);
    }
};

QTEST_MAIN(WindowFrameTests)
#include "window_frame_tests.moc"
