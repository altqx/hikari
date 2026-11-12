#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <utility>

namespace hikari::testing {

/// While alive, collects the messages that mean broken wiring: QML binding loops and connects with a null end.
/// Every message still reaches the handler installed before it.
class WiringWarnings {
public:
    WiringWarnings() : m_previous(qInstallMessageHandler(&WiringWarnings::handle)) { s_current = this; }
    ~WiringWarnings()
    {
        s_current = nullptr;
        qInstallMessageHandler(m_previous);
    }
    WiringWarnings(const WiringWarnings &) = delete;
    WiringWarnings &operator=(const WiringWarnings &) = delete;

    QStringList take() { return std::exchange(m_found, {}); }

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (s_current && (message.contains(QLatin1String("Binding loop detected")) ||
                          (message.contains(QLatin1String("QObject::connect")) &&
                           message.contains(QLatin1String("invalid nullptr parameter")))))
            s_current->m_found << message;
        if (s_current && s_current->m_previous)
            s_current->m_previous(type, context, message);
    }

    inline static WiringWarnings *s_current = nullptr;
    QtMessageHandler m_previous;
    QStringList m_found;
};

}
