#include "hikari/app/update_checker.h"

#include "hikari/application/update_check.h"
#include "hikari/backends/desktop_handoff.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <optional>

namespace hikari::app {

namespace {

namespace a = application;

constexpr int kReleasesPerPage = 50;
constexpr int kMaxPages = 4;
constexpr qint64 kMaxListBytes = 4 * 1024 * 1024;
constexpr int kCheckDeadlineMs = 2 * 60 * 1000;

std::optional<std::int64_t> integer(const QJsonValue &v)
{
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (d != std::floor(d) || std::abs(d) > 9.0e15)
        return std::nullopt;
    return std::int64_t(d);
}

std::optional<std::string> text(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    return v.toString().toStdString();
}

std::vector<a::ReleaseEntry> releaseList(const QJsonArray &list, bool &unreadable)
{
    std::vector<a::ReleaseEntry> out;
    for (const QJsonValue &value : list) {
        const QJsonObject r = value.toObject();
        if (!value.isObject() || r.value(QStringLiteral("tag_name")).toString().isEmpty()) {
            unreadable = true;
            continue;
        }
        a::ReleaseEntry entry;
        entry.tag = r.value(QStringLiteral("tag_name")).toString().toStdString();
        const QJsonValue name = r.value(QStringLiteral("name"));
        entry.name = name.isString() && !name.toString().isEmpty() ? name.toString().toStdString() : entry.tag;
        entry.notes = r.value(QStringLiteral("body")).toString().toStdString();
        entry.published = r.value(QStringLiteral("published_at")).toString().toStdString();
        entry.draft = r.value(QStringLiteral("draft")).toBool();
        entry.prerelease = r.value(QStringLiteral("prerelease")).toBool();
        for (const QJsonValue &asset : r.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject o = asset.toObject();
            entry.assets.push_back({o.value(QStringLiteral("name")).toString().toStdString(),
                                    integer(o.value(QStringLiteral("size"))).value_or(0),
                                    o.value(QStringLiteral("digest")).toString().toStdString()});
        }
        out.push_back(std::move(entry));
    }
    return out;
}

a::ManifestFields manifestFields(const QByteArray &body)
{
    a::ManifestFields f;
    const QJsonObject m = QJsonDocument::fromJson(body).object();
    f.schema = integer(m.value(QStringLiteral("schema")));
    f.version = text(m.value(QStringLiteral("version")));
    f.codebase = text(m.value(QStringLiteral("codebase")));
    if (const QJsonValue to = m.value(QStringLiteral("offer_to")); to.isArray()) {
        std::vector<std::string> names;
        bool strings = true;
        for (const QJsonValue &v : to.toArray()) {
            strings = strings && v.isString();
            names.push_back(v.toString().toStdString());
        }
        if (strings)
            f.offerTo = std::move(names);
    }
    const QJsonValue packages = m.value(QStringLiteral("packages"));
    f.packagesIsObject = packages.isObject();
    const QJsonObject p = packages.toObject();
    for (auto it = p.begin(); it != p.end(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject o = it.value().toObject();
        a::ManifestFields::Package package;
        package.name = text(o.value(QStringLiteral("name")));
        package.sha256 = text(o.value(QStringLiteral("sha256")));
        package.minOs = text(o.value(QStringLiteral("min_os")));
        package.minGlibc = text(o.value(QStringLiteral("min_glibc")));
        package.size = integer(o.value(QStringLiteral("size")));
        f.packages.emplace(it.key().toStdString(), std::move(package));
    }
    return f;
}

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString offerName(a::OfferKind kind)
{
    switch (kind) {
    case a::OfferKind::Install:
        return QStringLiteral("install");
    case a::OfferKind::Requirement:
        return QStringLiteral("requirement");
    case a::OfferKind::Aur:
        return QStringLiteral("aur");
    case a::OfferKind::Notify:
        break;
    }
    return QStringLiteral("notify");
}

bool hasAsset(const a::ReleaseEntry &release, std::string_view name)
{
    return std::any_of(release.assets.begin(), release.assets.end(), [&](const auto &asset) { return asset.name == name; });
}

QString origin(const QUrl &url)
{
    return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo).toString();
}

}

struct UpdateChecker::Run {
    bool interactive = false;
    bool listOnly = false;
    // MUST KILL: tri-state: unset while reading, false when a page was not a list
    std::optional<bool> listRead;
    bool unreadable = false;
    int page = 0;
    std::vector<a::ReleaseEntry> releases;
    std::vector<a::ReleaseEntry> candidates;
    std::size_t next = 0;
    QPointer<QNetworkReply> reply;
    qint64 cap = 0;
    QTimer deadline;
};

UpdateFeed UpdateFeed::github()
{
    UpdateFeed feed;
    feed.releases = QUrl(QStringLiteral("https://api.github.com/repos/altqx/hikari/releases?per_page=%1").arg(kReleasesPerPage));
    feed.downloadBase = QStringLiteral("https://github.com/altqx/hikari/releases/download/");
    feed.pageBase = QStringLiteral("https://github.com/altqx/hikari/releases/tag/");
    feed.paginate = true;
    return feed;
}

UpdateFeed UpdateFeed::local(const QUrl &base)
{
    QString root = base.toString();
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    UpdateFeed feed;
    feed.releases = QUrl(root + QStringLiteral("releases.json"));
    feed.downloadBase = root + QStringLiteral("download/");
    feed.pageBase = root + QStringLiteral("releases/tag/");
    return feed;
}

UpdateChecker::UpdateChecker(QString settingsFile, UpdateFeed feed, QString version, UpdateEnvironment environment,
                             QObject *parent)
    : QObject(parent), m_ownedSettings(std::make_unique<ui::SettingsStore>(std::move(settingsFile))),
      m_settings(m_ownedSettings.get()), m_feed(std::move(feed)), m_version(std::move(version)),
      m_installer(std::make_unique<UpdateInstaller>(std::move(environment), m_version))
{
    load();
    connect(m_installer.get(), &UpdateInstaller::failed, this, [this](const QString &message) { emit notice(message, false); });
}

UpdateChecker::UpdateChecker(ui::SettingsStore &settings, UpdateFeed feed, QString version,
                             UpdateEnvironment environment, QObject *parent)
    : QObject(parent), m_settings(&settings), m_feed(std::move(feed)), m_version(std::move(version)),
      m_installer(std::make_unique<UpdateInstaller>(std::move(environment), m_version))
{
    load();
    connect(m_installer.get(), &UpdateInstaller::failed, this, [this](const QString &message) { emit notice(message, false); });
}

UpdateChecker::~UpdateChecker()
{
    if (m_run && m_run->reply) {
        m_run->reply->disconnect(this);
        m_run->reply->abort();
    }
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
    m_autoInstall = m_settings->boolean("updater.autoInstall");
    m_nextCheck = m_settings->integer64("updater.nextCheck");
    m_skippedVersion = m_settings->text("updater.skippedVersion");
}

void UpdateChecker::save() const
{
    m_settings->set("updater.autoCheck", m_autoCheck);
    m_settings->set("updater.checkForStable", m_stableOnly);
    m_settings->set("updater.autoInstall", m_autoInstall);
    m_settings->set("updater.nextCheck", m_nextCheck);
    m_settings->set("updater.skippedVersion", m_skippedVersion);
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

void UpdateChecker::setAutoInstall(bool on)
{
    if (m_autoInstall == on)
        return;
    m_autoInstall = on;
    save();
    emit optionsChanged();
}

void UpdateChecker::skipVersion(const QString &version)
{
    if (m_skippedVersion == version)
        return;
    m_skippedVersion = version;
    save();
    emit optionsChanged();
}

QString UpdateChecker::aurCommand() const
{
    const UpdateEnvironment &env = m_installer->environment();
    return qs(a::aurUpdateCommand(env.aurHelper.toStdString(), env.package.aurChannel));
}

bool UpdateChecker::aur() const
{
    return m_installer->environment().package.kind == a::PackageKind::Arch;
}

void UpdateChecker::setNextCheck(qint64 secondsSinceEpoch)
{
    m_nextCheck = secondsSinceEpoch;
    save();
}

QString UpdateChecker::versionLabel() const
{
    return QStringLiteral("%1 (%2)").arg(m_version, packageKindLabel(m_installer->environment().package.kind));
}

bool UpdateChecker::stableOnlyOption() const
{
    const PackageInfo &package = m_installer->environment().package;
    return a::hasStableOnlyOption(package.kind, package.aurChannel);
}

bool UpdateChecker::installsUpdates() const
{
    return a::installsUpdates(m_installer->environment().package.kind);
}

bool UpdateChecker::checking() const
{
    return m_run && !m_run->listOnly;
}

bool UpdateChecker::stableOnlyApplied() const
{
    const PackageInfo &package = m_installer->environment().package;
    return a::stableOnlyFor(m_stableOnly, package.kind, package.aurChannel);
}

void UpdateChecker::checkNow()
{
    check(true);
}

void UpdateChecker::readReleases()
{
    if (!m_run)
        start(true, false);
    else if (m_run->listRead)
        emit releasesRead(*m_run->listRead);
}

bool UpdateChecker::checkOnStartup()
{
    if (!a::automaticCheckDue(m_autoCheck, QDateTime::currentSecsSinceEpoch(), m_nextCheck))
        return false;
    check(false);
    return true;
}

void UpdateChecker::startUp()
{
    if (const QString message = m_installer->start(); !message.isEmpty())
        emit notice(message, true);
}

void UpdateChecker::remindInAWeek()
{
    setNextCheck(QDateTime::currentSecsSinceEpoch() + a::kUpdateWeek);
}

void UpdateChecker::openReleasePage(const QString &url)
{
    if (!url.isEmpty() && url.startsWith(m_feed.pageBase))
        backends::desktop::openUrl(QUrl(url));
}

void UpdateChecker::downloadOffer()
{
    if (!m_offerInstallable)
        return;
    m_installer->download(m_offerVersion, m_offerPackage,
                          QUrl(assetUrl(QStringLiteral("v") + m_offerVersion, qs(m_offerPackage.name))), true);
}

QString UpdateChecker::assetUrl(const QString &tag, const QString &asset) const
{
    if (!a::isValidUrlTag(tag.toStdString()) || !a::isValidAssetName(asset.toStdString()))
        return {};
    return m_feed.downloadBase + tag + QLatin1Char('/') + asset;
}

QString UpdateChecker::pageUrl(const QString &tag) const
{
    return a::isValidUrlTag(tag.toStdString()) ? m_feed.pageBase + tag : QString();
}

QNetworkReply *UpdateChecker::get(const QUrl &url, bool api)
{
    QNetworkRequest request(url);
    if (api) {
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_version));
    request.setTransferTimeout(60 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    const QString feedOrigin = origin(m_feed.releases);
    const bool github = m_feed.paginate;
    connect(reply, &QNetworkReply::redirected, this, [reply, api, github, feedOrigin](const QUrl &to) {
        // GitHub's API answers from its own host; assets redirect to its download hosts.
        const bool https = to.scheme() == QLatin1String("https") && to.port() == -1 && to.userInfo().isEmpty();
        const bool allowed = github ? https && (api ? to.host().compare(QLatin1String("api.github.com"), Qt::CaseInsensitive) == 0
                                                    : a::isAllowedDownloadHost(to.host().toStdString()))
                                    : origin(to) == feedOrigin;
        if (allowed)
            emit reply->redirectAllowed();
        else
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64) {
        if (m_run && received > m_run->cap)
            reply->abort();
    });
    return reply;
}

void UpdateChecker::check(bool interactive)
{
    if (m_run) {
        if (m_run->listOnly) {
            m_run->listOnly = false;
            m_run->interactive = interactive;
            emit checkingChanged();
        }
        return;
    }
    start(false, interactive);
}

void UpdateChecker::start(bool listOnly, bool interactive)
{
    m_run = std::make_unique<Run>();
    m_run->listOnly = listOnly;
    m_run->interactive = interactive;
    m_run->deadline.setSingleShot(true);
    connect(&m_run->deadline, &QTimer::timeout, this, [this] {
        // MUST KILL: deferred because the abort ends the run that owns this timer
        if (m_run && m_run->reply)
            QTimer::singleShot(0, m_run->reply.data(), &QNetworkReply::abort);
    });
    m_run->deadline.start(kCheckDeadlineMs);
    if (!listOnly)
        emit checkingChanged();
    requestPage(1);
}

void UpdateChecker::requestPage(int page)
{
    QUrl url = m_feed.releases;
    if (page > 1) {
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("page"), QString::number(page));
        url.setQuery(query);
    }
    m_run->page = page;
    m_run->cap = kMaxListBytes;
    m_run->reply = get(url, true);
    connect(m_run->reply, &QNetworkReply::finished, this, &UpdateChecker::pageFinished);
}

void UpdateChecker::pageFinished()
{
    QNetworkReply *reply = m_run->reply;
    m_run->reply = nullptr;
    reply->deleteLater();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (reply->error() != QNetworkReply::NoError || (status.isValid() && status.toInt() != 200)) {
        end(QStringLiteral("failed"));
        emit releasesRead(false);
        return;
    }
    const QByteArray body = reply->readAll();
    if (body.size() > kMaxListBytes) {
        end(QStringLiteral("failed"));
        emit releasesRead(false);
        return;
    }
    const QJsonDocument json = QJsonDocument::fromJson(body);
    m_run->unreadable = m_run->unreadable || !json.isArray();
    const qsizetype listed = json.array().size();
    auto page = releaseList(json.array(), m_run->unreadable);
    const bool full = listed >= kReleasesPerPage;
    m_run->releases.insert(m_run->releases.end(), std::make_move_iterator(page.begin()),
                           std::make_move_iterator(page.end()));
    if (m_feed.paginate && full && m_run->page < kMaxPages) {
        requestPage(m_run->page + 1);
        return;
    }
    const bool read = !m_run->unreadable;
    if (read)
        m_releases = m_run->releases;
    m_run->listRead = read;
    if (m_run->listOnly) {
        end(QString());
        emit releasesRead(read);
        return;
    }
    emit releasesRead(read);
    m_run->candidates = a::updateCandidates(m_run->releases, m_version.toStdString(), stableOnlyApplied());
    if (m_run->candidates.size() > a::kMaxCandidates)
        m_run->candidates.resize(a::kMaxCandidates);
    m_run->next = 0;
    walk();
}

void UpdateChecker::walk()
{
    while (m_run->next < m_run->candidates.size()) {
        const a::ReleaseEntry &candidate = m_run->candidates[m_run->next];
        if (hasAsset(candidate, a::kManifestAsset)) {
            const QString url = assetUrl(qs(candidate.tag), qs(std::string(a::kManifestAsset)));
            m_run->cap = a::kMaxManifestBytes;
            m_run->reply = get(QUrl(url), false);
            connect(m_run->reply, &QNetworkReply::finished, this, &UpdateChecker::manifestFinished);
            return;
        }
        ++m_run->next;
        const auto packages = a::fallbackPackages(candidate);
        if (a::offeredTo(packages, a::Codebase::Qt)) {
            offer(candidate, packages);
            return;
        }
    }
    end(QStringLiteral("current"));
}

void UpdateChecker::manifestFinished()
{
    QNetworkReply *reply = m_run->reply;
    m_run->reply = nullptr;
    reply->deleteLater();
    const a::ReleaseEntry candidate = m_run->candidates[m_run->next++];
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QByteArray body = reply->error() == QNetworkReply::NoError && (!status.isValid() || status.toInt() == 200)
                                ? reply->readAll()
                                : QByteArray();
    if (body.isEmpty() || body.size() > a::kMaxManifestBytes) {
        walk();
        return;
    }
    const auto manifest = a::readManifest(manifestFields(body), candidate);
    if (manifest.status == a::ManifestStatus::Invalid) {
        walk();
        return;
    }
    const auto packages =
        manifest.status == a::ManifestStatus::Valid ? manifest.packages : a::fallbackPackages(candidate);
    if (!a::offeredTo(packages, a::Codebase::Qt)) {
        walk();
        return;
    }
    offer(candidate, packages);
}

void UpdateChecker::offer(const a::ReleaseEntry &release, const a::ReleasePackages &packages)
{
    const UpdateEnvironment &env = m_installer->environment();
    auto chosen = a::chooseOffer(env.package.kind, onWindows(), packages, env.osVersion.toStdString(),
                                 env.glibcVersion.toStdString());
    if (chosen.kind == a::OfferKind::Install && !m_installer->canInstall())
        chosen.kind = a::OfferKind::Notify;
    const QString tag = qs(release.tag);
    const QString version = tag.mid(1);
    m_offerVersion = version;
    m_offerPackage = chosen.package;
    m_offerInstallable = chosen.kind == a::OfferKind::Install;
    const bool interactive = m_run->interactive;
    const QVariantMap map{{QStringLiteral("name"), qs(release.name)},
                          {QStringLiteral("tag"), tag},
                          {QStringLiteral("version"), version},
                          {QStringLiteral("url"), pageUrl(tag)},
                          {QStringLiteral("notes"), qs(release.notes)},
                          {QStringLiteral("prerelease"), a::isPrerelease(release)},
                          {QStringLiteral("aurCarries"), a::aurCarries(a::isPrerelease(release), env.package.aurChannel)},
                          {QStringLiteral("offer"), offerName(chosen.kind)},
                          {QStringLiteral("requirement"), qs(chosen.requirement)},
                          {QStringLiteral("requirementIsOs"), chosen.requirementIsOs}};
    end(QString());
    if (!interactive && !m_skippedVersion.isEmpty() && version == m_skippedVersion)
        return;
    if (m_offerInstallable && !interactive && m_autoInstall) {
        downloadOffer();
        return;
    }
    emit finished(QStringLiteral("available"), map, interactive);
}

void UpdateChecker::end(const QString &outcome)
{
    const bool interactive = m_run->interactive;
    const bool listOnly = m_run->listOnly;
    if (QNetworkReply *reply = m_run->reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_run.reset();
    if (listOnly)
        return;
    emit checkingChanged();
    setNextCheck(QDateTime::currentSecsSinceEpoch() + a::kUpdateDay);
    if (interactive && !outcome.isEmpty())
        emit finished(outcome, {}, true);
}

}
