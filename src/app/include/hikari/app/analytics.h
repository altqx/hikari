#pragma once

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
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    struct Config {
        // Empty: nothing is queued or sent.
        QString key;
        QUrl endpoint = defaultEndpoint();
        // Empty: the queue lives in memory only.
        QString queueFile;
        QString version;
        bool doNotTrack = false;
        int flushIntervalMs = 5 * 60 * 1000;
        int firstFlushMs = 60 * 1000;
    };
    static QUrl defaultEndpoint() { return QUrl(QStringLiteral("https://eu.i.posthog.com/batch/")); }
    static QUrl whatIsSentUrl()
    {
        return QUrl(QStringLiteral("https://hikarisub.altqx.com/en/docs/help/what-is-sent"));
    }
    // Empty when built without HIKARI_POSTHOG_KEY.
    static QString compiledKey();
    // True for any DO_NOT_TRACK value but unset, "" or "0".
    static bool doNotTrack(const QByteArray &value);

    // Queue limits; the oldest events are dropped first.
    static constexpr int kMaxEvents = 1000;
    static constexpr qint64 kMaxBytes = 1024 * 1024;
    static constexpr int kBatchSize = 100;
    static constexpr int kMaxLayoutChanges = 20;

    Analytics(ui::SettingsStore &settings, Config config, QObject *parent = nullptr);
    ~Analytics() override;

    bool active() const;

    // Returns whether the event was queued: sending is on and the event conforms.
    bool capture(const QString &event, const QJsonObject &properties = {});
    static bool conforms(const QString &event, const QJsonObject &properties);

    // Sends `events` at once in their own batch, even with usage data off (then under a
    // one-off id). False without a key, with a report in flight or when an event does not
    // conform; otherwise reportSent() follows.
    bool sendReport(const QList<std::pair<QString, QJsonObject>> &events);
    bool canSendReports() const { return !m_config.key.isEmpty(); }

    // Once per command and way per session; `via` is "key" or "pointer".
    Q_INVOKABLE void commandUsed(const QString &symbol, const QString &via);
    // Once per panel per session; `dock` is a dock's uniqueName.
    Q_INVOKABLE void panelOpened(const QString &dock);
    // Once per dialog per session.
    Q_INVOKABLE void dialogOpened(const QString &dialogId);
    // `layout`: a preset's name, or "saved" for a layout the user saved.
    Q_INVOKABLE void layoutChanged(const QString &layout);
    void gettingStartedFinished(const QStringList &pagesChanged);
    void gettingStartedSkipped();
    // `kind`: load_failed, failed, lost or stopped; `bundledScript`: "" for a user script.
    void automationError(const QString &kind, const QString &bundledScript);
    // `path`'s file name when its bytes are a bundled script of that name in the manifest
    // at `manifestPath` (sha256 and Automation/... path per line); "" otherwise.
    static QString bundledScript(const QString &path, const QString &manifestPath);
    // `kind`: file or dummy, and for audio also from_video.
    void mediaOpened(bool video, qint64 milliseconds, bool ready, const QString &kind);
    // Callable from any thread; counted only while sending is on.
    void frameRendered(double milliseconds);

    struct StartFacts {
        QString package;     // installer, portable, appimage, deb, rpm, aur, build_tree
        int screens = 1;
        qreal scale = 1;
        qint64 startupMs = 0;
        QString layout;      // a preset's name, "saved" or ""
    };
    // Only the first call sends.
    void appStarted(const StartFacts &facts);
    // Blocks up to `timeoutMs` for the last send, then writes the queue.
    void quit(int timeoutMs = 2000);

    void flush();
    // Oldest first.
    QJsonArray queued() const;
    QString distinctId() const;

    static constexpr std::array<const char *, 6> kFrameBuckets{"lt8ms", "8_16ms", "16_33ms", "33_66ms", "66_200ms",
                                                               "200ms_plus"};
    static QString timeBucket(qint64 milliseconds);
    static QString sessionBucket(qint64 seconds);
    static QString scaleBucket(qreal scale);
    static QString screensBucket(int screens);
    static int frameBucket(double milliseconds);
    static QString osId();
    static QString archId();
    // "FixMinorErrors" => "fix_minor_errors"; "" for an unknown dock.
    static QString panelId(const QString &dock);

signals:
    void activeChanged();
    // `count` events left the queue.
    void sent(int count);
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
    std::atomic<bool> m_counting = false;
    std::array<std::atomic<qint64>, kFrameBuckets.size()> m_frames{};
};

} // namespace hikari::app
