// K2: the Hikari theme layer (src/ui/theme.h, docs/qt/ux/visual-language.md,
// the user's 2026-10-05 decision after MuseScore 4's appearance model,
// docs/research/musescore-appearance.md). The four themes' role tables (Light
// and Dark the spec's tokens, High contrast black the spec's high-contrast
// column), the seven accent presets per mode and their measured WCAG
// contrast, following the system's light or dark scheme (never high
// contrast), the high-contrast pickers, the content colours fixed per theme
// (Dark keeps legacy's dark theme, config.cpp:405-472), the live preview and
// the application palette, and the Theme singleton QML reads. With
// HIKARI_THEME_SHEET_DIR set the swatch sheet for the user's review is
// written there (k2-accents.png) with the measured ratios (k2-contrast.txt).

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
// The XDG settings portal, as xdg-desktop-portal answers on the session bus:
// ReadOne (interface version 2) and the SettingChanged signal.
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

// A portal slower than the first read waits (one still starting): each
// answer comes 1.5 s late.
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

// A private session bus (no service directories, so no real portal starts)
// with a server and a client connection.
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

} // namespace

class ThemeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The application's controls style, chosen before any engine loads
        // the controls (src/app/composition.cpp does the same).
        ui::theme::chooseControlsStyle();
    }

    void init()
    {
        ui::theme::useSettings(nullptr);
        ui::theme::endPreview();
        ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
    }

    // visual-language.md, "Semantic appearance tokens": Light and Dark with
    // the default (green) accent are the spec's columns; High contrast black
    // is the spec's high-contrast column.
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
        QCOMPARE(dark.focus, dark.text); // K2 focus: the text colour, not the spec's #F9D784
        QCOMPARE(dark.danger, hex(0xFFADAD));
        const auto light = roles(Code::Light);
        QCOMPARE(surfaces(light), (std::array{hex(0xE5E9EC), hex(0xF9FAFB), hex(0xEDF0F3), hex(0xFFFFFF)}));
        QCOMPARE(light.text, hex(0x202832));
        QCOMPARE(light.muted, hex(0x526170));
        QCOMPARE(light.line, hex(0xAAB5BE));
        QCOMPARE(light.accent, hex(0x145C4C));
        QCOMPARE(light.onAccent, hex(0xFFFFFF));
        QCOMPARE(light.select, hex(0xD6EBE4));
        QCOMPARE(light.focus, light.text); // not the spec's #8D4200
        QCOMPARE(light.danger, hex(0xA51F31));
        const auto black = roles(Code::HighContrastBlack, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
        QCOMPARE(surfaces(black), (std::array{hex(0x000000), hex(0x080808), hex(0x151515), hex(0x000000)}));
        QCOMPARE(black.text, hex(0xFFFFFF));
        QCOMPARE(black.muted, hex(0xEEEEEE));
        QCOMPARE(black.line, hex(0xFFFFFF));
        QCOMPARE(black.accent, hex(0xFFFF00));
        QCOMPARE(black.onAccent, hex(0x000000));
        QCOMPARE(black.select, hex(0x253F60));
        QCOMPARE(black.focus, black.text); // not the spec's #00FFFF
        QCOMPARE(black.danger, hex(0xFFADAD));
        // The success text is legacy FontCollector's "#008000" in Light and
        // Dark (FontCollector.cpp:872, 978: the same in every legacy theme).
        QCOMPARE(light.success, hex(0x008000));
        QCOMPARE(dark.success, hex(0x008000));
        // Every role is set in every theme.
        for (const auto code : ui::theme::kCodes) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            for (std::size_t i = 0; i < ui::theme::kRoleNames.size(); ++i)
                QVERIFY2(ui::theme::role(r, i).isValid(), ui::theme::kRoleNames[i]);
        }
    }

    // The roles' measured contrast (WCAG 2.x) on the theme's four surfaces:
    // text, secondary text, danger and warning text at least 4.5:1; the
    // accent, focus, icons and disabled at least 3:1 (WCAG 1.4.11); text on
    // the accent and on the selected background at least 4.5:1. High
    // contrast holds every text role to 7:1 apart from disabled. The one
    // exception is legacy's success green in Light and Dark (above).
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
            QVERIFY2(contrastRatio(r.onAccent, r.accent) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.text, r.select) >= textMinimum, qPrintable(theme));
            QVERIFY2(contrastRatio(r.disabled, r.text) >= 2.0, qPrintable(theme)); // disabled looks unlike enabled
            if (hc) {
                QVERIFY2(lowest(r.success, r) >= 7.0, qPrintable(theme));
                QVERIFY2(lowest(r.line, r) >= 7.0, qPrintable(theme)); // boundaries survive without shadows
            }
        }
    }

    // K2 focus (visual-language.md, "Keyboard focus"; the user's 2026-10-05
    // decision after MuseScore 4, whose real focus ring is
    // NavigationFocusBorder in fontPrimaryColor): the focus role is the
    // theme's text colour in every theme and with every accent preset or
    // high-contrast text pick, so it reads on every surface as text does,
    // and it is never the colour of selection (the accent, the selected
    // background), on which it reads as text does.
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
        // A high-contrast text pick moves keyboard focus with it.
        const QVariantMap picks{{QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FFD000")},
                                {QStringLiteral("appearance.highContrastWhite.text"), QStringLiteral("#300060")}};
        const auto picked = ui::theme::choiceFrom([&](const char *id) { return picks.value(QLatin1String(id)); });
        QCOMPARE(roles(Code::HighContrastBlack, picked).focus, hex(0xFFD000));
        QCOMPARE(roles(Code::HighContrastWhite, picked).focus, hex(0x300060));
    }

    // Seven presets per mode, by hue, in the same order and keys in Light and
    // Dark; the default (the fourth) is the spec's green. Each meets 4.5:1
    // against every surface of its mode, carries readable text (4.5:1) and a
    // readable selected background.
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
                // the selected background stays a quiet tint (the spec green's)
                QVERIFY2(contrastRatio(a.select, r.panel) < 2.0, qPrintable(what));
                // the other mode's roles and the theme's own roles stay put
                QCOMPARE(r.text, roles(code).text);
                QCOMPARE(surfaces(r), surfaces(roles(code)));
            }
        }
        // the spec's green
        QCOMPARE(ui::theme::accent(false, QStringLiteral("green")).accent, hex(0x145C4C));
        QCOMPARE(ui::theme::accent(true, QStringLiteral("green")).accent, hex(0x9CDBC9));
        // an unknown key is the default
        QCOMPARE(QLatin1String(ui::theme::accent(true, QStringLiteral("teal")).key), QLatin1String("green"));
    }

    // Light and Dark each remember their own accent; high contrast ignores
    // them (its accent is the picker's).
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

    // Follow system theme (MuseScore's doSetIsDarkMode): Light or Dark from
    // the platform's colour scheme, keeping high contrast on its matching
    // side; never turning high contrast on or off. Off, or with no scheme
    // known, the chosen theme shows.
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
        // Live: the platform's scheme changes, the theme follows at once.
        ui::SettingsStore store;
        ui::theme::useSettings(&store);
        int changes = 0;
        QObject context;
        ui::theme::onChanged(&context, [&] { ++changes; });
        QCOMPARE(ui::theme::current().code, Code::Dark); // the default, following, scheme unknown
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 1);
        ui::theme::forceSystemScheme(S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(changes, 2);
        // following off: the platform's scheme no longer matters
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), false);
        ui::theme::forceSystemScheme(S::Light);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::useSettings(nullptr);
    }

    // Follow system theme on Linux desktops whose Qt platform theme reports
    // no scheme (the generic theme: sway, Hyprland, i3): the XDG settings
    // portal's color-scheme, read at the start and followed live; 1 dark,
    // 2 light, 0 (no preference) the chosen theme. A scheme Qt reports wins
    // (QStyleHints, forced here); following off, the portal no longer
    // matters. A portal older than ReadOne answers Read; one too slow for
    // the first read is followed once it answers; no portal on the bus
    // leaves the scheme unknown until one takes the name.
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
            // No portal on the bus: unknown (the bus answers ServiceUnknown;
            // no bound on the time, which a loaded host stretches).
            PortalColourScheme none(client);
            QCOMPARE(none.scheme(), S::Unknown);
        }

        // The portal answers on its own thread, as the real one does in its
        // own process, while the reader blocks this one.
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
        auto *fake = new FakeSettingsPortal; // prefer dark
        fake->moveToThread(&portalThread);
        fakes << fake;
        FakeSettingsPortal &portal = *fake;
        // Registered on its thread: the signal relay is made as its child.
        const auto registerOnItsThread = [&](QObject *object, QDBusConnection::RegisterOptions options) {
            bool registered = false;
            QMetaObject::invokeMethod(
                object, [&] { registered = server.registerObject(path, object, options); }, Qt::BlockingQueuedConnection);
            return registered;
        };
        // A reader made before the portal takes its name (one still
        // starting): unknown, then read once the name is taken.
        PortalColourScheme early(client);
        QCOMPARE(early.scheme(), S::Unknown);
        QVERIFY(registerOnItsThread(&portal, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(server.registerService(service));
        QTRY_COMPARE(early.scheme(), S::Dark);
        QCOMPARE(portal.reads.load(), 1);
        PortalColourScheme scheme(client);
        QCOMPARE(portal.reads.load(), 2);
        QCOMPARE(scheme.scheme(), S::Dark);

        // The theme follows it while Qt reports no scheme (the offscreen
        // platform's theme, like the generic one, reports none).
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

        // gsettings color-scheme prefer-light, prefer-dark, prefer-light, live
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 1);
        portal.change(1);
        QTRY_COMPARE(ui::theme::current().code, Code::Dark);
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(changes, 3);
        // Another setting changing leaves it.
        portal.change(1, QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme"));
        portal.change(1, QStringLiteral("org.freedesktop.appearance"), QStringLiteral("accent-color"));
        QTest::qWait(100);
        QCOMPARE(ui::theme::current().code, Code::Light);
        // No preference: the chosen theme (Dark, the default).
        portal.change(0);
        QTRY_COMPARE(ui::theme::current().code, Code::Dark);
        portal.change(2);
        QTRY_COMPARE(ui::theme::current().code, Code::Light);

        // A scheme Qt's platform theme reports comes first (GNOME, KDE,
        // xdgdesktopportal); Unknown from it leaves the portal's.
        ui::theme::forcePlatformScheme(S::Dark);
        QCOMPARE(ui::theme::systemScheme(), S::Dark);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        ui::theme::forcePlatformScheme(S::Unknown);
        QCOMPARE(ui::theme::current().code, Code::Light);
        ui::theme::forcePlatformScheme(std::nullopt);
        QCOMPARE(ui::theme::current().code, Code::Light);

        // High contrast keeps its side's pair; never turned on or off.
        store.setValue(QLatin1String(ui::theme::kThemeSetting), ui::theme::codeName(Code::HighContrastBlack));
        QCOMPARE(ui::theme::current().code, Code::HighContrastWhite);
        store.setValue(QLatin1String(ui::theme::kThemeSetting), ui::theme::codeName(Code::Dark));
        QCOMPARE(ui::theme::current().code, Code::Light);

        // Following off: the portal no longer matters.
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), false);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        portal.change(1);
        portal.change(2);
        QTest::qWait(100);
        QCOMPARE(ui::theme::current().code, Code::Dark);
        store.setValue(QLatin1String(ui::theme::kFollowSystemSetting), true);
        QCOMPARE(ui::theme::current().code, Code::Light);

        // The portal gone (its object destroyed): the chosen theme.
        ui::theme::followPortal(nullptr);
        QCOMPARE(ui::theme::current().code, Code::Dark);

        // A portal older than ReadOne: Read, its value in one more variant.
        server.unregisterObject(path);
        auto *old = new FakeOldSettingsPortal;
        old->moveToThread(&portalThread);
        fakes << old;
        QVERIFY(registerOnItsThread(old, QDBusConnection::ExportAllSlots));
        QDBusConnection client2 = QDBusConnection::connectToBus(bus.address, QStringLiteral("k2-portal-client2"));
        QCOMPARE(PortalColourScheme(client2).scheme(), S::Light);

        // A portal too slow for the first read: unknown at first, then its
        // late answer, followed by the theme.
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

    // The high-contrast pickers: accent, text-and-icons and border, freely
    // chosen, each high-contrast theme its own; every other role fixed; text
    // on a picked accent turns black or white to stay readable. An
    // unreadable value is the default. Light and Dark ignore them.
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
        QCOMPARE(black.onAccent, hex(0xFFFFFF)); // black on navy would not read
        QCOMPARE(black.background, base.background);
        QCOMPARE(black.select, base.select);
        QCOMPARE(black.focus, hex(0xFFD000)); // keyboard focus follows the text pick
        QCOMPARE(black.disabled, base.disabled);
        // High contrast white keeps its own (the unreadable one: its default)
        QCOMPARE(roles(Code::HighContrastWhite, choice).accent, hex(0x0037B3));
        QCOMPARE(roles(Code::HighContrastWhite, choice).text, hex(0x000000));
        // Light and Dark ignore the pickers
        QCOMPARE(roles(Code::Dark, choice), roles(Code::Dark));
        // The pickers' settings and defaults.
        QCOMPARE(QLatin1String(ui::theme::pickSetting(Code::HighContrastWhite, ui::theme::Pick::Border)),
                 QLatin1String("appearance.highContrastWhite.border"));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastBlack, ui::theme::Pick::Accent), hex(0xFFFF00));
        QCOMPARE(ui::theme::pickDefault(Code::HighContrastWhite, ui::theme::Pick::Text), hex(0x000000));
    }

    // Unknown or missing values are the defaults: the Dark theme (legacy's
    // default theme), following the system, the green accents.
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

    // Content colours are fixed per theme; Dark keeps legacy's dark theme
    // (config.cpp:405-472, LoadDefaultColors(true)): the audio display's
    // (the AudioDisplayOptions defaults, the spectrum's AUDIO_SPECTRUM_*
    // among them), the Grid's GRID_COMPARISON_* and GRID_SPELLCHECKER; Light
    // keeps legacy's light comparison and spelling colours. The
    // selection-type marks take the accent: AUDIO_SELECTION_BACKGROUND (and
    // _MODIFIED) at legacy's 0x37 alpha, AUDIO_WAVEFORM_SELECTED, and
    // GRID_SELECTION's blend at legacy's alpha 75 (config.cpp:422).
    void contentColoursAreFixedPerTheme()
    {
        const application::AudioDisplayOptions legacy;
        const auto dark = ui::theme::content(Code::Dark, roles(Code::Dark));
        auto expected = legacy;
        expected.selectionBackground = expected.selectionModified = 0x379CDBC9;
        expected.waveformSelected = 0xFF9CDBC9;
        auto actual = legacy;
        ui::theme::applyAudioColours(actual, dark.audio);
        QCOMPARE(actual.background, 0xFF36393Eu); // AUDIO_BACKGROUND
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
        // E4: the Line editor's text colours, legacy's EDITOR_* defaults
        // (config.cpp:432-442) in Dark and Light; EDITOR_TEXT is the text role.
        QCOMPARE(dark.syntax, (std::array{roles(Code::Dark).text, hex(0x8791FD), hex(0x00C3FF), hex(0x00C3FF),
                                          hex(0x0076FF), hex(0xADABAB), hex(0xBDB76B), hex(0xFB4544), hex(0x00C3FF),
                                          hex(0x8D80D3), hex(0x11A243)}));
        QCOMPARE(light.syntax, (std::array{roles(Code::Light).text, hex(0x7E3B00), hex(0x787600), hex(0x787600),
                                           hex(0x34C200), hex(0x817F7F), hex(0xBDB76B), hex(0xFB4544), hex(0x00C3FF),
                                           hex(0x8D80D3), hex(0x11A243)}));
        // The high-contrast themes' text colours read on their field at 4.5:1.
        for (const auto code : {Code::HighContrastWhite, Code::HighContrastBlack}) {
            const auto r = roles(code, ui::theme::choiceFrom([](const char *) { return QVariant(); }));
            const auto c = ui::theme::content(code, r);
            QCOMPARE(c.syntax[0], r.text);
            for (const QColor &colour : c.syntax)
                QVERIFY2(contrastRatio(colour, r.field) >= 4.5,
                         qPrintable(ui::theme::codeName(code) + QLatin1Char(' ') + colour.name()));
        }
        // The accent moves the selection marks only.
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
        // Each theme's display reads: the waveform and the timescale text on
        // their backgrounds (3:1), the high-contrast ones at 4.5:1.
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

    // With a profile the theme follows it live: the application palette the
    // controls draw with is the theme's, a change of an appearance setting
    // retints at once (others do not count), and the Options dialog's
    // preview shows staged values until it ends.
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
        QCOMPARE(window(), hex(0x20262D)); // Dark's panel
        QCOMPARE(QGuiApplication::palette().color(QPalette::Highlight), hex(0x9CDBC9));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText), hex(0x75818D));
        store.setValue(QStringLiteral("audio.spectrumOn"), true);
        QCOMPARE(changes, 0);
        store.setValue(QStringLiteral("appearance.darkAccent"), QStringLiteral("gold"));
        QCOMPARE(changes, 1);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Accent), ui::theme::accent(true, QStringLiteral("gold")).accent);
        // The preview: staged values over the profile's, nothing saved.
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("light")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(ui::theme::current().code, Code::Light);
        QCOMPARE(window(), hex(0xF9FAFB));
        QCOMPARE(store.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("dark"));
        ui::theme::endPreview();
        QCOMPARE(ui::theme::current().code, Code::Dark);
        QCOMPARE(window(), hex(0x20262D));
        QCOMPARE(ui::theme::current().roles.accent, ui::theme::accent(true, QStringLiteral("gold")).accent);
        // The profile goes: the defaults again.
        ui::theme::useSettings(nullptr);
        QCOMPARE(ui::theme::current().roles.accent, hex(0x9CDBC9));
    }

    // The Theme singleton QML reads: the roles, the theme's state, the
    // Appearance page's lists (the themes, a mode's accents with their
    // lowest contrast, the pickers, the roles a theme resolves to), live.
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
        // live
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(object->property("accent").value<QColor>(), hex(0xFFFF00));
        QCOMPARE(object->property("panel").value<QColor>(), hex(0x080808));
        QVERIFY(object->property("highContrast").toBool());
    }

    // The controls style (src/ui/style): every control outline in the
    // theme's boundary colour (the line role, the high-contrast border pick),
    // live; Fusion's own, its window colour darkened 140%, measures about
    // 1.1:1 against the Dark and High contrast black panels. A focused
    // field's outline stays Fusion's accent-derived one.
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
        slider.handle.border.color]
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
            // The controls take the new application palette an event loop
            // pass later; a closed menu when it opens (as the shell's do).
            QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("menu")), "open");
            QTRY_VERIFY_WITH_TIMEOUT(std::ranges::all_of(window->property("outlines").toList(),
                                                         [&](const QVariant &c) { return c.value<QColor>() == r.line; }),
                                     1000);
            const auto outlines = window->property("outlines").toList();
            QCOMPARE(outlines.size(), 15);
            for (qsizetype i = 0; i < outlines.size(); ++i)
                QVERIFY2(outlines[i].value<QColor>() == r.line,
                         qPrintable(QStringLiteral("%1: control %2 outline %3, line %4").arg(ui::theme::codeName(code)).arg(i)
                                        .arg(name(outlines[i].value<QColor>()), name(r.line))));
            // Fusion's outline: what the controls drew before.
            const QColor fusion = r.panel.darker(140);
            if (code == Code::Dark || code == Code::HighContrastBlack)
                QVERIFY(contrastRatio(fusion, r.panel) < 1.2);
            if (ui::theme::isHighContrast(code))
                QVERIFY(lowest(r.line, r) >= 7);
        }
        // The high-contrast border pick reaches the controls.
        store.setValue(QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack"));
        store.setValue(QString::fromLatin1(ui::theme::pickSetting(Code::HighContrastBlack, ui::theme::Pick::Border)),
                       QStringLiteral("#00FF00"));
        QTRY_COMPARE(window->property("outlines").toList().front().value<QColor>(), hex(0x00FF00));
    }

    // The controls style's combo box: Fusion's text field inside it, read-only
    // unless the box is editable, is an Edit under the ComboBox to assistive
    // technology (UI Automation reads it). It carries the box's name, live,
    // as a native combo box's edit does; it had none (Windows native gate,
    // dialogs: nine unnamed Edits, one per ComboBox).
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
            // The box itself keeps its name, and the field stays its child.
            QAccessibleInterface *combo = edit->parent();
            QVERIFY(combo);
            QCOMPARE(combo->role(), QAccessible::ComboBox);
            QCOMPARE(combo->text(QAccessible::Name), QString::fromLatin1(name));
        }
        // Live: a new name (a language switch) reaches the field.
        window->setProperty("choiceName", QStringLiteral("Lines"));
        QCOMPARE(field("choice")->text(QAccessible::Name), QStringLiteral("Lines"));
    }

    // K2 focus (visual-language.md, "Keyboard focus"; the user's 2026-10-05
    // decision after MuseScore 4's NavigationFocusBorder, made 1 wide and
    // tight by the user's 2026-10-13 decision): every focusable control of
    // the style draws keyboard focus (Tab, Backtab, a shortcut) as a ring 1
    // wide in the focus role, the theme's text colour: just outside a
    // button, tool button, combo box, check box, radio button, field, text
    // area, spin box or a slider's handle, touching its edge, and 1 inside a
    // tab. The accent
    // stays for selection, the default button and a focused field's border,
    // and the control's own outline stays the boundary colour, so keyboard
    // focus and selection are told apart. A click's focus draws no ring.
    // Live, in every theme, with a high-contrast text pick; without a
    // profile the ring is the palette's text colour.
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
            primary->forceActiveFocus(Qt::MouseFocusReason); // (a focus change, with its reason)
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
                // (the palette reaches the controls an event loop pass later)
                QTRY_VERIFY2(ringColour(i) == r.text,
                             qPrintable(where + QStringLiteral(" ring %1, text %2").arg(name(ringColour(i)), name(r.text))));
                QCOMPARE(ringColour(i), r.focus);
                QVERIFY2(ringColour(i) != r.accent && ringColour(i) != r.select, qPrintable(where));
                QCOMPARE(ring(i)->property("border").value<QObject *>()->property("width").toInt(), 1);
                // Where it is: just outside the control (the slider's
                // handle), touching it; 1 inside a tab's background, which
                // the tab bar would clip.
                const QRectF at = sceneRect(ring(i));
                if (i == Tab) {
                    auto *background = controls[i]->property("background").value<QQuickItem *>();
                    QVERIFY2(sceneRect(controls[i]).contains(at), qPrintable(where));
                    QCOMPARE(at, sceneRect(background).adjusted(1, 1, -1, -1));
                } else {
                    QCOMPARE(at, sceneRect(i == Slider ? controls[i]->property("handle").value<QQuickItem *>()
                                                       : controls[i]).adjusted(-1, -1, 1, 1));
                }
                // and drawn: its left side in the text colour, 1 wide (the
                // pixel inside it is not the ring's)
                const QImage shot = window->grabWindow();
                const qreal dpr = window->effectiveDevicePixelRatio();
                const auto pixelAt = [&](qreal x) {
                    return QPoint(int(std::floor((at.left() + x) * dpr)), int(std::floor((at.top() + at.height() / 2) * dpr)));
                };
                QVERIFY2(shot.pixelColor(pixelAt(0.5)) == r.text,
                         qPrintable(where + QStringLiteral(" drawn %1").arg(name(shot.pixelColor(pixelAt(0.5))))));
                if (i == Tab || i == Field || i == 6 /* the text area */)
                    QVERIFY2(shot.pixelColor(pixelAt(1.5)) != r.text,
                             qPrintable(where + QStringLiteral(" 2 wide: %1").arg(name(shot.pixelColor(pixelAt(1.5))))));
                // The control's own outline stays the boundary colour.
                if (i < 5)
                    QTRY_VERIFY2(outline(i) == r.line, qPrintable(where));
            }
            // The default button keeps the accent's outline; a focused
            // field's border is the accent's outline too, with the ring
            // beside it only for keyboard focus.
            QVERIFY2(colour("defaultOutline") != r.line, qPrintable(what));
            controls[Field]->forceActiveFocus(Qt::MouseFocusReason);
            QTRY_COMPARE(colour("fieldOutline"), colour("defaultOutline"));
            QVERIFY(!ring(Field)->isVisible());
            // A click's focus draws no ring.
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
        // A high-contrast text pick reaches the ring.
        store.setValue(QString::fromLatin1(ui::theme::pickSetting(Code::HighContrastBlack, ui::theme::Pick::Text)),
                       QStringLiteral("#FFD000"));
        tabTo(controls[0]);
        QTRY_COMPARE(ringColour(0), hex(0xFFD000));
        // Without a profile: the palette's text colour.
        ui::theme::useSettings(nullptr);
        tabTo(controls[0]);
        QTRY_VERIFY(ring(0)->isVisible());
        QTRY_COMPARE(ringColour(0), colour("paletteText"));
    }

    // K2 focus (the user's 2026-10-13 decision): the panel holding the focus
    // shows a small triangle in the accent at its header's top trailing
    // corner (PanelFocusMark.qml), where the header is `field` (a lone
    // panel's title bar, a selected tab; also the panel body, the palette's
    // base) or `raised` (the tab strip). The accent meets 3:1 on those and on
    // the `panel` and `background` surfaces around a panel in every theme
    // with every preset, the high contrast themes' default accent pick
    // included. With
    // HIKARI_THEME_SHEET_DIR set the ratios are written to
    // k2-focus-mark-contrast.txt for the review.
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

    // The swatch sheet for the user's review: per mode, the seven accents as
    // primary buttons and selection samples on the mode's four surfaces, each
    // with its lowest measured contrast, and the four themes' role tables
    // with their measured contrast as text.
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
                    // a primary button
                    const QRect button(x + 8, y + 8, cell - 16, 30);
                    p.setPen(Qt::NoPen);
                    p.setBrush(a.accent);
                    p.drawRoundedRect(button, 1, 1);
                    p.setPen(a.onAccent);
                    p.drawText(button, Qt::AlignCenter, QString::fromLatin1(a.name));
                    // a selected row with its leading accent marker
                    const QRect row(x + 8, y + 44, cell - 16, 22);
                    p.fillRect(row, a.select);
                    p.fillRect(QRect(row.x(), row.y(), 3, row.height()), a.accent);
                    p.setPen(r.text);
                    p.drawText(row.adjusted(8, 0, 0, 0), Qt::AlignVCenter, QStringLiteral("Selected line"));
                    // the ratios
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
