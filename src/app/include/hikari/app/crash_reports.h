#pragma once

#include "hikari/app/crash_report.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <utility>

namespace hikari::ui {
class LogController;
class SettingsStore;
}

namespace hikari::app {

class Analytics;

class CrashReports : public QObject {
    Q_OBJECT
    // "", "crash" or "freeze".
    Q_PROPERTY(QString pending READ pending NOTIFY changed)
    Q_PROPERTY(bool freezeOffered READ freezeOffered NOTIFY changed)
    // True when the build has a PostHog key.
    Q_PROPERTY(bool canSend READ canSend CONSTANT)
    Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
    // In characters.
    Q_PROPERTY(int noteLimit READ noteLimit CONSTANT)
public:
    struct Config {
        QString crashRecord;
        QString freezeRecord;
        QString pendingFile;
        QString logTail;
        // A folder for the issue's screenshot and zip, until discardAttachments.
        QString attachments;
        QString version;
        std::function<bool(const QUrl &)> openUrl;
        std::function<void(const QString &)> copy;
    };
    static constexpr char kAlwaysSend[] = "privacy.alwaysSendCrashReports";

    CrashReports(ui::SettingsStore &settings, Analytics &analytics, ui::LogController &log, Config config,
                 QObject *parent = nullptr);

    // Callbacks giving the open files' names and their Lines' texts, which reports scrub.
    void setKnown(std::function<QStringList()> names, std::function<QStringList()> texts);
    // The callback returns "" without an editing target file.
    void setCurrentFile(std::function<QString()> file) { m_currentFile = std::move(file); }
    // Call once the window shows: completes a pending report and, with "Always send", sends it.
    void started(const QString &package, int screens, qreal scale, const QString &layout);
    void freezeRecovered(const crash::Record &record);

    QString pending() const;
    bool freezeOffered() const { return m_freeze.has_value(); }
    bool canSend() const;
    bool sending() const { return m_sending; }
    static int noteLimit() { return report::kNoteChars; }

    // `source`: "start" (the pending report) or "freeze". seconds() is a freeze's length in s.
    Q_INVOKABLE QDateTime when(const QString &source) const;
    Q_INVOKABLE int seconds(const QString &source) const;
    Q_INVOKABLE QString text(const QString &source, const QString &note) const;
    // False when it could not start (no key, already sending).
    Q_INVOKABLE bool send(const QString &source, const QString &note);
    // Returns {complete, opened}: complete false when too long for a link (the issue is copied,
    // the blank form opens); opened false when no browser opened.
    Q_INVOKABLE QVariantMap openOnGitHub(const QString &source, const QString &note);
    Q_INVOKABLE void dismiss(const QString &source);
    Q_INVOKABLE QString sentText() const { return m_sentText; }

    // `kind`: "bug", "feature" or "question"; `fields` maps the form's field ids to values.
    // beginIssue, called as the form opens, snapshots the Log and the editing target's file.
    Q_INVOKABLE void beginIssue();
    Q_INVOKABLE QString issueSystemText() const;
    Q_INVOKABLE QString issueLogText() const;
    Q_INVOKABLE QString issueMarkdown(const QString &kind, const QString &title, const QVariantMap &fields,
                                      bool withLog) const;
    Q_INVOKABLE QVariantMap openIssue(const QString &kind, const QString &title, const QVariantMap &fields,
                                      bool withLog);
    Q_INVOKABLE void copyIssue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog);
    // "HikariSub V · package · os V".
    Q_INVOKABLE QString systemSummary() const;
    // takeScreenshot and zipSubtitleFile return the file's path, "" on failure; the files stay
    // until discardAttachments.
    Q_INVOKABLE QString subtitleFileName() const;
    Q_INVOKABLE QString takeScreenshot(QObject *window);
    Q_INVOKABLE QString zipSubtitleFile();
    Q_INVOKABLE void copyImage(const QString &path);
    Q_INVOKABLE void discardAttachments();

    QStringList logTail() const { return m_tail; }

signals:
    void changed();
    void sendingChanged();
    // On failure the report stays in `source`, "" when a newer freeze took its place.
    void sent(const QString &source, bool ok);
    void freezeNoticed(int seconds);
    // `source` as in sent().
    void autoSent(const QString &kind, bool ok, const QString &source);

private:
    std::optional<report::Report> *slot(const QString &source);
    const std::optional<report::Report> *slot(const QString &source) const;
    report::Report fresh() const;
    void collect();
    void savePending();
    QString sendingFile() const;
    static void writeReport(const QString &path, const std::optional<report::Report> &r);
    void done(const QString &source);
    void entryAdded(const QString &time, int severity, const QString &title, const QString &text,
                    const QString &details);
    void writeTail();
    report::Issue issue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog) const;
    bool sendAuto(const QString &source);
    QVariantMap open(const report::Issue &issue);

    ui::SettingsStore &m_settings;
    Analytics &m_analytics;
    Config m_config;
    report::Scrubber m_scrubber;
    report::System m_system;
    std::optional<report::Report> m_pending;
    std::optional<report::Report> m_freeze;
    QStringList m_tail;
    QString m_sentText;
    std::function<QString()> m_currentFile;
    QString m_issueLog;
    QString m_issueFile;
    std::optional<std::pair<quint64, quint64>> m_issueFileId;
    QTimer m_tailTimer;
    bool m_started = false;
    bool m_sending = false;
    QString m_sendingSource;
    std::optional<report::Report> m_inFlight;
    bool m_sendingAuto = false;
};

}
