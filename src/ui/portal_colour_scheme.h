#pragma once

#include <QDBusConnection>
#include <QDBusVariant>
#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>

namespace hikari::ui::theme {

// Qt's generic platform theme (sway, Hyprland, i3) leaves QStyleHints::colorScheme Unknown; this reads the XDG settings portal's.
class PortalColourScheme : public QObject {
    Q_OBJECT
public:
    explicit PortalColourScheme(const QDBusConnection &bus, QObject *parent = nullptr);

    Qt::ColorScheme scheme() const { return m_scheme; }
    // The portal's color-scheme: 1 prefer dark, 2 prefer light, 0 no preference.
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

void followPortal(PortalColourScheme *portal);
void forcePlatformScheme(std::optional<Qt::ColorScheme> scheme);

}
