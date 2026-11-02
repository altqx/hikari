#pragma once

#include "settings_store.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace hikari::app {

class Analytics;

// Suggests starring HikariSub on GitHub after real use, at most twice ever (#645). Every counter
// lives in the settings; nothing is sent beyond the two usage events.
class StarPrompt : public QObject {
    Q_OBJECT
public:
    struct Config {
        // Empty: the system clock.
        std::function<QDateTime()> now;
        // Whether a Document is being edited in the foreground. Empty: the application is active.
        std::function<bool()> editing;
        int tickMs = 60 * 1000;
        int minDays = 5;
        qint64 minEditingSeconds = 3 * 60 * 60;
        qint64 sessionWaitSeconds = 10 * 60;
        qint64 snoozeSeconds = 30 * 24 * 60 * 60;
        int maxShows = 2;
        qint64 typingQuietMs = 10 * 1000;
    };
    static QUrl repositoryUrl() { return QUrl(QStringLiteral("https://github.com/altqx/hikari")); }

    StarPrompt(ui::SettingsStore &settings, Analytics &analytics, Config config, QObject *parent = nullptr);

    // Counts the day and the editing time since the last tick; emits due() while the toast may show.
    void tick();
    void keyPressed();
    // A first run, or the first run after an update with What's new: no prompt in this run.
    void quietRun() { m_quietRun = true; }

    // Shows nothing itself: true when the toast may show now, then counted as shown and snoozed.
    Q_INVOKABLE bool offer(bool playing, bool fullScreen, bool dialogOpen);
    // Opens the repository; the prompt never shows again.
    Q_INVOKABLE void star();
    bool waiting(bool playing, bool fullScreen, bool dialogOpen) const;
    bool finished() const;

signals:
    void due();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QDateTime now() const;
    bool countersReady() const;
    void countDay();

    ui::SettingsStore &m_settings;
    Analytics &m_analytics;
    Config m_config;
    QTimer m_timer;
    QDateTime m_started;
    QDateTime m_lastTick;
    QDateTime m_lastKey;
    bool m_quietRun = false;
};

}
