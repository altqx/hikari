#include "settings_store.h"
#include "theme.h"

#include "hikari/application/audio_display.h"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStyleHints>
#include <QTextStream>
#include <QWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cmath>
#include <optional>
#include <set>
#include <utility>

#ifdef HIKARI_COLOUR_SCHEME_PORTAL
#include "portal_colour_scheme.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#endif

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::theme::Code;
using ui::theme::contrastRatio;

namespace {

QColor hex(QRgb rgb)
{
    return QColor::fromRgb(rgb);
}

ui::theme::Roles roles(Code code, const ui::theme::Choice &choice = {})
{
    return ui::theme::resolve(code, choice);
}

std::array<QColor, 4> surfaces(const ui::theme::Roles &r)
{
    return {r.background, r.panel, r.raised, r.field};
}

double lowest(const QColor &colour, const ui::theme::Roles &r)
{
    double out = 21;
    for (const QColor &s : surfaces(r))
        out = std::min(out, contrastRatio(colour, s));
    return out;
}

QString name(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}


#ifdef HIKARI_COLOUR_SCHEME_PORTAL
// xdg-desktop-portal's Settings: ReadOne (interface version 2) and SettingChanged.
class FakeSettingsPortal : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
    QVariant colourScheme = QVariant::fromValue(quint32(1));
    std::atomic<int> reads = 0;
    void change(quint32 value, const QString &nameSpace = QStringLiteral("org.freedesktop.appearance"),
                const QString &key = QStringLiteral("color-scheme"))
    {
        // QtDBus relays a signal emitted on the object's own thread only.
        if (QThread::currentThread() != thread()) {
            QMetaObject::invokeMethod(this, [=, this] { change(value, nameSpace, key); }, Qt::BlockingQueuedConnection);
            return;
        }
        if (nameSpace == QLatin1String("org.freedesktop.appearance") && key == QLatin1String("color-scheme"))
            colourScheme = QVariant::fromValue(value);
        emit SettingChanged(nameSpace, key, QDBusVariant(QVariant::fromValue(value)));
    }
public slots:
    QDBusVariant ReadOne(const QString &nameSpace, const QString &key)
    {
        ++reads;
        if (nameSpace == QLatin1String("org.freedesktop.appearance") && key == QLatin1String("color-scheme"))
            return QDBusVariant(colourScheme);
        return QDBusVariant(QVariant());
    }
signals:
    void SettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);
};

// A portal older than ReadOne: Read wraps the value in a second variant.
class FakeOldSettingsPortal : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public slots:
    QDBusVariant Read(const QString &, const QString &)
    {
        return QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant::fromValue(quint32(2)))));
    }
};

class FakeSlowSettingsPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public slots:
    QDBusVariant ReadOne(const QString &, const QString &)
    {
        setDelayedReply(true);
        const QDBusMessage reply =
            message().createReply(QVariant::fromValue(QDBusVariant(QVariant::fromValue(quint32(2)))));
        QTimer::singleShot(1500, this, [bus = connection(), reply] { bus.send(reply); });
        return {};
    }
};

// No service directories, so dbus-daemon never starts a real portal.
struct PrivateBus {
    QTemporaryDir dir;
    QProcess daemon;
    QString address;
    bool start()
    {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (program.isEmpty())
            return false;
        const QString config = dir.filePath(QStringLiteral("bus.conf"));
        QFile f(config);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                "<busconfig><type>session</type><listen>unix:dir=" + dir.path().toUtf8() + "</listen>"
                "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n");
        f.close();
        daemon.start(program, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                               QStringLiteral("--print-address=1")});
        if (!daemon.waitForStarted() || !daemon.waitForReadyRead(5000))
            return false;
        address = QString::fromUtf8(daemon.readLine()).trimmed();
        return !address.isEmpty();
    }
    ~PrivateBus()
    {
        for (const char *name : {"k2-portal-server", "k2-portal-client", "k2-portal-client2"})
            QDBusConnection::disconnectFromBus(QLatin1String(name));
        daemon.kill();
        daemon.waitForFinished();
    }
};
#endif

}

class ThemeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Qt Quick Controls take a style only before any engine loads them.
        ui::theme::chooseControlsStyle();
    }

    void init()
    {
        ui::theme::useSettings(nullptr);
        ui::theme::endPreview();
        ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
    }

    void rolesAreTheSpecTokens()
    {
        const auto dark = roles(Code::Dark);
        QCOMPARE(surfaces(dark), (std::array{hex(0x171B20), hex(0x20262D), hex(0x29313A), hex(0x171D24)}));
        QCOMPARE(dark.text, hex(0xE8EDF2));
        QCOMPARE(dark.muted, hex(0xA5B1BD));
        QCOMPARE(dark.line, hex(0x414B57));
        QCOMPARE(dark.accent, hex(0x9CDBC9));
        QCOMPARE(dark.onAccent, hex(0x102C24));
        QCOMPARE(dark.select, hex(0x304C47));
        QCOMPARE(dark.focus, dark.text);
        QCOMPARE(dark.danger, hex(0xFFADAD));
        const auto light = roles(Code::Light);
        QCOMPARE(surfaces(light), (std::array{hex(0xE5E9EC), hex(0xF9FAFB), hex(0xEDF0F3), hex(0xFFFFFF)}));
        QCOMPARE(light.text, hex(0x202832));
        QCOMPARE(light.muted, hex(0x526170));
        QCOMPARE(light.line, hex(0xAAB5BE));
        QCOMPARE(light.accent, hex(0x145C4C));
        QCOMPARE(light.onAccent, hex(0xFFFFFF));
        QCOMPARE(light.select, hex(0xD6EBE4));
        QCOMPARE(light.focus, light.text);
        QCOMPARE(light.danger, hex(0xA51F31));
        const auto black = roles(Code::HighContrastBlack, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
        QCOMPARE(surfaces(black), (std::array{hex(0x000000), hex(0x080808), hex(0x151515), hex(0x000000)}));
        QCOMPARE(black.text, hex(0xFFFFFF));
        QCOMPARE(black.muted, hex(0xEEEEEE));
        QCOMPARE(black.line, hex(0xFFFFFF));
        QCOMPARE(black.accent, hex(0xFFFF00));
        QCOMPARE(black.onAccent, hex(0x000000));
        QCOMPARE(black.select, hex(0x253F60));
        QCOMPARE(black.focus, black.text);
        QCOMPARE(black.danger, hex(0xFFADAD));
        QCOMPARE(light.success, hex(0x008000));
        QCOMPARE(dark.success, hex(0x4DBA4D));
        QCOMPARE(light.control, hex(0x76828D));
        QCOMPARE(dark.control, hex(0x7A8691));
        QCOMPARE(black.control, black.line);
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            for (std::size_t i = 0; i < ui::theme::kRoleNames.size(); ++i)
                QVERIFY2(ui::theme::role(r, i).isValid(), ui::theme::kRoleNames[i]);
        }
    }

    void rolesMeetContrast()
    {
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, defaults);
            const QString theme = ui::theme::codeName(code);
            const bool hc = ui::theme::isHighContrast(code);
            const double textMinimum = hc ? 7.0 : 4.5;
            for (const auto &[role, colour] : {std::pair{"text", r.text}, std::pair{"muted", r.muted},
                                               std::pair{"danger", r.danger}, std::pair{"warning", r.warning}})
                QVERIFY2(lowest(colour, r) >= textMinimum, qPrintable(theme + QLatin1Char(' ') + QLatin1String(role)));
            QVERIFY2(lowest(r.accent, r) >= (hc ? 7.0 : 4.5), qPrintable(theme));
            QVERIFY2(lowest(r.focus, r) >= 3.0, qPrintable(theme));
            QVERIFY2(lowest(r.disabled, r) >= 3.0, qPrintable(theme));
            QVERIFY2(lowest(r.control, r) >= 3.0, qPrintable(theme + QStringLiteral(" control")));
            if (code != Code::Light)
                QVERIFY2(lowest(r.success, r) >= textMinimum, qPrintable(theme + QStringLiteral(" success")));
            QVERIFY2(contrastRatio(r.onAccent, r.accent) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.text, r.select) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.disabled, r.text) >= 2.0, qPrintable(theme));
            if (hc) {
                QVERIFY2(lowest(r.success, r) >= 7.0, qPrintable(theme));
                QVERIFY2(lowest(r.line, r) >= 7.0, qPrintable(theme));
            }
        }
    }

    void focusIsTheTextColour()
    {
        const auto check = [](const ui::theme::Roles &r, const QString &what) {
            QVERIFY2(r.focus == r.text, qPrintable(what + QStringLiteral(": focus ") + name(r.focus)));
            QVERIFY2(r.focus != r.accent, qPrintable(what));
            QVERIFY2(r.focus != r.select, qPrintable(what));
            QVERIFY2(contrastRatio(r.focus, r.select) >= 4.5, qPrintable(what));
            QVERIFY2(lowest(r.focus, r) >= 4.5, qPrintable(what));
        };
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes)
            check(roles(code, defaults), ui::theme::codeName(code));
        for (const bool dark : {false, true})
            for (const auto &a : ui::theme::accents(dark)) {
                ui::theme::Choice choice;
                (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                check(roles(dark ? Code::Dark : Code::Light, choice),
                      QLatin1String(dark ? "dark " : "light ") + QLatin1String(a.key));
            }
        const QVariantMap picks{{QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FFD000")},
                                {QStringLiteral("appearance.highContrastWhite.text"), QStringLiteral("#300060")}};
        const auto picked = ui::theme::choiceFrom([&](const char *id) { return picks.value(QLatin1String(id)); });
        QCOMPARE(roles(Code::HighContrastBlack, picked).focus, hex(0xFFD000));
        QCOMPARE(roles(Code::HighContrastWhite, picked).focus, hex(0x300060));
    }

    void accentPresetsMeetContrast()
    {
        const QStringList keys{"red", "orange", "gold", "green", "blue", "purple", "pink"};
        for (const bool dark : {false, true}) {
            const auto list = ui::theme::accents(dark);
            QCOMPARE(list.size(), std::size_t(7));
            QStringList seen;
            for (const auto &a : list)
                seen << QLatin1String(a.key);
            QCOMPARE(seen, keys);
            QCOMPARE(QLatin1String(list[3].key), QLatin1String(ui::theme::kDefaultAccent));
            const Code code = dark ? Code::Dark : Code::Light;
            for (const auto &a : list) {
                ui::theme::Choice choice;
                (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                const auto r = roles(code, choice);
                const QString what = QLatin1String(dark ? "dark " : "light ") + QLatin1String(a.key);
                QCOMPARE(r.accent, a.accent);
                QCOMPARE(r.select, a.select);
                QVERIFY2(lowest(a.accent, r) >= 4.5, qPrintable(what));
                QVERIFY2(contrastRatio(a.onAccent, a.accent) >= 4.5, qPrintable(what));
                QVERIFY2(contrastRatio(r.text, a.select) >= 4.5, qPrintable(what));
                QVERIFY2(contrastRatio(a.select, r.panel) < 2.0, qPrintable(what));
                QCOMPARE(r.text, roles(code).text);
                QCOMPARE(surfaces(r), surfaces(roles(code)));
            }
        }
        QCOMPARE(ui::theme::accent(false, QStringLiteral("green")).accent, hex(0x145C4C));
        QCOMPARE(ui::theme::accent(true, QStringLiteral("green")).accent, hex(0x9CDBC9));
        QCOMPARE(QLatin1String(ui::theme::accent(true, QStringLiteral("teal")).key), QLatin1String("green"));
    }

    void eachModeKeepsItsAccent()
    {
        ui::theme::Choice choice;
        choice.lightAccent = QStringLiteral("blue");
        choice.darkAccent = QStringLiteral("pink");
        QCOMPARE(roles(Code::Light, choice).accent, ui::theme::accent(false, QStringLiteral("blue")).accent);
        QCOMPARE(roles(Code::Dark, choice).accent, ui::theme::accent(true, QStringLiteral("pink")).accent);
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        choice.picks = defaults.picks;
        QCOMPARE(roles(Code::HighContrastBlack, choice).accent, hex(0xFFFF00));
        QCOMPARE(roles(Code::HighContrastWhite, choice).accent, hex(0x0037B3));
    }

    void followingTheSystemSwitchesLightAndDarkOnly()
    {
        using S = Qt::ColorScheme;
        const auto shown = [](Code chosen, bool follow, S scheme) {
            ui::theme::Choice choice;
            choice.theme = chosen;
            choice.followSystem = follow;
            return ui::theme::effective(choice, scheme);
        };
        QCOMPARE(shown(Code::Dark, true, S::Light), Code::Light);
        QCOMPARE(shown(Code::Light, true, S::Dark), Code::Dark);
        QCOMPARE(shown(Code::Light, true, S::Light), Code::Light);
        QCOMPARE(shown(Code::HighContrastBlack, true, S::Light), Code::HighContrastWhite);
        QCOMPARE(shown(Code::HighContrastWhite, true, S::Dark), Code::HighContrastBlack);
        QCOMPARE(shown(Code::Dark, true, S::Unknown), Code::Dark);
        QCOMPARE(shown(Code::Dark, false, S::Light), Code::Dark);
        QCOMPARE(shown(Code::HighContrastBlack, false, S::Light), Code::HighContrastBlack);
        ui::SettingsStore store;
        ui::theme::useSettings(&store);
        int changes = 0;
        QObject context;
        ui::theme::onChanged(&context, [&] { ++changes; });
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 1);
        ui::theme::forceSystemScheme(S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(changes, 2);
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), false);
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::useSettings(nullptr);
    }

    void followingTheSystemReadsTheSettingsPortal()
    {
#ifndef HIKARI_COLOUR_SCHEME_PORTAL
        QSKIP("the XDG settings portal is read on Linux only");
#else
        using S = Qt::ColorScheme;
        using ui::theme::PortalColourScheme;
        QCOMPARE(PortalColourScheme::fromValue(QVariant::fromValue(quint32(0))), S::Unknown);
        QCOMPARE(PortalColourScheme::fromValue(QVariant::fromValue(quint32(1))), S::Dark);
        QCOMPARE(PortalColourScheme::fromValue(QVariant::fromValue(quint32(2))), S::Light);
        QCOMPARE(PortalColourScheme::fromValue(QVariant::fromValue(quint32(3))), S::Unknown);
        QCOMPARE(PortalColourScheme::fromValue(QVariant(QStringLiteral("1"))), S::Unknown);
        QCOMPARE(PortalColourScheme::fromValue(QVariant::fromValue(QDBusVariant(QVariant::fromValue(quint32(2))))), S::Light);

        PrivateBus bus;
        if (!bus.start())
            QSKIP("dbus-daemon is not installed");
        QDBusConnection server = QDBusConnection::connectToBus(bus.address, QStringLiteral("k2-portal-server"));
        QDBusConnection client = QDBusConnection::connectToBus(bus.address, QStringLiteral("k2-portal-client"));
        QVERIFY(server.isConnected() && client.isConnected());
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop");
        const QString service = QStringLiteral("org.freedesktop.portal.Desktop");

        {
            PortalColourScheme none(client);
            QCOMPARE(none.scheme(), S::Unknown);
        }

        // The fake answers on its own thread: the reader's blocking call holds this one.
        QThread portalThread;
        portalThread.start();
        QList<QObject *> fakes;
        auto stop = qScopeGuard([&] {
            server.unregisterObject(path);
            for (QObject *fake : std::as_const(fakes))
                QMetaObject::invokeMethod(fake, [fake] { delete fake; }, Qt::BlockingQueuedConnection);
            portalThread.quit();
            portalThread.wait();
        });
        auto *fake = new FakeSettingsPortal;
        fake->moveToThread(&portalThread);
        fakes << fake;
        FakeSettingsPortal &portal = *fake;
        // QtDBus makes the signal relay a child of the object, so register it on the object's thread.
        const auto registerOnItsThread = [&](QObject *object, QDBusConnection::RegisterOptions options) {
            bool registered = false;
            QMetaObject::invokeMethod(
                object, [&] { registered = server.registerObject(path, object, options); }, Qt::BlockingQueuedConnection);
            return registered;
        };
        PortalColourScheme early(client);
        QCOMPARE(early.scheme(), S::Unknown);
        QVERIFY(registerOnItsThread(&portal, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(server.registerService(service));
        QTRY_COMPARE(early.scheme(), S::Dark);
        QCOMPARE(portal.reads.load(), 1);
        PortalColourScheme scheme(client);
        QCOMPARE(portal.reads.load(), 2);
        QCOMPARE(scheme.scheme(), S::Dark);

        // The offscreen platform theme, like the generic one, reports no colour scheme.
        ui::theme::forceSystemScheme(std::nullopt);
        if (QGuiApplication::styleHints()->colorScheme() != S::Unknown)
            QSKIP("this platform's theme reports a colour scheme of its own");
        ui::SettingsStore store;
        ui::theme::useSettings(&store);
        ui::theme::followPortal(&scheme);
        auto restore = qScopeGuard([] {
            ui::theme::followPortal(nullptr);
            ui::theme::forcePlatformScheme(std::nullopt);
            ui::theme::useSettings(nullptr);
            ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
        });
        QVERIFY(ui::theme::systemScheme() == S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        int changes = 0;
        QObject context;
        ui::theme::onChanged(&context, [&] { ++changes; });

        // The portal's color-scheme: 1 prefer-dark, 2 prefer-light, 0 no preference.
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 1);
        portal.change(1);
        QTRY_COMPARE(ui::theme::current().code, Code::Dark);
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 3);
        portal.change(1, QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme"));
        portal.change(1, QStringLiteral("org.freedesktop.appearance"), QStringLiteral("accent-color"));
        QTest::qWait(100);
        QCOMPARE(ui::theme::current().code, Code::Light);
        portal.change(0);
        QTRY_COMPARE(ui::theme::current().code, Code::Dark);
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);

        ui::theme::forcePlatformScheme(S::Dark);
        QCOMPARE(ui::theme::systemScheme(), S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::forcePlatformScheme(S::Unknown);
        QCOMPARE(ui::theme::current().code, Code::Light);
        ui::theme::forcePlatformScheme(std::nullopt);
        QCOMPARE(ui::theme::current().code, Code::Light);

        store.setValue(QLatin1String(ui::theme::kThemeSetting), ui::theme::codeName(Code::HighContrastBlack));
        QCOMPARE(ui::theme::current().code, Code::HighContrastWhite);
        store.setValue(QLatin1String(ui::theme::kThemeSetting), ui::theme::codeName(Code::Dark));
        QCOMPARE(ui::theme::current().code, Code::Light);

        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), false);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        portal.change(1);
        portal.change(2);
        QTest::qWait(100);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), true);
        QCOMPARE(ui::theme::current().code, Code::Light);

        ui::theme::followPortal(nullptr);
        QCOMPARE(ui::theme::current().code, Code::Dark);

        server.unregisterObject(path);
        auto *old = new FakeOldSettingsPortal;
        old->moveToThread(&portalThread);
        fakes << old;
        QVERIFY(registerOnItsThread(old, QDBusConnection::ExportAllSlots));
        QDBusConnection client2 = QDBusConnection::connectToBus(bus.address, QStringLiteral("k2-portal-client2"));
        QCOMPARE(PortalColourScheme(client2).scheme(), S::Light);

        server.unregisterObject(path);
        auto *slow = new FakeSlowSettingsPortal;
        slow->moveToThread(&portalThread);
        fakes << slow;
        QVERIFY(registerOnItsThread(slow, QDBusConnection::ExportAllSlots));
        PortalColourScheme late(client2);
        QCOMPARE(late.scheme(), S::Unknown);
        ui::theme::followPortal(&late);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QTRY_COMPARE_WITH_TIMEOUT(late.scheme(), S::Light, 5000);
        QCOMPARE(ui::theme::current().code, Code::Light);
        ui::theme::followPortal(nullptr);
#endif
    }

    void highContrastPickersSetThreeRoles()
    {
        QVariantMap values{{QStringLiteral("appearance.highContrastBlack.accent"), QStringLiteral("#000080")},
                           {QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FFD000")},
                           {QStringLiteral("appearance.highContrastBlack.border"), QStringLiteral("#00FF00")},
                           {QStringLiteral("appearance.highContrastWhite.accent"), QStringLiteral("not a colour")}};
        const auto choice = ui::theme::choiceFrom([&](const char *id) { return values.value(QLatin1String(id)); });
        const auto base = roles(Code::HighContrastBlack, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
        const auto black = roles(Code::HighContrastBlack, choice);
        QCOMPARE(black.accent, hex(0x000080));
        QCOMPARE(black.text, hex(0xFFD000));
        QCOMPARE(black.line, hex(0x00FF00));
        QCOMPARE(black.onAccent, hex(0xFFFFFF));
        QCOMPARE(black.background, base.background);
        QCOMPARE(black.select, base.select);
        QCOMPARE(black.focus, hex(0xFFD000));
        QCOMPARE(black.disabled, base.disabled);
        QCOMPARE(roles(Code::HighContrastWhite, choice).accent, hex(0x0037B3));
        QCOMPARE(roles(Code::HighContrastWhite, choice).text, hex(0x000000));
        QCOMPARE(roles(Code::Dark, choice), roles(Code::Dark));
        QCOMPARE(QLatin1String(ui::theme::pickSetting(Code::HighContrastWhite, ui::theme::Pick::Border)),
                 QLatin1String("appearance.highContrastWhite.border"));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastBlack, ui::theme::Pick::Accent), hex(0xFFFF00));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastWhite, ui::theme::Pick::Text), hex(0x000000));
    }

    void choiceDefaults()
    {
        const auto choice = ui::theme::choiceFrom([](const char *id) {
            return QLatin1String(id) == QLatin1String("appearance.theme") ? QVariant(QStringLiteral("LightSentro"))
                                                                         : QVariant(QStringLiteral("x"));
        });
        QCOMPARE(choice.theme, Code::Dark);
        QCOMPARE(choice.lightAccent, QStringLiteral("green"));
        QCOMPARE(choice.darkAccent, QStringLiteral("green"));
        QCOMPARE(ui::theme::choiceFrom([](const char *) { return QVariant(); }).followSystem, true);
        for (const auto code : ui::theme::kCodes)
            QCOMPARE(ui::theme::codeFromName(ui::theme::codeName(code)), std::optional(code));
        QVERIFY(!ui::theme::codeFromName(QStringLiteral("highContrast")));
    }

    void contentColoursAreFixedPerTheme()
    {
        const application::AudioDisplayOptions legacy;
        const auto dark = ui::theme::content(Code::Dark, roles(Code::Dark));
        auto expected = legacy;
        expected.selectionBackground = expected.selectionModified = 0x379CDBC9;
        expected.waveformSelected = 0xFF9CDBC9;
        auto actual = legacy;
        ui::theme::applyAudioColours(actual, dark.audio);
        QCOMPARE(actual.background, 0xFF36393Eu);
        QCOMPARE(actual.spectrumEcho, 0xFF674FD7u);
        using O = application::AudioDisplayOptions;
        for (const auto member : {&O::background, &O::lineStart, &O::lineEnd, &O::inactiveBoundary, &O::cursor,
                                  &O::secondBoundaries, &O::keyframe, &O::selectionBackground, &O::selectionModified,
                                  &O::inactiveBackground, &O::waveform, &O::waveformInactive, &O::waveformModified,
                                  &O::waveformSelected, &O::timescaleBackground, &O::timescaleText, &O::spectrumBackground,
                                  &O::spectrumEcho, &O::spectrumInner, &O::lineBoundaryMark, &O::syllableBoundaries,
                                  &O::syllableText})
            QCOMPARE(actual.*member, expected.*member);
        QCOMPARE(dark.comparison, (std::array{hex(0x2700FF), hex(0x272B32), hex(0x3A3E45), hex(0x003176), hex(0x3662A1)}));
        QCOMPARE(dark.spellcheck, hex(0x940000));
        const auto light = ui::theme::content(Code::Light, roles(Code::Light));
        QCOMPARE(light.comparison, (std::array{hex(0xFFFFFF), hex(0xFF000C), hex(0xB7AC00), hex(0x9C0000), hex(0x817900)}));
        QCOMPARE(light.spellcheck, hex(0xFF6968));
        QCOMPARE(dark.grammar, hex(0x6EA8FE));
        QCOMPARE(light.grammar, hex(0x1A56DB));
        for (const auto code : {Code::Dark, Code::Light, Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            const auto c = ui::theme::content(code, r);
            QVERIFY2(contrastRatio(c.grammar, r.field) >= 3.0, qPrintable(ui::theme::codeName(code)));
            QVERIFY(c.grammar != c.spellcheck);
        }
        QCOMPARE(ui::theme::content(Code::HighContrastWhite, roles(Code::HighContrastWhite)).grammar, hex(0x0000C8));
        QCOMPARE(ui::theme::content(Code::HighContrastBlack, roles(Code::HighContrastBlack)).grammar, hex(0xFFFF00));
        QCOMPARE(dark.syntax, (std::array{roles(Code::Dark).text, hex(0x8791FD), hex(0x00C3FF), hex(0x00C3FF),
                                          hex(0x0076FF), hex(0xADABAB), hex(0xBDB76B), hex(0xFB4544), hex(0x00C3FF),
                                          hex(0x8D80D3), hex(0x11A243)}));
        QCOMPARE(light.syntax, (std::array{roles(Code::Light).text, hex(0x7E3B00), hex(0x787600), hex(0x787600),
                                           hex(0x34C200), hex(0x817F7F), hex(0xBDB76B), hex(0xFB4544), hex(0x00C3FF),
                                           hex(0x8D80D3), hex(0x11A243)}));
        for (const auto code : {Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            const auto c = ui::theme::content(code, r);
            QCOMPARE(c.syntax[0], r.text);
            for (const QColor &colour : c.syntax)
                QVERIFY2(contrastRatio(colour, r.field) >= 4.5,
                         qPrintable(ui::theme::codeName(code) + QLatin1Char(' ') + colour.name()));
        }
        ui::theme::Choice choice;
        choice.darkAccent = QStringLiteral("blue");
        const auto blue = ui::theme::content(Code::Dark, roles(Code::Dark, choice));
        const QColor accent = ui::theme::accent(true, QStringLiteral("blue")).accent;
        QCOMPARE(blue.audio.waveformSelected, accent.rgba());
        QCOMPARE(blue.audio.selectionBackground, (accent.rgba() & 0x00FFFFFFu) | 0x37000000u);
        QCOMPARE(blue.comparisonSelection, QColor(accent.red(), accent.green(), accent.blue(), 75));
        QCOMPARE(blue.audio.background, dark.audio.background);
        QCOMPARE(blue.audio.cursor, dark.audio.cursor);
        QCOMPARE(blue.comparison, dark.comparison);
        for (const auto code : {Code::Light, Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto c = ui::theme::content(code, roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); })));
            const double minimum = ui::theme::isHighContrast(code) ? 4.5 : 3.0;
            const auto colour = [](std::uint32_t argb) { return QColor::fromRgba(argb); };
            QVERIFY2(contrastRatio(colour(c.audio.waveform), colour(c.audio.background)) >= minimum,
                     qPrintable(ui::theme::codeName(code)));
            QVERIFY2(contrastRatio(colour(c.audio.timescaleText), colour(c.audio.timescaleBackground)) >= 4.5,
                     qPrintable(ui::theme::codeName(code)));
            QVERIFY2(contrastRatio(colour(c.audio.cursor), colour(c.audio.background)) >= 3.0,
                     qPrintable(ui::theme::codeName(code)));
        }
    }

    void profilePreviewAndPalette()
    {
        ui::SettingsStore store;
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(before);
        });
        ui::theme::useSettings(&store);
        QObject context;
        int changes = 0;
        ui::theme::onChanged(&context, [&] { ++changes; });
        const auto window = [] { return QGuiApplication::palette().color(QPalette::Window); };
        QCOMPARE(window(), hex(0x20262D));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Highlight), hex(0x9CDBC9));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText), hex(0x75818D));
        store.setValue(QStringLiteral("audio.spectrumOn"), true);
        QCOMPARE(changes, 0);
        store.setValue(QStringLiteral("appearance.darkAccent"), QStringLiteral("gold"));
        QCOMPARE(changes, 1);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Accent), ui::theme::accent(true, QStringLiteral("gold")).accent);
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("light")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(window(), hex(0xF9FAFB));
        QCOMPARE(store.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("dark"));
        ui::theme::endPreview();
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(window(), hex(0x20262D));
        QCOMPARE(ui::theme::current().roles.accent, ui::theme::accent(true, QStringLiteral("gold")).accent);
        ui::theme::useSettings(nullptr);
        QCOMPARE(ui::theme::current().roles.accent, hex(0x9CDBC9));
    }

    void themeSingletonExposesTheRoles()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import Hikari.Ui
QtObject {
    property color accent: Theme.accent
    property color panel: Theme.panel
    property string code: Theme.code
    property bool dark: Theme.dark
    property bool highContrast: Theme.highContrast
    property var themes: Theme.themes()
    property var accents: Theme.accents(false)
    property var picks: Theme.picks("highContrastWhite")
    property var lightRoles: Theme.rolesOf("light", {"appearance.lightAccent": "purple"})
}
)", QUrl(QStringLiteral("qrc:/k2-test.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("accent").value<QColor>(), hex(0x9CDBC9));
        QCOMPARE(object->property("code").toString(), QStringLiteral("dark"));
        QVERIFY(object->property("dark").toBool());
        QVERIFY(!object->property("highContrast").toBool());
        const auto themes = object->property("themes").toList();
        QCOMPARE(themes.size(), 4);
        QCOMPARE(themes[2].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("High contrast white"));
        const auto accents = object->property("accents").toList();
        QCOMPARE(accents.size(), 7);
        QCOMPARE(accents[3].toMap().value(QStringLiteral("accent")).value<QColor>(), hex(0x145C4C));
        QVERIFY(accents[3].toMap().value(QStringLiteral("contrast")).toDouble() > 6.4);
        const auto picks = object->property("picks").toList();
        QCOMPARE(picks.size(), 3);
        QCOMPARE(picks[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Text and icons"));
        QCOMPARE(picks[0].toMap().value(QStringLiteral("defaultColour")).toString(), QStringLiteral("#0037B3"));
        QCOMPARE(object->property("lightRoles").toMap().value(QStringLiteral("accent")).value<QColor>(),
                 ui::theme::accent(false, QStringLiteral("purple")).accent);
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(object->property("accent").value<QColor>(), hex(0xFFFF00));
        QCOMPARE(object->property("panel").value<QColor>(), hex(0x080808));
        QVERIFY(object->property("highContrast").toBool());
    }

    void controlsOutlineInTheBoundaryColour()
    {
        QCOMPARE(QQuickStyle::name(), QStringLiteral("HikariStyle"));
        ui::SettingsStore store;
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(before);
        });
        ui::theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
Window {
    width: 400; height: 400
    property var outlines: [button.background.border.color, toolButton.background.border.color,
        field.background.border.color, area.background.border.color, spin.background.border.color,
        combo.background.border.color, check.indicator.border.color, radio.indicator.border.color,
        group.background.border.color, frame.background.border.color, menu.background.border.color,
        tab.background.border.color, tabBar.background.children[0].color, slider.background.border.color,
        slider.handle.border.color, off.background.border.color, offField.background.border.color]
    // A control the style leaves alone is Fusion's (the qmldir's fallback),
    // not Basic's: Fusion's switch indicator is its SwitchIndicator item.
    property string switchIndicator: String(toggle.indicator)
    Column {
        Button { id: button; text: "B" }
        ToolButton { id: toolButton; text: "T" }
        TextField { id: field }
        TextArea { id: area }
        SpinBox { id: spin }
        ComboBox { id: combo; model: ["a"] }
        CheckBox { id: check; text: "c" }
        RadioButton { id: radio; text: "r" }
        GroupBox { id: group; title: "g" }
        Frame { id: frame }
        TabBar { id: tabBar; TabButton { id: tab; text: "t" } }
        Menu { id: menu; objectName: "menu" }
        Slider { id: slider }
        Switch { id: toggle }
        Button { id: off; text: "off"; enabled: false }
        TextField { id: offField; enabled: false }
    }
}
)", QUrl(QStringLiteral("qrc:/k2-controls.qml")));
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY2(window->property("switchIndicator").toString().startsWith(QStringLiteral("SwitchIndicator")),
                 qPrintable(window->property("switchIndicator").toString()));
        for (const Code code : ui::theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(code));
            const ui::theme::Roles &r = ui::theme::current().roles;
            // The controls take a new application palette an event loop pass later; a closed menu only when it opens.
            QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("menu")), "open");
            // MUST KILL: expected picks the operable controls by their index in outlines (0-7, 13, 14)
            const auto expected = [&](qsizetype i) { return (i < 8 || i == 13 || i == 14) ? r.control : r.line; };
            const auto drawn = [&] {
                const auto outlines = window->property("outlines").toList();
                for (qsizetype i = 0; i < outlines.size(); ++i)
                    if (outlines[i].value<QColor>() != expected(i))
                        return false;
                return true;
            };
            QTRY_VERIFY_WITH_TIMEOUT(drawn(), 1000);
            const auto outlines = window->property("outlines").toList();
            QCOMPARE(outlines.size(), 17);
            for (qsizetype i = 0; i < outlines.size(); ++i)
                QVERIFY2(outlines[i].value<QColor>() == expected(i),
                         qPrintable(QStringLiteral("%1: control %2 outline %3, expected %4").arg(ui::theme::codeName(code)).arg(i)
                                        .arg(name(outlines[i].value<QColor>()), name(expected(i)))));
            QVERIFY2(lowest(r.control, r) >= 3, qPrintable(ui::theme::codeName(code)));
            if (!ui::theme::isHighContrast(code))
                QVERIFY(r.control != r.line);
            // Fusion's own outline is the window colour darkened 140%.
            const QColor fusion = r.panel.darker(140);
            if (code == Code::Dark || code == Code::HighContrastBlack)
                QVERIFY(contrastRatio(fusion, r.panel) < 1.2);
            if (ui::theme::isHighContrast(code))
                QVERIFY(lowest(r.line, r) >= 7);
        }
        store.setValue(QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack"));
        store.setValue(QString::fromLatin1(ui::theme::pickSetting(Code::HighContrastBlack, ui::theme::Pick::Border)),
                       QStringLiteral("#00FF00"));
        QTRY_COMPARE(window->property("outlines").toList().front().value<QColor>(), hex(0x00FF00));
    }

    void comboBoxFieldCarriesTheBoxName()
    {
        QCOMPARE(QQuickStyle::name(), QStringLiteral("HikariStyle"));
        const bool wasActive = QAccessible::isActive();
        QAccessible::setActive(true);
        auto restore = qScopeGuard([&] { QAccessible::setActive(wasActive); });
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
Window {
    width: 400; height: 200
    property string choiceName: "Which lines"
    Column {
        ComboBox { objectName: "choice"; model: ["All lines", "Selected lines"]; Accessible.name: choiceName }
        ComboBox { objectName: "editable"; editable: true; model: ["one"]; Accessible.name: "Search text:" }
    }
}
)", QUrl(QStringLiteral("qrc:/k1-combo-names.qml")));
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        const auto field = [&](const char *box) -> QAccessibleInterface * {
            auto *combo = window->findChild<QQuickItem *>(QLatin1String(box));
            if (!combo)
                return nullptr;
            auto *content = combo->property("contentItem").value<QQuickItem *>();
            return content ? QAccessible::queryAccessibleInterface(content) : nullptr;
        };
        for (const auto &[box, name] : {std::pair{"choice", "Which lines"}, std::pair{"editable", "Search text:"}}) {
            QAccessibleInterface *edit = field(box);
            QVERIFY2(edit, box);
            QCOMPARE(edit->role(), QAccessible::EditableText);
            QCOMPARE(edit->text(QAccessible::Name), QString::fromLatin1(name));
            QAccessibleInterface *combo = edit->parent();
            QVERIFY(combo);
            QCOMPARE(combo->role(), QAccessible::ComboBox);
            QCOMPARE(combo->text(QAccessible::Name), QString::fromLatin1(name));
        }
        window->setProperty("choiceName", QStringLiteral("Lines"));
        QCOMPARE(field("choice")->text(QAccessible::Name), QStringLiteral("Lines"));
    }

    void keyboardFocusRingInTheTextColour()
    {
        ui::SettingsStore store;
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(before);
        });
        ui::theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
Window {
    width: 400; height: 900; visible: true
    property var controls: [button, toolButton, combo, check, radio, field, area, spin, tab, slider]
    property var outlines: [button.background.border.color, toolButton.background.border.color,
        combo.background.border.color, check.indicator.border.color, radio.indicator.border.color]
    property color defaultOutline: primary.background.border.color
    property color fieldOutline: field.background.border.color
    property color paletteText: button.palette.windowText
    property Item primary: primary
    Column {
        x: 40; y: 20; spacing: 16
        Button { id: button; text: "B" }
        ToolButton { id: toolButton; text: "T" }
        ComboBox { id: combo; model: ["a"] }
        CheckBox { id: check; text: "c" }
        RadioButton { id: radio; text: "r" }
        TextField { id: field }
        TextArea { id: area; text: "a" }
        SpinBox { id: spin }
        TabBar { width: 200; TabButton { id: tab; text: "t" } TabButton { text: "u" } }
        Slider { id: slider }
        Button { id: primary; text: "P"; highlighted: true }
    }
}
)", QUrl(QStringLiteral("qrc:/k2-focus.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.get());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QList<QQuickItem *> controls;
        for (const QVariant &c : object->property("controls").toList())
            controls.append(c.value<QQuickItem *>());
        QCOMPARE(controls.size(), 10);
        enum { Field = 5, Tab = 8, Slider = 9 };
        const auto ring = [&](qsizetype i) { return controls[i]->findChild<QQuickItem *>(QStringLiteral("focusRing")); };
        for (qsizetype i = 0; i < controls.size(); ++i)
            QVERIFY2(ring(i), qPrintable(QStringLiteral("control %1 has no focus ring").arg(i)));
        const auto outline = [&](qsizetype i) { return object->property("outlines").toList()[i].value<QColor>(); };
        const auto colour = [&](const char *name) { return object->property(name).value<QColor>(); };
        const auto ringColour = [&](qsizetype i) {
            return ring(i)->property("border").value<QObject *>()->property("color").value<QColor>();
        };
        const auto sceneRect = [](const QQuickItem *item) {
            return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        };
        auto *primary = object->property("primary").value<QQuickItem *>();
        const auto tabTo = [&](QQuickItem *control) {
            // MUST KILL: tabTo first gives primary mouse focus so each Tab focus is a fresh focus change
            primary->forceActiveFocus(Qt::MouseFocusReason);
            control->forceActiveFocus(Qt::TabFocusReason);
        };
        const auto check = [&](const ui::theme::Roles &r, const QString &what) {
            for (qsizetype i = 0; i < controls.size(); ++i) {
                const QString where = QStringLiteral("%1: control %2").arg(what).arg(i);
                tabTo(controls[i]);
                QTRY_VERIFY2(ring(i)->isVisible(), qPrintable(where));
                for (qsizetype j = 0; j < controls.size(); ++j)
                    if (j != i)
                        QTRY_VERIFY2(!ring(j)->isVisible(), qPrintable(where + QStringLiteral(" and %1").arg(j)));
                // The palette reaches the controls an event loop pass later.
                QTRY_VERIFY2(ringColour(i) == r.text,
                             qPrintable(where + QStringLiteral(" ring %1, text %2").arg(name(ringColour(i)), name(r.text))));
                QCOMPARE(ringColour(i), r.focus);
                QVERIFY2(ringColour(i) != r.accent && ringColour(i) != r.select, qPrintable(where));
                QCOMPARE(ring(i)->property("border").value<QObject *>()->property("width").toInt(), 1);
                const QRectF at = sceneRect(ring(i));
                if (i == Tab) {
                    auto *background = controls[i]->property("background").value<QQuickItem *>();
                    QVERIFY2(sceneRect(controls[i]).contains(at), qPrintable(where));
                    QCOMPARE(at, sceneRect(background).adjusted(1, 1, -1, -1));
                } else {
                    QCOMPARE(at, sceneRect(i == Slider ? controls[i]->property("handle").value<QQuickItem *>()
                                                       : controls[i]).adjusted(-1, -1, 1, 1));
                }
                const QImage shot = window->grabWindow();
                const qreal dpr = window->effectiveDevicePixelRatio();
                const auto pixelAt = [&](qreal x) {
                    return QPoint(int(std::floor((at.left() + x) * dpr)), int(std::floor((at.top() + at.height() / 2) * dpr)));
                };
                QVERIFY2(shot.pixelColor(pixelAt(0.5)) == r.text,
                         qPrintable(where + QStringLiteral(" drawn %1").arg(name(shot.pixelColor(pixelAt(0.5))))));
                // MUST KILL: the text area is the bare index 6 beside the Field, Tab and Slider enum
                if (i == Tab || i == Field || i == 6)
                    QVERIFY2(shot.pixelColor(pixelAt(1.5)) != r.text,
                             qPrintable(where + QStringLiteral(" 2 wide: %1").arg(name(shot.pixelColor(pixelAt(1.5))))));
                if (i < 5)
                    QTRY_VERIFY2(outline(i) == r.control, qPrintable(where));
            }
            QVERIFY2(colour("defaultOutline") != r.control, qPrintable(what));
            controls[Field]->forceActiveFocus(Qt::MouseFocusReason);
            QTRY_COMPARE(colour("fieldOutline"), colour("defaultOutline"));
            QVERIFY(!ring(Field)->isVisible());
            controls[0]->forceActiveFocus(Qt::MouseFocusReason);
            QTRY_VERIFY(controls[0]->hasActiveFocus());
            QVERIFY2(!ring(0)->isVisible(), qPrintable(what));
        };
        for (const Code code : ui::theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(code));
            check(ui::theme::current().roles, ui::theme::codeName(code));
            if (QTest::currentTestFailed())
                return;
        }
        store.setValue(QString::fromLatin1(ui::theme::pickSetting(Code::HighContrastBlack, ui::theme::Pick::Text)),
                       QStringLiteral("#FFD000"));
        tabTo(controls[0]);
        QTRY_COMPARE(ringColour(0), hex(0xFFD000));
        ui::theme::useSettings(nullptr);
        tabTo(controls[0]);
        QTRY_VERIFY(ring(0)->isVisible());
        QTRY_COMPARE(ringColour(0), colour("paletteText"));
    }

    void panelFocusMarkContrast()
    {
        QString report;
        QTextStream out(&report);
        out << "theme | accent | on field | on raised | on panel | on background\n";
        const auto check = [&](Code code, const ui::theme::Roles &r, const QString &accent) {
            const QString what = ui::theme::codeName(code) + QLatin1Char(' ') + accent;
            out << ui::theme::codeName(code) << " | " << accent << " " << name(r.accent);
            const std::pair<const char *, QColor> surfaces[] = {
                {"field", r.field}, {"raised", r.raised}, {"panel", r.panel}, {"background", r.background}};
            for (const auto &[surface, colour] : surfaces) {
                const double ratio = contrastRatio(r.accent, colour);
                out << " | " << QString::number(ratio, 'f', 2);
                QVERIFY2(ratio >= 3.0, qPrintable(what + QStringLiteral(" on %1 %2").arg(QLatin1String(surface)).arg(ratio)));
            }
            out << "\n";
        };
        for (const bool dark : {false, true})
            for (const auto &a : ui::theme::accents(dark)) {
                ui::theme::Choice choice;
                (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                const Code code = dark ? Code::Dark : Code::Light;
                check(code, roles(code, choice), QLatin1String(a.key));
            }
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const Code code : {Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto r = roles(code, defaults);
            QCOMPARE(r.accent, ui::theme::pickDefault(code, ui::theme::Pick::Accent));
            check(code, r, QStringLiteral("accent pick"));
        }
        qInfo().noquote() << report;
        if (const QString dir = qEnvironmentVariable("HIKARI_THEME_SHEET_DIR"); !dir.isEmpty()) {
            QVERIFY(QDir().mkpath(dir));
            QFile file(dir + QStringLiteral("/k2-focus-mark-contrast.txt"));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(report.toUtf8());
        }
    }

    void swatchSheet()
    {
        constexpr int cell = 150, rowHeight = 112, label = 120;
        QImage sheet(label + 7 * cell, 2 * (rowHeight * 4 + 40) + 20, QImage::Format_ARGB32);
        sheet.fill(Qt::white);
        QPainter p(&sheet);
        p.setRenderHint(QPainter::Antialiasing);
        QFont font = p.font();
        font.setPixelSize(12);
        p.setFont(font);
        QString report;
        QTextStream out(&report);
        int y = 10;
        const char *surfaceNames[] = {"bg", "panel", "raised", "field"};
        for (const bool dark : {false, true}) {
            const Code code = dark ? Code::Dark : Code::Light;
            p.setPen(Qt::black);
            font.setBold(true);
            p.setFont(font);
            p.drawText(QRect(10, y, 600, 24), Qt::AlignVCenter,
                       dark ? QStringLiteral("Dark accents") : QStringLiteral("Light accents"));
            font.setBold(false);
            p.setFont(font);
            y += 30;
            out << (dark ? "Dark" : "Light") << " accents (accent / text on accent / lowest on surfaces / text on selected)\n";
            const auto list = ui::theme::accents(dark);
            for (int s = 0; s < 4; ++s) {
                const auto base = roles(code);
                const QColor surface = surfaces(base)[std::size_t(s)];
                p.fillRect(QRect(0, y, sheet.width(), rowHeight), surface);
                p.setPen(base.muted);
                p.drawText(QRect(10, y, label - 10, rowHeight), Qt::AlignVCenter,
                           QStringLiteral("%1\n%2").arg(QLatin1String(surfaceNames[s]), name(surface)));
                for (std::size_t i = 0; i < list.size(); ++i) {
                    const auto &a = list[i];
                    ui::theme::Choice choice;
                    (dark ? choice.darkAccent : choice.lightAccent) = QLatin1String(a.key);
                    const auto r = roles(code, choice);
                    const int x = label + int(i) * cell;
                    const QRect button(x + 8, y + 8, cell - 16, 30);
                    p.setPen(Qt::NoPen);
                    p.setBrush(a.accent);
                    p.drawRoundedRect(button, 1, 1);
                    p.setPen(a.onAccent);
                    p.drawText(button, Qt::AlignCenter, QString::fromLatin1(a.name));
                    const QRect row(x + 8, y + 44, cell - 16, 22);
                    p.fillRect(row, a.select);
                    p.fillRect(QRect(row.x(), row.y(), 3, row.height()), a.accent);
                    p.setPen(r.text);
                    p.drawText(row.adjusted(8, 0, 0, 0), Qt::AlignVCenter, QStringLiteral("Selected line"));
                    p.setPen(base.text);
                    p.drawText(QRect(x + 8, y + 70, cell - 16, 40), Qt::AlignLeft | Qt::AlignTop,
                               QStringLiteral("%1 %2:1\non it %3:1")
                                   .arg(name(a.accent))
                                   .arg(contrastRatio(a.accent, surface), 0, 'f', 2)
                                   .arg(contrastRatio(a.onAccent, a.accent), 0, 'f', 2));
                    if (s == 0)
                        out << "  " << a.key << " " << name(a.accent) << " / " << name(a.onAccent) << " "
                            << QString::number(contrastRatio(a.onAccent, a.accent), 'f', 2) << ":1 / "
                            << QString::number(lowest(a.accent, r), 'f', 2) << ":1 / "
                            << QString::number(contrastRatio(r.text, a.select), 'f', 2) << ":1\n";
                }
                y += rowHeight;
            }
            y += 10;
        }
        p.end();
        out << "\nRoles (lowest contrast on bg, panel, raised, field)\n";
        const auto defaults = ui::theme::choiceFrom([](const char *) { return QVariant(); });
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, defaults);
            out << ui::theme::codeName(code) << "\n";
            for (std::size_t i = 0; i < ui::theme::kRoleNames.size(); ++i) {
                const QColor c = ui::theme::role(r, i);
                out << "  " << ui::theme::kRoleNames[i] << " " << name(c) << " " << QString::number(lowest(c, r), 'f', 2)
                    << ":1\n";
            }
            out << "  onAccent on accent " << QString::number(contrastRatio(r.onAccent, r.accent), 'f', 2)
                << ":1, text on select " << QString::number(contrastRatio(r.text, r.select), 'f', 2) << ":1\n";
        }
        QVERIFY(!sheet.isNull());
        const QString dir = qEnvironmentVariable("HIKARI_THEME_SHEET_DIR");
        if (dir.isEmpty())
            return;
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(sheet.save(dir + QStringLiteral("/k2-accents.png")));
        QFile text(dir + QStringLiteral("/k2-contrast.txt"));
        QVERIFY(text.open(QIODevice::WriteOnly | QIODevice::Text));
        text.write(report.toUtf8());
    }
};

QTEST_MAIN(ThemeTests)

#include "theme_tests.moc"
