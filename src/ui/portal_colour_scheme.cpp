#include "portal_colour_scheme.h"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

namespace hikari::ui::theme {

Q_LOGGING_CATEGORY(lcPortalScheme, "hikari.theme")

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kSettings = QStringLiteral("org.freedesktop.portal.Settings");
const QString kNamespace = QStringLiteral("org.freedesktop.appearance");
const QString kKey = QStringLiteral("color-scheme");
constexpr int kReadTimeoutMs = 1000;

// Read (portal version 1) wraps the value in one more variant than ReadOne.
QVariant unwrap(QVariant value)
{
    for (int depth = 0; depth < 3; ++depth) {
        if (value.metaType() == QMetaType::fromType<QDBusVariant>())
            value = value.value<QDBusVariant>().variant();
        else if (value.metaType() == QMetaType::fromType<QDBusArgument>())
            value = qdbus_cast<QDBusVariant>(value.value<QDBusArgument>()).variant();
        else
            break;
    }
    return value;
}

}

PortalColourScheme::PortalColourScheme(const QDBusConnection &bus, QObject *parent) : QObject(parent), m_bus(bus)
{
    if (!m_bus.isConnected()) {
        qCDebug(lcPortalScheme) << "no session bus: the portal's colour scheme is not read";
        return;
    }
    m_bus.connect(kService, kPath, kSettings, QStringLiteral("SettingChanged"), this,
                  SLOT(settingChanged(QString, QString, QDBusVariant)));
    QString method = QStringLiteral("ReadOne");
    const auto request = [&method] {
        auto read = QDBusMessage::createMethodCall(kService, kPath, kSettings, method);
        read << kNamespace << kKey;
        return read;
    };
    QDBusMessage reply = m_bus.call(request(), QDBus::Block, kReadTimeoutMs);
    // xdg-desktop-portal before 1.15 has no ReadOne.
    if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
        method = QStringLiteral("Read");
        reply = m_bus.call(request(), QDBus::Block, kReadTimeoutMs);
    }
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        m_scheme = fromValue(reply.arguments().first());
        qCDebug(lcPortalScheme) << "portal colour scheme" << m_scheme << "from" << method;
        return;
    }
    qCDebug(lcPortalScheme) << "portal" << method << "failed:" << reply.errorName() << reply.errorMessage();
    const auto readLate = [this, method] {
        auto read = QDBusMessage::createMethodCall(kService, kPath, kSettings, method);
        read << kNamespace << kKey;
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(read), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            watcher->deleteLater();
            const QDBusMessage late = watcher->reply();
            qCDebug(lcPortalScheme) << "portal answered late:" << late.type() << late.arguments();
            if (!m_signalled && late.type() == QDBusMessage::ReplyMessage && !late.arguments().isEmpty())
                set(fromValue(late.arguments().first()));
        });
    };
    if (reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply")
        || reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.Timeout")) {
        readLate();
        return;
    }
    auto *appears = new QDBusServiceWatcher(kService, m_bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(appears, &QDBusServiceWatcher::serviceRegistered, this, [appears, readLate] {
        appears->deleteLater();
        qCDebug(lcPortalScheme) << "portal took its name: read again";
        readLate();
    });
}

Qt::ColorScheme PortalColourScheme::fromValue(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    const int type = inner.metaType().id();
    if (type != QMetaType::UInt && type != QMetaType::Int)
        return Qt::ColorScheme::Unknown;
    switch (inner.toUInt()) {
    case 1:
        return Qt::ColorScheme::Dark;
    case 2:
        return Qt::ColorScheme::Light;
    default:
        return Qt::ColorScheme::Unknown;
    }
}

void PortalColourScheme::settingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value)
{
    if (nameSpace != kNamespace || key != kKey)
        return;
    m_signalled = true;
    qCDebug(lcPortalScheme) << "portal colour scheme changed:" << value.variant();
    set(fromValue(value.variant()));
}

void PortalColourScheme::set(Qt::ColorScheme scheme)
{
    if (scheme == m_scheme)
        return;
    m_scheme = scheme;
    emit changed();
}

}
