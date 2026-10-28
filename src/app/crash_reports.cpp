#include "hikari/app/crash_reports.h"

#include "hikari/app/analytics.h"
#include "hikari/backends/desktop_handoff.h"
#include "log_controller.h"
#include "settings_store.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSaveFile>
#include <QUuid>
#include <QtCore/private/qzipwriter_p.h>

namespace hikari::app {

namespace {

const QString kStart = QStringLiteral("start");

QString severityName(int severity)
{
    switch (severity) {
    case ui::LogController::Error: return QStringLiteral("Error");
    case ui::LogController::Warning: return QStringLiteral("Warning");
    default: return QStringLiteral("Info");
    }
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        return {};
    QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
    if (lines.size() > report::kLogLines)
        lines = lines.mid(lines.size() - report::kLogLines);
    return lines;
}

} // namespace

CrashReports::CrashReports(ui::SettingsStore &settings, Analytics &analytics, ui::LogController &log, Config config,
                           QObject *parent)
    : QObject(parent), m_settings(settings), m_analytics(analytics), m_config(std::move(config))
{
    if (!m_config.openUrl)
        m_config.openUrl = [](const QUrl &url) { return backends::desktop::openUrl(url); };
    if (!m_config.copy)
        m_config.copy = [](const QString &text) {
            if (auto *clipboard = QGuiApplication::clipboard())
                clipboard->setText(text);
        };
    m_system = report::System::current(m_config.version, {}, 1, 1, {});
    collect();
    m_tailTimer.setSingleShot(true);
    m_tailTimer.setInterval(500);
    connect(&m_tailTimer, &QTimer::timeout, this, &CrashReports::writeTail);
    connect(&log, &ui::LogController::added, this, &CrashReports::entryAdded);
    connect(&m_analytics, &Analytics::reportSent, this, [this](bool ok) {
        if (!m_sending)
            return;
        const QString source = m_sendingSource;
        const QString kind = source == kStart ? pending() : QStringLiteral("freeze");
        const bool automatic = m_sendingAuto;
        m_sending = false;
        m_sendingAuto = false;
        if (ok) {
            done(source);
        } else if (source != kStart && m_freeze) {
            // Kept for the next start; nothing tries again before.
            if (!m_pending) {
                m_pending = m_freeze;
                savePending();
            }
            m_freeze.reset();
            emit changed();
        }
        emit sendingChanged();
        if (automatic)
            emit autoSent(kind, ok);
        else
            emit sent(source, ok);
    });
}

void CrashReports::setKnown(std::function<QStringList()> names, std::function<QStringList()> texts)
{
    m_scrubber.setKnownNames(std::move(names));
    m_scrubber.setKnownTexts(std::move(texts));
}

report::Report CrashReports::fresh() const
{
    report::Report r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.system = m_system;
    r.settings = report::settingsSummary(m_settings);
    return r;
}

void CrashReports::collect()
{
    // The last run's Log, then the records it left: a crash, or a freeze
    // it was closed in (the later, when both).
    const QStringList lastLog = readLines(m_config.logTail);
    QFile::remove(m_config.logTail);
    std::optional<crash::Record> record;
    QDateTime when;
    for (const QString &path : {m_config.crashRecord, m_config.freezeRecord}) {
        if (path.isEmpty() || !QFile::exists(path))
            continue;
        const QDateTime modified = QFileInfo(path).lastModified();
        if (auto r = crash::read(path); r && (!record || modified > when)) {
            record = r;
            when = modified;
        }
        QFile::remove(path);
    }
    if (record) {
        m_pending = fresh();
        m_pending->record = record;
        m_pending->when = when;
        m_pending->log = lastLog;
        savePending();
        return;
    }
    QFile file(m_config.pendingFile);
    if (!m_config.pendingFile.isEmpty() && file.size() < 1024 * 1024 && file.open(QIODevice::ReadOnly)) {
        m_pending = report::Report::fromJson(QJsonDocument::fromJson(file.readAll()).object());
        if (!m_pending || !m_pending->record) {
            m_pending.reset();
            QFile::remove(m_config.pendingFile);
        }
    }
}

void CrashReports::savePending()
{
    if (m_config.pendingFile.isEmpty())
        return;
    if (!m_pending) {
        QFile::remove(m_config.pendingFile);
        return;
    }
    QSaveFile file(m_config.pendingFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_pending->toJson()).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void CrashReports::started(const QString &package, int screens, qreal scale, const QString &layout)
{
    m_system = report::System::current(m_config.version, package, screens, scale, layout);
    if (m_started)
        return;
    m_started = true;
    if (!m_pending)
        return;
    m_pending->system = m_system;
    m_pending->settings = report::settingsSummary(m_settings);
    // Usage data: the record alone, as AN1 sent it; Send report adds the rest.
    if (!m_pending->exceptionSent && m_analytics.active() &&
        m_analytics.capture(QStringLiteral("$exception"), m_pending->exceptionProperties()))
        m_pending->exceptionSent = true;
    savePending();
    emit changed();
    if (m_settings.boolean(kAlwaysSend) && canSend())
        sendAuto(kStart);
}

void CrashReports::freezeRecovered(const crash::Record &record)
{
    m_freeze = fresh();
    m_freeze->record = record;
    m_freeze->when = QDateTime::currentDateTime().addSecs(-record.seconds);
    m_freeze->log = m_tail;
    m_freeze->recovered = true;
    if (m_analytics.active() && m_analytics.capture(QStringLiteral("$exception"), m_freeze->exceptionProperties()))
        m_freeze->exceptionSent = true;
    emit changed();
    if (m_settings.boolean(kAlwaysSend) && canSend())
        sendAuto(QStringLiteral("freeze"));
    else
        emit freezeNoticed(record.seconds);
}

void CrashReports::sendAuto(const QString &source)
{
    if (send(source, {}))
        m_sendingAuto = true;
}

QString CrashReports::pending() const
{
    return m_pending ? m_pending->kind() : QString();
}

bool CrashReports::canSend() const
{
    return m_analytics.canSendReports();
}

std::optional<report::Report> *CrashReports::slot(const QString &source)
{
    return source == kStart ? &m_pending : &m_freeze;
}

const std::optional<report::Report> *CrashReports::slot(const QString &source) const
{
    return source == kStart ? &m_pending : &m_freeze;
}

QDateTime CrashReports::when(const QString &source) const
{
    const auto *r = slot(source);
    return *r ? (*r)->when : QDateTime();
}

int CrashReports::seconds(const QString &source) const
{
    const auto *r = slot(source);
    return *r && (*r)->record ? (*r)->record->seconds : 0;
}

QString CrashReports::text(const QString &source, const QString &note) const
{
    const auto *r = slot(source);
    return *r ? (*r)->text(note) : QString();
}

bool CrashReports::send(const QString &source, const QString &note)
{
    auto *r = slot(source);
    if (!*r || m_sending || !canSend())
        return false;
    QList<std::pair<QString, QJsonObject>> events;
    if (!(*r)->exceptionSent)
        events.append({QStringLiteral("$exception"), (*r)->exceptionProperties()});
    events.append({QStringLiteral("crash_report"), (*r)->reportProperties(note)});
    if (!m_analytics.sendReport(events))
        return false;
    m_sentText = (*r)->text(note);
    m_sending = true;
    m_sendingSource = source;
    emit sendingChanged();
    return true;
}

QVariantMap CrashReports::openOnGitHub(const QString &source, const QString &note)
{
    const auto *r = slot(source);
    if (!*r)
        return {};
    const report::Issue issue = report::Issue::fromReport(**r, note);
    const report::Issue::Link link = issue.link();
    if (!link.complete)
        m_config.copy(issue.markdown());
    m_config.openUrl(link.url);
    done(source);
    return {{QStringLiteral("complete"), link.complete}};
}

void CrashReports::dismiss(const QString &source)
{
    done(source);
}

void CrashReports::done(const QString &source)
{
    if (source == kStart) {
        m_pending.reset();
        savePending();
    } else {
        m_freeze.reset();
    }
    emit changed();
}

void CrashReports::entryAdded(const QString &time, int severity, const QString &title, const QString &text,
                              const QString &details)
{
    const QString head = time + u' ' + severityName(severity).leftJustified(8, u' ');
    const QStringList textLines = text.split(u'\n');
    for (qsizetype i = 0; i < textLines.size(); ++i)
        m_tail << (i == 0 ? head + (title.isEmpty() ? QString() : m_scrubber.scrub(title) + QStringLiteral(": "))
                          : QStringLiteral("    ")) +
                      m_scrubber.scrub(textLines[i]);
    for (const QString &d : details.split(u'\n', Qt::SkipEmptyParts))
        m_tail << QStringLiteral("    ") + m_scrubber.scrub(d);
    if (m_tail.size() > report::kLogLines)
        m_tail = m_tail.mid(m_tail.size() - report::kLogLines);
    if (!m_config.logTail.isEmpty())
        m_tailTimer.start();
}

void CrashReports::writeTail()
{
    QSaveFile file(m_config.logTail);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_tail.join(u'\n').toUtf8());
        file.commit();
    }
}

QString CrashReports::issueSystemText() const
{
    report::Report r = fresh();
    return r.systemText() + QStringLiteral("\n\n") + r.settingsText();
}

QString CrashReports::issueLogText() const
{
    return m_tail.join(u'\n');
}

report::Issue CrashReports::issue(const QString &kind, const QString &title, const QVariantMap &fields,
                                  bool withLog) const
{
    report::Issue i;
    QStringList ids;
    if (kind == QLatin1String("feature")) {
        i.kind = report::Issue::Feature;
        ids = {QStringLiteral("wanted"), QStringLiteral("why")};
    } else if (kind == QLatin1String("question")) {
        i.kind = report::Issue::Question;
        ids = {QStringLiteral("question")};
    } else {
        ids = {QStringLiteral("what-happened"), QStringLiteral("steps"), QStringLiteral("expected")};
    }
    i.title = title.trimmed();
    for (const QString &id : std::as_const(ids))
        i.fields.append({id, fields.value(id).toString()});
    i.fields.append({QStringLiteral("system"), issueSystemText()});
    if (withLog && i.kind == report::Issue::Bug)
        i.fields.append({QStringLiteral("log"), issueLogText()});
    return i;
}

QString CrashReports::issueMarkdown(const QString &kind, const QString &title, const QVariantMap &fields,
                                    bool withLog) const
{
    return issue(kind, title, fields, withLog).markdown();
}

QVariantMap CrashReports::openIssue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog)
{
    const report::Issue i = issue(kind, title, fields, withLog);
    const report::Issue::Link link = i.link();
    if (!link.complete)
        m_config.copy(i.markdown());
    m_config.openUrl(link.url);
    return {{QStringLiteral("complete"), link.complete}};
}

void CrashReports::copyIssue(const QString &kind, const QString &title, const QVariantMap &fields, bool withLog)
{
    m_config.copy(issueMarkdown(kind, title, fields, withLog));
}

QString CrashReports::systemSummary() const
{
    return QStringLiteral("HikariSub %1 · %2 · %3 %4").arg(m_system.version, m_system.package, m_system.os, m_system.osVersion);
}

QString CrashReports::subtitleFileName() const
{
    const QString file = m_currentFile ? m_currentFile() : QString();
    return QFileInfo(file).isFile() ? QFileInfo(file).fileName() : QString();
}

QString CrashReports::takeScreenshot(QObject *window)
{
    auto *quick = qobject_cast<QQuickWindow *>(window);
    if (!quick || m_config.attachments.isEmpty() || !QDir().mkpath(m_config.attachments))
        return {};
    const QString path = m_config.attachments + QStringLiteral("/hikarisub-window.png");
    const QImage image = quick->grabWindow();
    return !image.isNull() && image.save(path, "PNG") ? path : QString();
}

QString CrashReports::zipSubtitleFile()
{
    const QString file = m_currentFile ? m_currentFile() : QString();
    QFile in(file);
    if (m_config.attachments.isEmpty() || !QFileInfo(file).isFile() || in.size() > 64 * 1024 * 1024 ||
        !in.open(QIODevice::ReadOnly) || !QDir().mkpath(m_config.attachments))
        return {};
    const QString name = QFileInfo(file).fileName();
    const QString path = m_config.attachments + u'/' + QFileInfo(file).completeBaseName() + QStringLiteral(".zip");
    QFile::remove(path);
    QZipWriter zip(path);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(name, in.readAll());
    zip.close();
    return zip.status() == QZipWriter::NoError ? path : QString();
}

void CrashReports::copyImage(const QString &path)
{
    if (auto *clipboard = QGuiApplication::clipboard(); clipboard && !path.isEmpty())
        clipboard->setImage(QImage(path));
}

void CrashReports::discardAttachments()
{
    if (!m_config.attachments.isEmpty())
        QDir(m_config.attachments).removeRecursively();
}

} // namespace hikari::app
