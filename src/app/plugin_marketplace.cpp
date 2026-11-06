#include "hikari/app/plugin_marketplace.h"

#include "hikari/api/api.h"
#include "hikari/backends/desktop_handoff.h"
#include "settings_store.h"

#include <QCollator>
#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

#include <algorithm>

namespace hikari::app {

namespace {

constexpr int kDayMs = 24 * 60 * 60 * 1000;
constexpr qint64 kMaxIndexBytes = 32 * 1024 * 1024;

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

std::span<const std::byte> bytes(const QByteArray &data)
{
    return {reinterpret_cast<const std::byte *>(data.constData()), std::size_t(data.size())};
}

QString origin(const QUrl &url)
{
    return url.scheme().toLower() + QStringLiteral("://") + url.host().toLower() + QLatin1Char(':') +
           QString::number(url.port(-1));
}

QString failure(const QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() && status.toInt() != 200 ? QStringLiteral("HTTP %1").arg(status.toInt()) : QString();
}

QString refusalText(const plugins::IndexRefusal &refusal)
{
    switch (refusal.kind) {
    case plugins::IndexRefusal::Kind::Signature:
        return QCoreApplication::translate("HikariSub.Plugins", "The marketplace’s list isn’t signed by the marketplace key, so HikariSub ignored it.");
    case plugins::IndexRefusal::Kind::OtherFile:
        return QCoreApplication::translate("HikariSub.Plugins", "The marketplace’s signature belongs to another file, so HikariSub ignored the list.");
    case plugins::IndexRefusal::Kind::Older:
        return QCoreApplication::translate("HikariSub.Plugins", "The marketplace sent an older list than one HikariSub has already seen, so HikariSub ignored it.");
    case plugins::IndexRefusal::Kind::Malformed:
        break;
    }
    return QCoreApplication::translate("HikariSub.Plugins", "Couldn’t read the marketplace’s list: %1").arg(text(refusal.detail));
}

QVariantMap refused(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

}

MarketplaceFeed MarketplaceFeed::hikarisub()
{
    return {QUrl(QStringLiteral("https://marketplace.hikarisub.org/")), {*minisign::parsePublicKey(plugins::kMarketplaceKey)}};
}

PluginMarketplace::PluginMarketplace(PluginManager &manager, ui::SettingsStore *settings, MarketplaceFeed feed,
                                     QString highestSerialFile, QString version, QNetworkAccessManager *network,
                                     QObject *parent)
    : QObject(parent), m_manager(manager), m_settings(settings), m_feed(std::move(feed)),
      m_highestSerialFile(std::move(highestSerialFile)), m_version(std::move(version)), m_network(network)
{
    if (!m_network) {
        m_ownNetwork = std::make_unique<QNetworkAccessManager>();
        m_network = m_ownNetwork.get();
    }
    m_timer.setInterval(kDayMs);
    connect(&m_timer, &QTimer::timeout, this, &PluginMarketplace::refresh);
    connect(&m_manager, &PluginManager::pluginsChanged, this, &PluginMarketplace::rebuild);
    if (m_settings)
        connect(m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
            if (id == QLatin1String("plugins.checkUpdates"))
                schedule();
        });
}

PluginMarketplace::~PluginMarketplace()
{
    for (QNetworkReply *reply : {m_indexReply.data(), m_signatureReply.data(), m_packageReply.data()})
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
}

void PluginMarketplace::start()
{
    schedule();
    if (m_timer.isActive())
        refresh();
}

void PluginMarketplace::schedule()
{
    if (available() && m_settings && m_settings->boolean("plugins.checkUpdates"))
        m_timer.start();
    else
        m_timer.stop();
}

bool PluginMarketplace::sameOrigin(const QUrl &url) const
{
    return url.isValid() && origin(url) == origin(m_feed.base);
}

QNetworkReply *PluginMarketplace::get(const QUrl &url, qint64 limit)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(m_version));
    request.setTransferTimeout(60 * 1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64) {
        if (received > limit)
            reply->abort();
    });
    return reply;
}

void PluginMarketplace::refresh()
{
    if (!available() || m_indexReply || m_signatureReply)
        return;
    m_status = QStringLiteral("loading");
    emit changed();
    m_indexReply = get(m_feed.base.resolved(QUrl(QStringLiteral("index.json"))), kMaxIndexBytes);
    m_signatureReply = get(m_feed.base.resolved(QUrl(QStringLiteral("index.json.minisig"))), qint64(minisign::kMaxSignatureBytes));
    connect(m_indexReply, &QNetworkReply::finished, this, &PluginMarketplace::indexFinished);
    connect(m_signatureReply, &QNetworkReply::finished, this, &PluginMarketplace::indexFinished);
}

void PluginMarketplace::indexFinished()
{
    if (!m_indexReply || !m_signatureReply || !m_indexReply->isFinished() || !m_signatureReply->isFinished())
        return;
    QNetworkReply *index = m_indexReply;
    QNetworkReply *signature = m_signatureReply;
    m_indexReply.clear();
    m_signatureReply.clear();
    index->deleteLater();
    signature->deleteLater();
    for (QNetworkReply *reply : {index, signature})
        if (const QString problem = failure(reply); !problem.isEmpty()) {
            fail(QCoreApplication::translate("HikariSub.Plugins", "HikariSub couldn’t reach the marketplace: %1").arg(problem));
            return;
        }
    const QByteArray body = index->readAll();
    const QByteArray minisig = signature->readAll();
    const std::int64_t highest = plugins::loadHighestSerial(m_highestSerialFile.toStdU16String());
    auto read = plugins::readIndex(bytes(body), std::string_view(minisig.constData(), std::size_t(minisig.size())),
                                   m_feed.keys, highest);
    if (!read) {
        fail(refusalText(read.error()));
        return;
    }
    if (read->serial > highest && !m_highestSerialFile.isEmpty())
        plugins::saveHighestSerial(m_highestSerialFile.toStdU16String(), read->serial);
    m_index = std::move(*read);
    m_status = QStringLiteral("ready");
    m_problem.clear();
    rebuild();
}

void PluginMarketplace::fail(const QString &problem)
{
    m_status = QStringLiteral("failed");
    m_problem = problem;
    emit changed();
}

void PluginMarketplace::rebuild()
{
    m_listings.clear();
    m_categories.clear();
    m_offers.clear();
    m_updates = 0;
    if (!m_index) {
        emit changed();
        return;
    }
    const plugins::Registry registry = m_manager.registry().value_or(plugins::Registry{});
    QSet<QString> categories;
    for (const plugins::Listing &listing : m_index->plugins) {
        if (listing.revoked)
            continue;
        const plugins::ListedVersion *runnable = listing.newest(api::kApiVersion);
        const auto newest = std::ranges::find_if(listing.versions, [](const plugins::ListedVersion &v) { return !v.revoked; });
        if (newest == listing.versions.end())
            continue;
        const plugins::ListedVersion &shown = runnable ? *runnable : *newest;
        const auto installed = registry.find(listing.id);
        QString action = QStringLiteral("install");
        if (!runnable)
            action = QStringLiteral("incompatible");
        else if (installed != registry.end())
            action = plugins::compareVersions(runnable->version, installed->second.version) > 0 ? QStringLiteral("update")
                                                                                                 : QStringLiteral("installed");
        QStringList listed;
        for (const std::string &category : listing.categories) {
            listed << text(category);
            categories.insert(text(category));
        }
        QVariantList screenshots;
        for (const plugins::ListedScreenshot &shot : shown.screenshots)
            if (sameOrigin(QUrl(text(shot.url))))
                screenshots << QVariantMap{{QStringLiteral("url"), QUrl(text(shot.url))}, {QStringLiteral("caption"), text(shot.caption)}};
        const auto needs = m_index->apiVersions.find(shown.api);
        const QUrl icon(text(shown.icon));
        m_listings << QVariantMap{
            {QStringLiteral("id"), text(listing.id)},
            {QStringLiteral("name"), text(shown.name)},
            {QStringLiteral("summary"), text(shown.summary)},
            {QStringLiteral("description"), text(shown.description)},
            {QStringLiteral("authorName"), text(listing.authorName)},
            {QStringLiteral("version"), text(shown.version)},
            {QStringLiteral("unrestricted"), shown.unrestricted},
            {QStringLiteral("categories"), listed},
            {QStringLiteral("permissions"), permissionLines(shown.unrestricted, plugins::readPermissions(shown.permissions))},
            {QStringLiteral("license"), text(shown.license)},
            {QStringLiteral("homepage"), text(shown.homepage)},
            {QStringLiteral("repo"), text(listing.repo)},
            {QStringLiteral("published"), text(shown.published).left(10)},
            {QStringLiteral("changelog"), text(shown.changelog)},
            {QStringLiteral("needs"), needs == m_index->apiVersions.end() ? QString() : text(needs->second)},
            {QStringLiteral("icon"), sameOrigin(icon) ? icon : QUrl()},
            {QStringLiteral("screenshots"), screenshots},
            {QStringLiteral("installedVersion"), installed == registry.end() ? QString() : text(installed->second.version)},
            {QStringLiteral("action"), action},
        };
    }
    for (const auto &[id, entry] : registry) {
        const plugins::Listing *listing = m_index->find(id);
        const plugins::ListedVersion *runnable = listing ? listing->newest(api::kApiVersion) : nullptr;
        if (!runnable)
            continue;
        const int order = plugins::compareVersions(runnable->version, entry.version);
        const bool marketplace = entry.source == plugins::Source::Marketplace;
        const QString update = marketplace && order > 0 ? text(runnable->version) : QString();
        const bool move = !marketplace && entry.key == listing->publicKey && order >= 0;
        if (!update.isEmpty())
            ++m_updates;
        if (!update.isEmpty() || move)
            m_offers.insert(text(id), QVariantMap{{QStringLiteral("update"), update}, {QStringLiteral("move"), move}});
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(m_listings, [&](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_categories = categories.values();
    std::ranges::sort(m_categories, [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    emit changed();
}

bool PluginMarketplace::openSource(const QString &id) const
{
    const plugins::Listing *listing = m_index ? m_index->find(id.toStdString()) : nullptr;
    const QUrl repo = listing ? QUrl(text(listing->repo)) : QUrl();
    return repo.scheme() == QLatin1String("https") && backends::desktop::openUrl(repo);
}

void PluginMarketplace::download(const QString &id)
{
    if (m_packageReply)
        return;
    const plugins::Listing *listing = m_index ? m_index->find(id.toStdString()) : nullptr;
    const plugins::ListedVersion *listed = listing ? listing->newest(api::kApiVersion) : nullptr;
    if (!listed) {
        emit reviewed(refused(QCoreApplication::translate("HikariSub.Plugins", "“%1” isn’t in the marketplace for this HikariSub.").arg(id)));
        return;
    }
    const QUrl url(text(listed->url));
    if (!sameOrigin(url)) {
        emit reviewed(refused(QCoreApplication::translate("HikariSub.Plugins", "The marketplace lists a download outside its own site, so HikariSub won’t fetch it.")));
        return;
    }
    m_downloading = id;
    emit downloadingChanged();
    m_packageReply = get(url, listed->size);
    connect(m_packageReply, &QNetworkReply::finished, this,
            [this, reply = m_packageReply.data(), listing = *listing, version = *listed] {
                reply->deleteLater();
                m_packageReply.clear();
                m_downloading.clear();
                emit downloadingChanged();
                if (const QString problem = failure(reply); !problem.isEmpty()) {
                    emit reviewed(refused(QCoreApplication::translate("HikariSub.Plugins", "Couldn’t download the plugin: %1").arg(problem)));
                    return;
                }
                emit reviewed(m_manager.reviewListed(reply->readAll(), listing, version));
            });
}

}
