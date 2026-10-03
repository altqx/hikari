#include "hikari/app/update_checker.h"

#include "hikari/application/update_check.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace hikari::app {

namespace {

// The release list as legacy FindNewerRelease reads it; anything but an
// array gives no releases.
std::vector<application::ReleaseEntry> releases(const QByteArray &body)
{
    std::vector<application::ReleaseEntry> out;
    const QJsonDocument json = QJsonDocument::fromJson(body);
    if (!json.isArray())
        return out;
    for (const QJsonValue &value : json.array()) {
        const QJsonObject r = value.toObject();
        application::ReleaseEntry entry;
        entry.tag = r.value(QStringLiteral("tag_name")).toString().toStdString();
        const QJsonValue name = r.value(QStringLiteral("name"));
        entry.name = name.isString() ? name.toString().toStdString() : entry.tag;
        entry.url = r.value(QStringLiteral("html_url")).toString().toStdString();
        entry.notes = r.value(QStringLiteral("body")).toString().toStdString();
        entry.draft = r.value(QStringLiteral("draft")).toBool();
        entry.prerelease = r.value(QStringLiteral("prerelease")).toBool();
        out.push_back(std::move(entry));
    }
    return out;
}

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

} // namespace

QUrl UpdateChecker::defaultFeed()
{
    return QUrl(QStringLiteral("https://api.github.com/repos/altqx/hikari/releases?per_page=10"));
}

UpdateChecker::UpdateChecker(QString settingsFile, QUrl feed, QString version, QObject *parent)
    : QObject(parent), m_ownedSettings(std::make_unique<ui::SettingsStore>(std::move(settingsFile))),
      m_settings(m_ownedSettings.get()), m_feed(std::move(feed)), m_version(std::move(version))
{
    load();
}

UpdateChecker::UpdateChecker(ui::SettingsStore &settings, QUrl feed, QString version, QObject *parent)
    : QObject(parent), m_settings(&settings), m_feed(std::move(feed)), m_version(std::move(version))
{
    load();
}

void UpdateChecker::reload()
{
    load();
    emit optionsChanged();
}

void UpdateChecker::load()
{
    m_autoCheck = m_settings->boolean("updater.autoCheck");
    m_stableOnly = m_settings->boolean("updater.checkForStable");
    m_nextCheck = m_settings->integer64("updater.nextCheck");
}

void UpdateChecker::save() const
{
    m_settings->set("updater.autoCheck", m_autoCheck);
    m_settings->set("updater.checkForStable", m_stableOnly);
    m_settings->set("updater.nextCheck", m_nextCheck);
}

void UpdateChecker::setAutoCheck(bool on)
{
    if (m_autoCheck == on)
        return;
    m_autoCheck = on;
    save();
    emit optionsChanged();
}

void UpdateChecker::setStableOnly(bool on)
{
    if (m_stableOnly == on)
        return;
    m_stableOnly = on;
    save();
    emit optionsChanged();
}

void UpdateChecker::setNextCheck(qint64 secondsSinceEpoch)
{
    m_nextCheck = secondsSinceEpoch;
    save();
}

void UpdateChecker::checkNow()
{
    check(true);
}

bool UpdateChecker::checkOnStartup()
{
    if (!application::automaticCheckDue(m_autoCheck, QDateTime::currentSecsSinceEpoch(), m_nextCheck))
        return false;
    check(false);
    return true;
}

void UpdateChecker::remindInAWeek()
{
    setNextCheck(QDateTime::currentSecsSinceEpoch() + application::kUpdateWeek);
}

void UpdateChecker::openReleasePage(const QString &url)
{
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void UpdateChecker::check(bool interactive)
{
    if (m_reply)
        return;
    QNetworkRequest request(m_feed);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_version));
    request.setTransferTimeout(30000);
    m_reply = m_network.get(request);
    emit checkingChanged();
    connect(m_reply, &QNetworkReply::finished, this, [this, interactive] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        emit checkingChanged();
        // Any finished check backs the automatic one off a day, so a server
        // that is down does not mean a request on every start.
        setNextCheck(QDateTime::currentSecsSinceEpoch() + application::kUpdateDay);
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                emit finished(QStringLiteral("failed"), {}, true);
            return;
        }
        const auto found =
            application::findNewerRelease(releases(reply->readAll()), m_version.toStdString(), m_stableOnly);
        if (!found) {
            if (interactive)
                emit finished(QStringLiteral("current"), {}, true);
            return;
        }
        emit finished(QStringLiteral("available"),
                      {{QStringLiteral("name"), qs(found->name)},
                       {QStringLiteral("tag"), qs(found->tag)},
                       {QStringLiteral("url"), qs(found->url)},
                       {QStringLiteral("notes"), qs(found->notes)}},
                      interactive);
    });
}

} // namespace hikari::app
