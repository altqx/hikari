#include "colour_picker_controller.h"
#include "screen_sampler.h"
#include "settings_store.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRasterWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;

namespace {

QVariantMap rgba(int r, int g, int b, int a = 0)
{
    return {{QStringLiteral("r"), r}, {QStringLiteral("g"), g}, {QStringLiteral("b"), b}, {QStringLiteral("a"), a}};
}

class PositionWindow : public QRasterWindow {
protected:
    void paintEvent(QPaintEvent *) override
    {
        QImage image(size(), QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.setPixel(x, y, qRgb(x % 256, y % 256, 77));
        QPainter(this).drawImage(0, 0, image);
    }
};

QPoint sentPosition(QWindow &window, QPoint local, QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(local), QPointF(window.mapToGlobal(local)), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
    return window.mapToGlobal(local);
}

}

class ColourPickerTest : public QObject {
    Q_OBJECT

private slots:
    void switchClicksIsAPersistedOption()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("settings.ini"));
        {
            ui::SettingsStore store(ini);
            ui::ColourPickerController picker(store);
            QVERIFY(!picker.switchClicks());
            QSignalSpy changed(&picker, &ui::ColourPickerController::switchClicksChanged);
            picker.setSwitchClicks(true);
            QCOMPARE(changed.count(), 1);
            QVERIFY(store.boolean("colourPicker.switchClicks"));
            store.sync();
        }
        ui::SettingsStore again(ini);
        ui::ColourPickerController picker(again);
        QVERIFY(picker.switchClicks());
        QSignalSpy changed(&picker, &ui::ColourPickerController::switchClicksChanged);
        again.resetAll();
        QVERIFY(!picker.switchClicks());
        QVERIFY(changed.count() >= 1);
    }

    void simplePickerRecentEditsTheOptionText()
    {
        ui::SettingsStore store;
        store.set("colourPicker.recentColours", QStringLiteral("&H000000FF& &H0000FF00& &H00FF0000&"));
        ui::ColourPickerController picker(store);
        picker.addRecentFromSimplePicker(rgba(0, 255, 0));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H0000FF00& &H000000FF& &H00FF0000&"));
        QCOMPARE(picker.recent().first().toMap().value(QStringLiteral("g")).toInt(), 255);
        picker.addRecentFromSimplePicker(rgba(0, 0, 255));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H00FF0000& &H0000FF00& &H000000FF&"));
        store.set("colourPicker.recentColours", QString());
        picker.addRecentFromSimplePicker(rgba(1, 2, 3, 4));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H04030201&"));
        store.set("colourPicker.recentColours", QStringLiteral("  &H000000FF&   &H04030201&  "));
        picker.addRecentFromSimplePicker(rgba(1, 2, 3, 4));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H04030201& &H000000FF&"));

        QStringList full;
        for (int i = 0; i < 32; ++i)
            full << QStringLiteral("&H000000%1&").arg(i, 2, 16, QLatin1Char('0')).toUpper();
        store.set("colourPicker.recentColours", full.join(QLatin1Char(' ')));
        picker.addRecentFromSimplePicker(rgba(0, 0, 0));
        QString text = store.text("colourPicker.recentColours");
        QCOMPARE(text, full.join(QLatin1Char(' ')));
        QCOMPARE(picker.recent().at(31).toMap().value(QStringLiteral("r")).toInt(), 0x1f);
        picker.addRecentFromSimplePicker(rgba(0x10, 0, 0));
        text = store.text("colourPicker.recentColours");
        QStringList moved = full;
        moved.move(16, 0);
        QCOMPARE(text, moved.join(QLatin1Char(' ')));
        picker.addRecentFromSimplePicker(rgba(0, 0, 0x7f));
        text = store.text("colourPicker.recentColours");
        QCOMPARE(text.count(QLatin1Char(' ')), 31);
        QVERIFY(text.startsWith(QStringLiteral("&H007F0000& &H00000010& &H00000000&")));
        QVERIFY(!text.contains(QStringLiteral("&H0000001F&")));
        QVERIFY(!text.contains(QStringLiteral("  ")));

        picker.opened(QStringLiteral("1"));
        picker.addRecentFromSimplePicker(rgba(0, 0, 0x1e));
        QCOMPARE(picker.recent().first().toMap().value(QStringLiteral("b")).toInt(), 0x1e);
        QCOMPARE(store.text("colourPicker.recentColours"), picker.storeToString());
    }

    void conversionsAreTheLegacyOnes()
    {
        ui::SettingsStore store;
        ui::ColourPickerController picker(store);
        QCOMPARE(picker.rgbToHsv(255, 128, 0), (QVariantList{21, 255, 255}));
        QCOMPARE(picker.rgbToHsl(255, 128, 0), (QVariantList{21, 255, 127}));
        QCOMPARE(picker.hsvToRgb(171, 255, 128), (QVariantList{0, 0, 128}));
        QCOMPARE(picker.hsvToHsl(170, 255, 255), (QVariantList{170, 255, 127}));
        QCOMPARE(picker.hslToHsv(170, 255, 127), (QVariantList{170, 255, 254}));
        QCOMPARE(picker.htmlColour(QStringLiteral(" #f80 ")), (QVariantMap{{"r", 255}, {"g", 136}, {"b", 0}}));
        QCOMPARE(picker.htmlText(rgba(255, 128, 0)), QStringLiteral("#FF8000"));
        QCOMPARE(picker.assText(rgba(255, 128, 0, 0x80), false), QStringLiteral("&H0080FF&"));
    }

    void routesFollowThePlatform()
    {
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("xcb"), false, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("windows"), false, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("offscreen"), true, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, true).name, QStringLiteral("portal"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("xcb"), true, true).name, QStringLiteral("portal"));
        QVERIFY(ui::ScreenSampler::routeFor(QStringLiteral("xcb"), true, false).name.isEmpty());
        const auto none = ui::ScreenSampler::routeFor(QStringLiteral("wayland-egl"), true, false);
        QVERIFY(none.name.isEmpty());
        QVERIFY(none.reason.contains(QStringLiteral("not available")));

        ui::ScreenSampler sampler;
        sampler.setRoute({QStringLiteral("grab"), {}});
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(Qt::red);
            return image;
        });
        QCOMPARE(sampler.sample(0, 0).size(), 49);
        QSignalSpy changed(&sampler, &ui::ScreenSampler::routeChanged);
        sampler.setRoute(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, false));
        QCOMPARE(changed.count(), 1);
        QVERIFY(!sampler.available());
        QVERIFY(!sampler.unavailableReason().isEmpty());
        QVERIFY(sampler.sample(0, 0).isEmpty());
        QSignalSpy failed(&sampler, &ui::ScreenSampler::portalFailed);
        sampler.pickFromPortal();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), sampler.unavailableReason());
        QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("no screenshot portal")));
        QVERIFY(!sampler.portalBusy());
        sampler.setRoute({QStringLiteral("grab"), {}});
        sampler.pickFromPortal();
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QStringLiteral("The desktop's screenshot portal is not in use here."));
        QVERIFY(!sampler.portalBusy());
    }

    void portalAnswersBeforeTheRequestIsOver()
    {
        ui::ScreenSampler sampler;
        sampler.setRoute(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, true));
        int asked = 0;
        sampler.setPortalRequest([&] { ++asked; });
        QStringList order;
        connect(&sampler, &ui::ScreenSampler::portalBusyChanged, this,
                [&] { order << (sampler.portalBusy() ? QStringLiteral("busy") : QStringLiteral("over")); });
        QVariantMap picked;
        connect(&sampler, &ui::ScreenSampler::portalPicked, this, [&](const QVariantMap &c) {
            QVERIFY(sampler.portalBusy());
            picked = c;
            order << QStringLiteral("picked");
        });
        QString message;
        connect(&sampler, &ui::ScreenSampler::portalFailed, this, [&](const QString &m) {
            message = m;
            order << QStringLiteral("failed");
        });

        sampler.pickFromPortal();
        QCOMPARE(asked, 1);
        QVERIFY(sampler.portalBusy());
        sampler.pickFromPortal();
        QCOMPARE(asked, 1);
        sampler.answerPortal(0, 1.0, 0.5, 0.0);
        QCOMPARE(order, (QStringList{QStringLiteral("busy"), QStringLiteral("picked"), QStringLiteral("over")}));
        QCOMPARE(picked, (QVariantMap{{"r", 255}, {"g", 128}, {"b", 0}, {"a", 0}}));
        QVERIFY(!sampler.portalBusy());
        sampler.answerPortal(0, 0, 0, 0);
        QCOMPARE(order.size(), 3);

        order.clear();
        sampler.pickFromPortal();
        sampler.answerPortal(1);
        QCOMPARE(order, (QStringList{QStringLiteral("busy"), QStringLiteral("over")}));

        order.clear();
        sampler.pickFromPortal();
        sampler.answerPortal(2);
        QCOMPARE(order, (QStringList{QStringLiteral("busy"), QStringLiteral("failed"), QStringLiteral("over")}));
        QCOMPARE(message, QStringLiteral("The desktop could not pick a colour from the screen."));

        order.clear();
        sampler.pickFromPortal();
        sampler.failPortalCall(QStringLiteral("No such interface"));
        QCOMPARE(order, (QStringList{QStringLiteral("busy"), QStringLiteral("failed"), QStringLiteral("over")}));
        QCOMPARE(message, QStringLiteral("No such interface"));
        QCOMPARE(asked, 4);
    }

    void theRouteOfThisSession()
    {
        ui::ScreenSampler sampler;
        const QString platform = QGuiApplication::platformName();
        qInfo().noquote() << "platform" << platform << "route" << sampler.route() << sampler.unavailableReason();
        if (platform.startsWith(QLatin1String("wayland"))
            || (platform == QLatin1String("xcb") && ui::ScreenSampler::isWaylandSession()))
            QVERIFY(sampler.route() == QLatin1String("portal")
                    || (sampler.route().isEmpty() && !sampler.unavailableReason().isEmpty()));
        else
            QCOMPARE(sampler.route(), QStringLiteral("grab"));
    }

    void theScreenRouteReadsThePixelsAroundThePoint()
    {
        ui::ScreenSampler sampler;
        if (sampler.route() != QLatin1String("grab"))
            QSKIP("this session reads no screen pixels (Wayland, or X11 on XWayland)");
        PositionWindow window;
        window.setFlags(Qt::FramelessWindowHint);
        window.setGeometry(QRect(100, 100, 200, 150));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QPoint inside(40, 30);
        const QPoint global = window.mapToGlobal(inside);
        QVariantList cells;
        QTRY_VERIFY((cells = sampler.sample(global.x(), global.y())).size() == 49
                    && cells.at(24).toMap().value(QStringLiteral("b")).toInt() == 77);
        const qreal ratio = window.devicePixelRatio();
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 7; ++column) {
                const auto cell = cells.at(row * 7 + column).toMap();
                QCOMPARE(cell.value(QStringLiteral("b")).toInt(), 77);
                QCOMPARE(cell.value(QStringLiteral("r")).toInt(), int(inside.x() * ratio) + column - 3);
                QCOMPARE(cell.value(QStringLiteral("g")).toInt(), int(inside.y() * ratio) + row - 3);
            }
    }

    void moveToPointerFollowsLegacy()
    {
        PositionWindow window;
        window.setGeometry(QRect(50, 60, 120, 90));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::ScreenSampler sampler;
        const QRect area = sampler.availableGeometryAt(0, 0);
        QVERIFY(area.width() > 400 && area.height() > 300);
        const QSize frame = window.frameGeometry().size();

        sampler.moveToPointer(&window, area.x() + 200, area.y() + 100);
        QTRY_COMPARE(window.frameGeometry().topLeft(), QPoint(area.x() + 200 - frame.width() / 2, area.y() + 115));
        sampler.moveToPointer(&window, area.x() + 3, area.y() + 100);
        QTRY_COMPARE(window.frameGeometry().x(), area.x());
        sampler.moveToPointer(&window, area.right() - 2, area.y() + 100);
        QTRY_COMPARE(window.frameGeometry().x(), area.x() + area.width() - frame.width());
        const int py = area.y() + area.height() - 20;
        sampler.moveToPointer(&window, area.x() + 200, py);
        QTRY_COMPARE(window.frameGeometry().y(), py + 15 - frame.height() - 30);
        window.resize(120, area.height() - 40);
        QTRY_VERIFY(window.frameGeometry().height() > area.height() - 60);
        const QSize tall = window.frameGeometry().size();
        sampler.moveToPointer(&window, area.x() + 200, area.y() + 30);
        QTRY_COMPARE(window.frameGeometry().y(), area.y() + area.height() - tall.height());
    }

    void trackingReportsThePointer()
    {
        if (ui::ScreenSampler().route() != QLatin1String("grab"))
            QSKIP("a Wayland session takes the portal route: the dropper never tracks the pointer there");
        PositionWindow window;
        window.setGeometry(QRect(50, 60, 120, 90));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::ScreenSampler sampler;
        QSignalSpy events(&sampler, &ui::ScreenSampler::pointerEvent);
        QVERIFY(sampler.startTracking(&window, false));
        QVERIFY(sampler.tracking());
        const QPoint outside = sentPosition(window, QPoint(-20, 200), QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        sentPosition(window, QPoint(10, 10), QEvent::MouseButtonRelease, Qt::RightButton, Qt::NoButton);
        QCOMPARE(events.count(), 2);
        QCOMPARE(events.at(0).at(0).toInt(), 0);
        QCOMPARE(events.at(0).at(1).toInt(), outside.x());
        QCOMPARE(events.at(0).at(2).toInt(), outside.y());
        QCOMPARE(events.at(0).at(5).toBool(), false);
        QCOMPARE(events.at(1).at(0).toInt(), 2);
        QCOMPARE(events.at(1).at(3).toInt(), int(Qt::RightButton));
        QCOMPARE(events.at(1).at(5).toBool(), true);

        QVERIFY(sampler.startTracking(&window, true));
        QMouseEvent over(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(window.mapToGlobal(QPoint(10, 10))),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &over);
        QCOMPARE(events.count(), 3);
        QCOMPARE(events.at(2).at(4).toInt(), int(Qt::LeftButton));
        sentPosition(window, QPoint(500, 10), QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(events.count(), 4);
        QCOMPARE(events.at(3).at(5).toBool(), false);

        sampler.stopTracking();
        QVERIFY(!sampler.tracking());
        sentPosition(window, QPoint(-5, -5), QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        QCOMPARE(events.count(), 4);
    }

    void theGrabWaitsForTheWindowToBeExposed()
    {
        QList<bool> asked;
        QWindow *grabbedWindow = nullptr;
        bool granted = true;
        ui::ScreenSampler sampler;
        sampler.setGrab([&](QWindow *w, bool grab) {
            grabbedWindow = w;
            asked.append(grab);
            return !grab || granted;
        });

        PositionWindow window;
        window.setGeometry(QRect(50, 60, 120, 90));
        QVERIFY(sampler.startTracking(&window, true));
        QVERIFY(sampler.tracking());
        QVERIFY(asked.isEmpty());
        QVERIFY(!sampler.holdsPointer());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(sampler.holdsPointer());
        QCOMPARE(grabbedWindow, &window);
        QVERIFY(!asked.isEmpty());
        QVERIFY(!asked.contains(false));

        QVERIFY(!sampler.regrabbing());
        qsizetype count = asked.size();
        QExposeEvent expose(QRegion(QRect(QPoint(), window.size())));
        QCoreApplication::sendEvent(&window, &expose);
        QCOMPARE(asked.size(), ++count);
        QEvent activate(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&window, &activate);
        QCOMPARE(asked.size(), ++count);
        QCursor::setPos(window.geometry().center());
        QTRY_COMPARE(QCursor::pos(), window.geometry().center());
        count = asked.size();
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(&window, &leave);
        QCOMPARE(asked.size(), count);
        QCursor::setPos(window.geometry().bottomRight() + QPoint(40, 40));
        QTRY_COMPARE(QCursor::pos(), window.geometry().bottomRight() + QPoint(40, 40));
        count = asked.size();
        QCoreApplication::sendEvent(&window, &leave);
        QCOMPARE(asked.size(), ++count);
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(&window, &enter);
        QCOMPARE(asked.size(), count);
        QCOMPARE(asked.last(), true);
        sampler.stopTracking();
        QVERIFY(!sampler.holdsPointer());
        QCOMPARE(asked.size(), count + 1);
        QCOMPARE(asked.last(), false);

        asked.clear();
        granted = false;
        QVERIFY(sampler.startTracking(&window, true));
        QCOMPARE(asked, QList<bool>{true});
        QTest::qWait(3 * ui::ScreenSampler::kRegrabInterval);
        QVERIFY(asked.size() > 1);
        QVERIFY(!sampler.holdsPointer());
        QVERIFY(sampler.regrabbing());
        granted = true;
        QTRY_VERIFY(sampler.holdsPointer());
        QVERIFY(!sampler.regrabbing());
        sampler.stopTracking();

        asked.clear();
        granted = false;
        QSignalSpy events(&sampler, &ui::ScreenSampler::pointerEvent);
        QVERIFY(sampler.startTracking(&window, false));
        QVERIFY(sampler.regrabbing());
        // Windows timers are coarse (about 16 ms): wait for the timer to stop, not a fixed time.
        QTRY_VERIFY_WITH_TIMEOUT(!sampler.regrabbing(),
                                 4 * ui::ScreenSampler::kRegrabTries * ui::ScreenSampler::kRegrabInterval);
        const qsizetype tries = asked.count(true);
        QVERIFY2(tries >= 1 + ui::ScreenSampler::kRegrabTries, qPrintable(QString::number(tries)));
        sentPosition(window, QPoint(-20, 200), QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        QCOMPARE(events.count(), 1);
        QVERIFY(sampler.tracking());
        QVERIFY(!sampler.holdsPointer());
        sampler.stopTracking();
    }
};

QTEST_MAIN(ColourPickerTest)
#include "colour_picker_tests.moc"
