#pragma once

// The log window's presenter (P5; legacy LogHandler/LogWindow at 20d647c4).
// Every message is kept with its local time ("HH:MM:SS text"). A message that
// is not silent pops the window up showing just that message; File > "Show /
// Hide log window" switches it to the whole log and toggles it. Messages may
// come from any thread; they are published on this object's thread.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class LogController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY changed)
    Q_PROPERTY(QString history READ history NOTIFY changed)
    // The window is shown; full: the whole log rather than the last message.
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    Q_PROPERTY(bool full READ full NOTIFY changed)
public:
    explicit LogController(QObject *parent = nullptr);

    QString lastMessage() const { return m_last; }
    QString history() const { return m_history; }
    bool shown() const { return m_shown; }
    bool full() const { return m_full; }

    // HikariLog (silent = false) and HikariLogSilent (silent = true).
    Q_INVOKABLE void log(const QString &text, bool silent = false);
    // File > "Show / Hide log window" (LogHandler::ShowLogWindow).
    Q_INVOKABLE void toggleWindow();
    Q_INVOKABLE void close();
    // Tests: the clock that stamps messages.
    void setClock(std::function<QString()> clock) { m_clock = std::move(clock); }

signals:
    void changed();

private:
    QString m_last;
    QString m_history;
    bool m_shown = false;
    bool m_full = false;
    std::function<QString()> m_clock;
};

} // namespace hikari::ui
