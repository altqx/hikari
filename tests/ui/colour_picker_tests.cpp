// Y7: the colour picker's option, conversions, simple-picker recent colour
// and screen sampler against legacy ColorPicker.cpp, colorspace.cpp and
// EditBox.cpp at 20d647c4.

#include "colour_picker_controller.h"
#include "screen_sampler.h"
#include "settings_store.h"

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

// A window whose every pixel encodes its own position: red the column,
// green the row, blue 77.
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

} // namespace

class ColourPickerTest : public QObject {
    Q_OBJECT

private slots:
    // COLORPICKER_SWITCH_CLICKS (config.h:129, a Bool unset by default):
    // the box writes the option at each click (ColorPicker.cpp:568-570) and
    // a later run reads it (EditBox.cpp:865).
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
        // "Set default" puts the option back and the box follows.
        QSignalSpy changed(&picker, &ui::ColourPickerController::switchClicksChanged);
        again.resetAll();
        QVERIFY(!picker.switchClicks());
        QVERIFY(changed.count() >= 1);
    }

    // DialogColorPicker::AddRecent without a created picker
    // (ColorPicker.cpp:755-766): the option text is edited, not the list.
    void simplePickerRecentEditsTheOptionText()
    {
        ui::SettingsStore store;
        store.set("colourPicker.recentColours", QStringLiteral("&H000000FF& &H0000FF00& &H00FF0000&"));
        ui::ColourPickerController picker(store);
        picker.addRecentFromSimplePicker(rgba(0, 255, 0));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H0000FF00& &H000000FF& &H00FF0000&"));
        QCOMPARE(picker.recent().first().toMap().value(QStringLiteral("g")).toInt(), 255);
        // From an empty option the text keeps legacy's trailing space.
        store.set("colourPicker.recentColours", QString());
        picker.addRecentFromSimplePicker(rgba(1, 2, 3, 4));
        QCOMPARE(store.text("colourPicker.recentColours"), QStringLiteral("&H04030201& "));

        // A full option whose first colour comes again: its removal leaves a
        // leading space, so the text reaches 32 spaces and loses its last
        // colour (the list would have kept it).
        QStringList full;
        for (int i = 0; i < 32; ++i)
            full << QStringLiteral("&H000000%1&").arg(i, 2, 16, QLatin1Char('0')).toUpper();
        store.set("colourPicker.recentColours", full.join(QLatin1Char(' ')));
        picker.addRecentFromSimplePicker(rgba(0, 0, 0)); // "&H00000000&", the first one
        const QString text = store.text("colourPicker.recentColours");
        QCOMPARE(text.count(QLatin1Char(' ')), 31);
        QVERIFY(text.startsWith(QStringLiteral("&H00000000&  &H00000001&")));
        QVERIFY(!text.contains(QStringLiteral("&H0000001F&")));
        QCOMPARE(picker.recent().at(30).toMap().value(QStringLiteral("r")).toInt(), 0x1e);
        QCOMPARE(picker.recent().at(31).toMap().value(QStringLiteral("r")).toInt(), 0); // padded black

        // With a created picker (opened for a window) the list takes it, as
        // AddColor does, and the option is the list's text.
        picker.opened(QStringLiteral("1"));
        picker.addRecentFromSimplePicker(rgba(0, 0, 0x1e));
        QCOMPARE(picker.recent().first().toMap().value(QStringLiteral("b")).toInt(), 0x1e);
        QCOMPARE(store.text("colourPicker.recentColours"), picker.storeToString());
    }

    // The picker's fields through colorspace.cpp (the full fixtures are in
    // hikari_core_colour_space_tests).
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

    // Wayland clients cannot read the screen: the sampler goes through the
    // desktop's Screenshot portal, or reports itself unavailable without one
    // (the #175 working assumption) and samples nothing.
    void routesFollowThePlatform()
    {
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("xcb"), false, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("windows"), false, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("offscreen"), true, false).name, QStringLiteral("grab"));
        QCOMPARE(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, true).name, QStringLiteral("portal"));
        // X11 on XWayland: its root window shows no Wayland client.
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
        // Without the portal route, asking the portal answers with the reason.
        QSignalSpy failed(&sampler, &ui::ScreenSampler::portalFailed);
        sampler.setRoute({});
        sampler.pickFromPortal();
        QCOMPARE(failed.count(), 1);
        QVERIFY(!sampler.portalBusy());
    }

    // The route this session resolves: on Wayland the desktop's Screenshot
    // portal when it offers PickColor, otherwise unavailable with a reason;
    // elsewhere the screen grab.
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

    // DropFromScreenXY (ColorPicker.cpp:427-445): the 7x7 screen pixels
    // centred on the point, row by row, read from the screen itself (here a
    // window painting its own coordinates).
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

    // CaptureMouse: every mouse event of the window comes with its global
    // position; the dropper keeps them, the simple picker lets those over
    // itself through (OnLeaveWindow releases the capture there).
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
};

QTEST_MAIN(ColourPickerTest)
#include "colour_picker_tests.moc"
