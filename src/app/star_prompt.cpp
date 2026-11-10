#include "hikari/app/star_prompt.h"

#include "hikari/app/analytics.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>

#include <algorithm>
#include <utility>

namespace hikari::app {

namespace {

constexpr char kDays[] = "starPrompt.days";
constexpr char kLastDay[] = "starPrompt.lastDay";
constexpr char kEditingSeconds[] = "starPrompt.editingSeconds";
constexpr char kShown[] = "starPrompt.shown";
constexpr char kSnoozedUntil[] = "starPrompt.snoozedUntil";
constexpr char kStarred[] = "starPrompt.starred";

}

StarPrompt::StarPrompt(ui::SettingsStore &settings, Analytics &analytics, Config config, QObject *parent)
    : QObject(parent), m_settings(settings), m_analytics(analytics), m_config(std::move(config))
{
    if (!m_config.now)
        m_config.now = [] { return QDateTime::currentDateTime(); };
    if (!m_config.editing)
        m_config.editing = [] { return QGuiApplication::applicationState() == Qt::ApplicationActive; };
    m_started = m_lastTick = now();
    if (!finished())
        countDay();
    m_timer.setInterval(m_config.tickMs);
    connect(&m_timer, &QTimer::timeout, this, &StarPrompt::tick);
    m_timer.start();
    QCoreApplication::instance()->installEventFilter(this);
}

QDateTime StarPrompt::now() const
{
    return m_config.now();
}

bool StarPrompt::finished() const
{
    return m_settings.boolean(kStarred) || m_settings.integer(kShown) >= m_config.maxShows;
}

void StarPrompt::countDay()
{
    const QString today = now().date().toString(Qt::ISODate);
    if (m_settings.text(kLastDay) == today)
        return;
    m_settings.set(kLastDay, today);
    m_settings.set(kDays, m_settings.integer64(kDays) + 1);
}

void StarPrompt::tick()
{
    const QDateTime t = now();
    const qint64 ms = std::clamp<qint64>(m_lastTick.msecsTo(t), 0, 2 * qint64(m_config.tickMs));
    m_lastTick = t;
    if (finished())
        return;
    countDay();
    if (ms >= 1000 && m_config.editing())
        m_settings.set(kEditingSeconds, m_settings.integer64(kEditingSeconds) + ms / 1000);
    if (waiting(false, false, false))
        emit due();
}

void StarPrompt::keyPressed()
{
    m_lastKey = now();
}

bool StarPrompt::countersReady() const
{
    return m_settings.integer64(kDays) >= m_config.minDays &&
           m_settings.integer64(kEditingSeconds) >= m_config.minEditingSeconds;
}

bool StarPrompt::waiting(bool playing, bool fullScreen, bool dialogOpen) const
{
    if (finished() || m_quietRun || !countersReady() || playing || fullScreen || dialogOpen)
        return false;
    const QDateTime t = now();
    const bool typing = m_lastKey.isValid() && m_lastKey.msecsTo(t) < m_config.typingQuietMs;
    return !typing && m_started.secsTo(t) >= m_config.sessionWaitSeconds &&
           t.toSecsSinceEpoch() >= m_settings.integer64(kSnoozedUntil);
}

bool StarPrompt::offer(bool playing, bool fullScreen, bool dialogOpen)
{
    if (!waiting(playing, fullScreen, dialogOpen))
        return false;
    m_settings.set(kShown, m_settings.integer64(kShown) + 1);
    m_settings.set(kSnoozedUntil, now().toSecsSinceEpoch() + m_config.snoozeSeconds);
    m_analytics.capture(QStringLiteral("star_prompt_shown"));
    return true;
}

void StarPrompt::star()
{
    m_settings.set(kStarred, true);
    m_analytics.capture(QStringLiteral("star_prompt_clicked"));
    backends::desktop::openUrl(repositoryUrl());
}

bool StarPrompt::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
        keyPressed();
    return QObject::eventFilter(watched, event);
}

}
