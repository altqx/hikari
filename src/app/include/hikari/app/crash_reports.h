#pragma once

// CR1 (#452): the crash, freeze and issue reports (crash_report.h), and what
// the windows do with them. Nothing leaves the machine but by the user's
// Send report (Analytics::sendReport, usage data on or off) or Open on
// GitHub (a prefilled issue in the browser); with usage data on, the
// record alone goes as $exception at the start after it, as AN1 sent it.
// * The start after a crash or a freeze the app was closed in: the record
//   it left (crash-record.txt, hang-record.txt) and that run's Log lines
//   become the pending report, kept (crash-report.json) until it is sent,
//   taken to GitHub or dismissed. "Always send crash reports" sends it at
//   once instead (autoSent).
// * A freeze the window came back from (HangWatchdog::recovered): a report
//   in memory, offered by a notice (freezeRecovered).
// * The Log tail: every Log entry, scrubbed as it comes, the last
//   report::kLogLines lines kept in log-tail.txt, so the next start has
//   the Log of a run that crashed.
// Each method that acts on a report takes its source: "start" (the
// pending one) or "freeze".

#include "hikari/app/crash_report.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace hikari::ui {
class LogController;
class SettingsStore;
} // namespace hikari::ui

namespace hikari::app {

class Analytics;

class CrashReports : public QObject {
    Q_OBJECT
    // The pending report's kind: "", "crash" or "freeze".
    Q_PROPERTY(QString pending READ pending NOTIFY changed)
    // A freeze the window came back from is offered.
    Q_PROPERTY(bool freezeOffered READ freezeOffered NOTIFY changed)
    // A build with a PostHog key can send; any build can go to GitHub.
    Q_PROPERTY(bool canSend READ canSend CONSTANT)
    Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
public:
    struct Config {
        QString crashRecord;  // AN1's crash-record.txt
        QString freezeRecord; // HangWatchdog's hang-record.txt
        QString pendingFile;  // crash-report.json
        QString logTail;      // log-tail.txt
        QString attachments;  // the issue's screenshot and zip, until Done
        QString version;
        // Opens a link in the browser and puts text on the clipboard
        // (tests: fakes).
        std::function<bool(const QUrl &)> openUrl;
        std::function<void(const QString &)> copy;
    };
    static constexpr char kAlwaysSend[] = "privacy.alwaysSendCrashReports";

    CrashReports(ui::SettingsStore &settings, Analytics &analytics, ui::LogController &log, Config config,
                 QObject *parent = nullptr);

    // The names of the files open now and their Lines' texts (the scrubber's).
    void setKnown(std::function<QStringList()> names, std::function<QStringList()> texts);
    // The editing target's subtitle file, "" without one (Report an issue).
    void setCurrentFile(std::function<QString()> file) { m_currentFile = std::move(file); }
    // Once the window shows: this system. A pending report is completed,
    // its $exception queued while usage data is on, and with "Always send"
    // it is sent.
    void started(const QString &package, int screens, qreal scale, const QString &layout);
    // HangWatchdog::recovered.
    void freezeRecovered(const crash::Record &record);

    QString pending() const;
    bool freezeOffered() const { return m_freeze.has_value(); }
    bool canSend() const;
    bool sending() const { return m_sending; }

    // When it crashed or froze, and for a freeze how long (s).
    Q_INVOKABLE QDateTime when(const QString &source) const;
    Q_INVOKABLE int seconds(const QString &source) const;
    // The whole report as it goes, with the note; its one-line summary.
    Q_INVOKABLE QString text(const QString &source, const QString &note) const;
    // Send report: false when it could not start (no key, already sending).
    Q_INVOKABLE bool send(const QString &source, const QString &note);
    // Open GitHub issue: a Bug issue with the same content. Returns
    // {complete}: false when it was too long for a link and went to the
    // clipboard, the blank form opening instead. The report is then done.
    Q_INVOKABLE QVariantMap openOnGitHub(const QString &source, const QString &note);
    // Don't send: the report goes, nothing is sent.
    Q_INVOKABLE void dismiss(const QString &source);
    // The last report sent, as it went ("Always send"'s Show report).
    Q_INVOKABLE QString sentText() const { return m_sentText; }

    // Help › Report an issue. `kind`: bug, feature or question; `fields`
    // the form's by id. System and settings always go; the Log's lines
    // when `withLog`.
    Q_INVOKABLE QString issueSystemText() const;
    Q_INVOKABLE QString issueLogText() const;
    Q_INVOKABLE QString issueMarkdown(const QString &kind, const QString &title, const QVariantMap &fields,
                                      bool withLog) const;
    Q_INVOKABLE QVariantMap openIssue(const QString &kind, const QString &title, const QVariantMap &fields,
                                      bool withLog);
    Q_INVOKABLE void copyIssue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog);
    // The one-line summary of the system ("HikariSub V · package · os V").
    Q_INVOKABLE QString systemSummary() const;
    // The attachments a link cannot carry, made only when the user ticks
    // them and kept in the cache folder until discardAttachments (Done or
    // Cancel): the window as a PNG (before the form opens over it), the
    // editing target's file in a .zip. Each returns its path, "" when it
    // could not be made.
    Q_INVOKABLE QString subtitleFileName() const;
    Q_INVOKABLE QString takeScreenshot(QObject *window);
    Q_INVOKABLE QString zipSubtitleFile();
    Q_INVOKABLE void copyImage(const QString &path);
    Q_INVOKABLE void discardAttachments();

    // The current run's Log lines, scrubbed (tests).
    QStringList logTail() const { return m_tail; }

signals:
    void changed();
    void sendingChanged();
    // A Send report finished.
    void sent(const QString &source, bool ok);
    // A freeze the window came back from, offered (the notice).
    void freezeNoticed(int seconds);
    // "Always send" sent the start's report (or a freeze's) by itself.
    void autoSent(const QString &kind, bool ok);

private:
    std::optional<report::Report> *slot(const QString &source);
    const std::optional<report::Report> *slot(const QString &source) const;
    report::Report fresh() const;
    void collect();
    void savePending();
    void done(const QString &source);
    void entryAdded(const QString &time, int severity, const QString &title, const QString &text,
                    const QString &details);
    void writeTail();
    report::Issue issue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog) const;
    void sendAuto(const QString &source);

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
    QTimer m_tailTimer;
    bool m_started = false;
    bool m_sending = false;
    QString m_sendingSource;
    bool m_sendingAuto = false;
};

} // namespace hikari::app
