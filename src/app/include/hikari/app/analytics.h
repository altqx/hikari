#pragma once

// AN1 (#404): anonymous usage data, sent to PostHog Cloud EU's capture API
// (POST /batch/). Every part of the application reports through this one
// class. An event is queued only while sending is on: a project key compiled
// in (HIKARI_POSTHOG_KEY, release builds), the setting privacy.usageData on
// (the default) and no DO_NOT_TRACK in the environment. Each event's
// properties are checked against the closed table in docs/qt/analytics.md
// (conforms()); an event that does not conform is dropped. The queue lives in
// memory and in a bounded file in the cache folder; it is sent every few
// minutes and at quit, never waited on by the window, and a send that fails
// waits for the next one. Turning the setting off drops the queue and the
// id; turning it on again makes a new id.

#include "settings_store.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <array>
#include <atomic>

class QNetworkReply;

namespace hikari::app {

class Analytics : public QObject {
    Q_OBJECT
    // Whether events are queued and sent now (Settings shows the switch either way).
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    struct Config {
        // The PostHog project key; empty: nothing is queued or sent.
        QString key;
        // PostHog Cloud EU's batch endpoint (tests: a local server).
        QUrl endpoint = defaultEndpoint();
        // The queue's file; empty: memory only.
        QString queueFile;
        // This version ($lib_version).
        QString version;
        // DO_NOT_TRACK was set for this run.
        bool doNotTrack = false;
        int flushIntervalMs = 5 * 60 * 1000;
        int firstFlushMs = 60 * 1000;
    };
    static QUrl defaultEndpoint() { return QUrl(QStringLiteral("https://eu.i.posthog.com/batch/")); }
    // "What is sent": the event table, docs/qt/analytics.md.
    static QUrl whatIsSentUrl()
    {
        return QUrl(QStringLiteral("https://github.com/altqx/hikari/blob/qt/docs/qt/analytics.md"));
    }
    // The key this build was made with (HIKARI_POSTHOG_KEY), "" for none.
    static QString compiledKey();
    // DO_NOT_TRACK's value: anything but unset, "" or "0" turns sending off.
    static bool doNotTrack(const QByteArray &value);

    // At most this many events, and this many bytes of them, are kept; the
    // oldest go first.
    static constexpr int kMaxEvents = 1000;
    static constexpr qint64 kMaxBytes = 1024 * 1024;
    static constexpr int kBatchSize = 100;
    static constexpr int kMaxLayoutChanges = 20;

    Analytics(ui::SettingsStore &settings, Config config, QObject *parent = nullptr);
    ~Analytics() override;

    bool active() const;

    // Queues `event` with `properties` (the common ones are added) when
    // sending is on and the event conforms; returns whether it was queued.
    bool capture(const QString &event, const QJsonObject &properties = {});
    // Whether `event` with `properties` holds only what the table allows.
    static bool conforms(const QString &event, const QJsonObject &properties);

    // CR1 (#452): a report the user chose to send (Send report): `events`
    // ($exception and crash_report) go at once in a batch of their own, with
    // usage data on or off, since pressing Send is consent for that report.
    // Off, they carry a one-off id and nothing is kept. Needs a key; false
    // when there is none or an event does not conform. reportSent() follows.
    bool sendReport(const QList<std::pair<QString, QJsonObject>> &events);
    bool canSendReports() const { return !m_config.key.isEmpty(); }

    // The call sites. Each takes ids from a closed list and drops anything else.
    // command_used, once per command and way per session: `via` is "key" or "pointer".
    Q_INVOKABLE void commandUsed(const QString &symbol, const QString &via);
    // panel_opened, once per panel per session (a dock's uniqueName).
    Q_INVOKABLE void panelOpened(const QString &dock);
    // dialog_opened, once per dialog per session (DialogWindow's dialogId).
    Q_INVOKABLE void dialogOpened(const QString &dialogId);
    // layout_changed: a preset's name (WorkspaceLayoutController::presets),
    // or "saved" for a layout the user saved.
    Q_INVOKABLE void layoutChanged(const QString &layout);
    // getting_started_finished with the pages whose settings Finish changed.
    void gettingStartedFinished(const QStringList &pagesChanged);
    void gettingStartedSkipped();
    // automation_error: `kind` load_failed, failed, lost or stopped;
    // `script` a bundled script's file name, or "" for a user script.
    void automationError(const QString &kind, const QString &bundledScript);
    // `path`'s file name when its bytes are a bundled script of that name in
    // the manifest at `manifestPath` (UserFolder::manifestName: sha256 and
    // Automation/... path per line), wherever it lies; "" otherwise. A script
    // the user dropped into a writable program folder is theirs.
    static QString bundledScript(const QString &path, const QString &manifestPath);
    // video_opened / audio_opened: `kind` file, dummy (and from_video for audio).
    void mediaOpened(bool video, qint64 milliseconds, bool ready, const QString &kind);
    // One frame's subtitles took this long to draw (app_closed's buckets);
    // from any thread. Counted only while sending is on; turning it off
    // drops the counts.
    void frameRendered(double milliseconds);

    struct StartFacts {
        QString package;     // installer, portable, appimage, deb, rpm, aur, build_tree
        int screens = 1;
        qreal scale = 1;
        qint64 startupMs = 0;
        QString layout;      // a preset's name, "saved" or ""
    };
    // app_started, once: the settings, this system and `facts`. (CR1's
    // CrashReports sends the last run's crash record as $exception.)
    void appStarted(const StartFacts &facts);
    // app_closed, then a last send that waits at most `timeoutMs` (the
    // window is gone by then), and the queue is written.
    void quit(int timeoutMs = 2000);

    // Starts a send now (the timer does it every few minutes).
    void flush();
    // What is queued, oldest first (tests).
    QJsonArray queued() const;
    QString distinctId() const;

    // The frame-time buckets' names, in order (render_frames_<name>).
    static constexpr std::array<const char *, 6> kFrameBuckets{"lt8ms", "8_16ms", "16_33ms", "33_66ms", "66_200ms",
                                                               "200ms_plus"};
    static QString timeBucket(qint64 milliseconds);
    static QString sessionBucket(qint64 seconds);
    static QString scaleBucket(qreal scale);
    static QString screensBucket(int screens);
    static int frameBucket(double milliseconds);
    // app_started's $os and arch.
    static QString osId();
    static QString archId();
    // A panel's id from its dock's uniqueName ("FixMinorErrors" =>
    // "fix_minor_errors"); "" for none of them.
    static QString panelId(const QString &dock);

signals:
    void activeChanged();
    // A send finished: `count` events left the queue (tests).
    void sent(int count);
    // A sendReport() finished: whether the endpoint took it.
    void reportSent(bool ok);

private:
    void settingChanged(const QString &id);
    void clear();
    void enqueue(QJsonObject event);
    void trim();
    void load();
    void save();
    void sendFinished();
    QJsonObject common() const;

    ui::SettingsStore &m_settings;
    Config m_config;
    bool m_enabled = false;
    QString m_sessionId;
    QJsonArray m_queue;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_reportReply;
    int m_inFlight = 0;
    QTimer m_flushTimer;
    QTimer m_saveTimer;
    QElapsedTimer m_session;
    QSet<QString> m_once;
    int m_layoutChanges = 0;
    bool m_started = false;
    bool m_closed = false;
    // Frames are counted only while active (the render thread reads it).
    std::atomic<bool> m_counting = false;
    std::array<std::atomic<qint64>, kFrameBuckets.size()> m_frames{};
};

} // namespace hikari::app
