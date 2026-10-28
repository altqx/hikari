#pragma once

// K2: Follow system theme on Linux reads the desktop's colour scheme from the
// XDG settings portal (org.freedesktop.portal.Settings, the
// org.freedesktop.appearance namespace's color-scheme key) when Qt's own
// platform theme reports none. Qt's GNOME, KDE and xdgdesktopportal themes
// set QStyleHints::colorScheme from the desktop; the generic theme that
// sway, Hyprland, i3 and the other desktops get leaves it Unknown, so the
// theme followed nothing there.
//
// The scheme is read once when made (blocking for up to a second, so the
// first window shows on the right side; a later answer is still taken, and a
// portal that takes its name on the bus later is read then) and then follows
// the portal's SettingChanged signal. Category hikari.theme
// logs what the portal answered.

#include <QDBusConnection>
#include <QDBusVariant>
#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>

namespace hikari::ui::theme {

class PortalColourScheme : public QObject {
    Q_OBJECT
public:
    // Reads the portal on `bus` (the session bus in the application, a
    // private one in tests). A bus without the portal leaves it Unknown.
    explicit PortalColourScheme(const QDBusConnection &bus, QObject *parent = nullptr);

    // Dark for 1 (prefer dark), Light for 2 (prefer light), Unknown for 0
    // (no preference) or anything else, as Qt's own portal theme maps it.
    Qt::ColorScheme scheme() const { return m_scheme; }
    static Qt::ColorScheme fromValue(const QVariant &value);

signals:
    void changed();

private slots:
    void settingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);

private:
    void set(Qt::ColorScheme scheme);
    QDBusConnection m_bus;
    Qt::ColorScheme m_scheme = Qt::ColorScheme::Unknown;
    bool m_signalled = false;
};

// The portal the theme follows while QStyleHints reports no scheme (nullptr:
// none). The theme makes its own on the session bus under the X11 and
// Wayland platforms; tests hand it one on a private bus.
void followPortal(PortalColourScheme *portal);
// For tests: the scheme QStyleHints is taken to report (nullopt: what it
// does report), so that a platform theme reporting one can be shown to come
// before the portal's.
void forcePlatformScheme(std::optional<Qt::ColorScheme> scheme);

} // namespace hikari::ui::theme
