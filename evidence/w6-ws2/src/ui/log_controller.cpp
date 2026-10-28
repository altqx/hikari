#include "log_controller.h"

#include <QThread>
#include <QTime>

namespace hikari::ui {

LogController::LogController(QObject *parent)
    : QObject(parent), m_clock([] { return QTime::currentTime().toString(QStringLiteral("HH:mm:ss")); })
{
}

void LogController::log(const QString &text, bool silent)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, text, silent] { log(text, silent); }, Qt::QueuedConnection);
        return;
    }
    m_last = text;
    m_history += m_clock() + QLatin1Char(' ') + text + QLatin1Char('\n');
    // A message that is not silent shows the window, back to just the last
    // message if it was showing the whole log (legacy OnGetLog).
    if (!silent && !m_shown) {
        m_shown = true;
        m_full = false;
    }
    emit changed();
}

void LogController::toggleWindow()
{
    // Legacy switches to the whole log once, then toggles: a window that was
    // showing a message hides (characterized).
    m_full = true;
    m_shown = !m_shown;
    emit changed();
}

void LogController::close()
{
    if (!m_shown)
        return;
    m_shown = false;
    emit changed();
}

} // namespace hikari::ui
