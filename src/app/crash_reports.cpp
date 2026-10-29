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

#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

constexpr qint64 kMaxRead = 1024 * 1024;

QByteArray readPrivate(const QString &path)
{
    if (path.isEmpty())
        return {};
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return {};
    struct stat st {};
    QFile file;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::getuid() || st.st_size > kMaxRead ||
        !file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
    return file.read(kMaxRead);
#else
    QFile file(path);
    if (QFileInfo(path).isSymLink() || file.size() > kMaxRead || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
#endif
}

bool privateFolder(const QString &folder)
{
    if (folder.isEmpty() || QFileInfo(folder).isSymLink() || !QDir().mkpath(folder))
        return false;
    const QFileInfo info(folder);
    if (info.isSymLink() || !info.isDir())
        return false;
#ifdef Q_OS_UNIX
    struct stat parent {};
    const QByteArray parentPath = QFile::encodeName(info.absolutePath());
    if (info.ownerId() != ::getuid() || ::stat(parentPath.constData(), &parent) != 0)
        return false;
    if (parent.st_mode & (S_IWGRP | S_IWOTH)) {
        if (parent.st_uid == ::getuid()) {
            if (::chmod(parentPath.constData(), parent.st_mode & 07755) != 0)
                return false;
        } else if (!(parent.st_mode & S_ISVTX)) {
            return false;
        }
    }
    return QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#else
    return true;
#endif
}

bool inPrivateFolder(const QString &path)
{
    return privateFolder(QFileInfo(path).absolutePath());
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
        if (!m_sending || !m_inFlight)
            return;
        const QString source = m_sendingSource;
        const report::Report sentReport = *std::exchange(m_inFlight, std::nullopt);
        const QString kind = sentReport.kind();
        const bool automatic = m_sendingAuto;
        m_sending = false;
        m_sendingAuto = false;
        writeReport(sendingFile(), std::nullopt);
        const auto *r = slot(source);
        const bool current = *r && (*r)->id == sentReport.id;
        QString kept = source;
        if (ok) {
            if (current)
                done(source);
        } else if (source != kStart) {
            if (!m_pending) {
                m_pending = sentReport;
                savePending();
                if (current)
                    m_freeze.reset();
                kept = kStart;
            } else if (!current) {
                kept.clear();
            }
            emit changed();
        }
        emit sendingChanged();
        if (automatic)
            emit autoSent(kind, ok, kept);
        else
            emit sent(kept, ok);
    });
    // MUST KILL: Analytics drops its queue when turned off, so exceptionSent is reset here
    connect(&m_analytics, &Analytics::activeChanged, this, [this] {
        if (m_analytics.active())
            return;
        for (auto *r : {&m_pending, &m_freeze})
            if (*r)
                (*r)->exceptionSent = false;
        savePending();
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
    const QStringList lastLog = report::boundLog(QString::fromUtf8(readPrivate(m_config.logTail)).split(u'\n', Qt::SkipEmptyParts));
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
        if (!sendingFile().isEmpty())
            QFile::remove(sendingFile());
        return;
    }
    if (const QByteArray json = readPrivate(m_config.pendingFile); !json.isEmpty()) {
        m_pending = report::Report::fromJson(QJsonDocument::fromJson(json).object());
        if (!m_pending || !m_pending->record) {
            m_pending.reset();
            QFile::remove(m_config.pendingFile);
        }
    }
    if (const QByteArray json = readPrivate(sendingFile()); !json.isEmpty() && !m_pending) {
        m_pending = report::Report::fromJson(QJsonDocument::fromJson(json).object());
        if (m_pending && m_pending->record)
            savePending();
        else
            m_pending.reset();
    }
    if (!sendingFile().isEmpty())
        QFile::remove(sendingFile());
}

QString CrashReports::sendingFile() const
{
    return m_config.pendingFile.isEmpty()
               ? QString()
               : QFileInfo(m_config.pendingFile).path() + QStringLiteral("/crash-report-sending.json");
}

void CrashReports::writeReport(const QString &path, const std::optional<report::Report> &r)
{
    if (path.isEmpty())
        return;
    if (!r) {
        QFile::remove(path);
        return;
    }
    QSaveFile file(path);
    if (inPrivateFolder(path) && file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(r->toJson()).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void CrashReports::savePending()
{
    writeReport(m_config.pendingFile, m_pending);
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
    if (!m_settings.boolean(kAlwaysSend) || !canSend() || !sendAuto(QStringLiteral("freeze")))
        emit freezeNoticed(record.seconds);
}

bool CrashReports::sendAuto(const QString &source)
{
    if (!send(source, {}))
        return false;
    m_sendingAuto = true;
    return true;
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
    m_inFlight = **r;
    if (source != kStart)
        writeReport(sendingFile(), m_inFlight);
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
    const QVariantMap result = open(issue);
    if (result.value(QStringLiteral("opened")).toBool())
        done(source);
    return result;
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
    const QStringList textLines = m_scrubber.scrub(text).split(u'\n');
    for (qsizetype i = 0; i < textLines.size(); ++i)
        m_tail << (i == 0 ? head + (title.isEmpty() ? QString() : m_scrubber.scrub(title) + QStringLiteral(": "))
                          : QStringLiteral("    ")) +
                      textLines[i];
    for (const QString &d : m_scrubber.scrub(details).split(u'\n', Qt::SkipEmptyParts))
        m_tail << QStringLiteral("    ") + d;
    m_tail = report::boundLog(std::move(m_tail));
    if (!m_config.logTail.isEmpty() && !m_tailTimer.isActive())
        m_tailTimer.start();
}

void CrashReports::writeTail()
{
    QSaveFile file(m_config.logTail);
    if (inPrivateFolder(m_config.logTail) && file.open(QIODevice::WriteOnly)) {
        file.write(m_tail.join(u'\n').toUtf8());
        file.commit();
    }
}

QString CrashReports::issueSystemText() const
{
    report::Report r = fresh();
    return r.systemText() + QStringLiteral("\n\n") + r.settingsText();
}

void CrashReports::beginIssue()
{
    m_issueLog = m_tail.join(u'\n');
    m_issueFile = m_currentFile ? m_currentFile() : QString();
    m_issueFileId = {};
#ifdef Q_OS_UNIX
    if (struct stat st {}; !m_issueFile.isEmpty() && ::stat(QFile::encodeName(m_issueFile).constData(), &st) == 0 &&
                           S_ISREG(st.st_mode))
        m_issueFileId = {quint64(st.st_dev), quint64(st.st_ino)};
#endif
}

QString CrashReports::issueLogText() const
{
    return m_issueLog;
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
    return open(issue(kind, title, fields, withLog));
}

QVariantMap CrashReports::open(const report::Issue &issue)
{
    const report::Issue::Link link = issue.link();
    if (!link.complete)
        m_config.copy(issue.markdown());
    const bool opened = m_config.openUrl(link.url);
    if (!opened && link.complete)
        m_config.copy(issue.markdown());
    return {{QStringLiteral("complete"), link.complete}, {QStringLiteral("opened"), opened}};
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
    return QFileInfo(m_issueFile).isFile() ? QFileInfo(m_issueFile).fileName() : QString();
}

QString CrashReports::takeScreenshot(QObject *window)
{
    auto *quick = qobject_cast<QQuickWindow *>(window);
    if (!quick || !privateFolder(m_config.attachments))
        return {};
    const QString path = m_config.attachments + QStringLiteral("/hikarisub-window.png");
    QFile::remove(path);
    const QImage image = quick->grabWindow();
    return !image.isNull() && image.save(path, "PNG") ? path : QString();
}

QString CrashReports::zipSubtitleFile()
{
    constexpr qint64 kMaxZip = 64 * 1024 * 1024;
    const QString &file = m_issueFile;
    QFile in;
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(file).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > kMaxZip || !m_issueFileId ||
        *m_issueFileId != std::pair{quint64(st.st_dev), quint64(st.st_ino)} ||
        !in.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
#else
    in.setFileName(file);
    if (!QFileInfo(file).isFile() || in.size() > kMaxZip || !in.open(QIODevice::ReadOnly))
        return {};
#endif
    if (!privateFolder(m_config.attachments))
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
