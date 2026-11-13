#include "hikari/app/plugin_marketplace.h"

#include "hikari/api/api.h"
#include "hikari/backends/desktop_handoff.h"
#include "settings_store.h"

#include <QBuffer>
#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImageReader>
#include <QMutex>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QQuickAsyncImageProvider>
#include <QSet>

#include <algorithm>
#include <memory>
#include <utility>

namespace hikari::app {

namespace {

constexpr int kDayMs = 24 * 60 * 60 * 1000;
constexpr qint64 kMaxIndexBytes = 32 * 1024 * 1024;
constexpr qint64 kMaxIconBytes = 256 * 1024;
constexpr qint64 kMaxScreenshotBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxMediaCacheBytes = 32 * 1024 * 1024;
const QSize kMaxMediaSize(1920, 1080);

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

QVariantMap notListed(const QString &id)
{
    return refused(QCoreApplication::translate("HikariSub.Plugins", "“%1” isn’t in the marketplace for this HikariSub.").arg(id));
}

QSize fitted(QSize natural, QSize box, bool scalable)
{
    if (scalable)
        natural = natural.scaled(256, 256, Qt::KeepAspectRatio);
    if (box.width() <= 0 && box.height() <= 0)
        return natural;
    const int unbounded = kMaxMediaSize.width();
    box = QSize(box.width() > 0 ? box.width() : unbounded, box.height() > 0 ? box.height() : unbounded)
              .boundedTo(kMaxMediaSize);
    const QSize scaled = natural.scaled(box, Qt::KeepAspectRatio);
    return scalable || scaled.width() < natural.width() ? scaled : natural;
}

QImage decodeMedia(const QByteArray &data, bool icon, const QString &path, QSize size)
{
    const std::span<const std::byte> raw = bytes(data);
    const plugins::PackageFile file{path.toStdString(), {raw.begin(), raw.end()}};
    if (data.isEmpty() || (icon ? plugins::iconProblem(file) : plugins::screenshotProblem(file)))
        return {};
    const bool svg = file.path.ends_with(".svg");
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, svg ? "svg" : file.path.ends_with(".png") ? "png" : "jpeg");
    const QSize natural = reader.size();
    if (!natural.isValid() || natural.isEmpty())
        return {};
    reader.setScaledSize(fitted(natural, size, svg));
    return reader.read();
}

class MediaResponse : public QQuickImageResponse {
public:
    MediaResponse() : m_link(std::make_shared<Link>()) { m_link->response = this; }
    ~MediaResponse() override
    {
        QMutexLocker lock(&m_link->mutex);
        m_link->response = nullptr;
    }

    std::function<void(QImage)> receiver() const
    {
        return [link = m_link](QImage image) {
            QMutexLocker lock(&link->mutex);
            MediaResponse *response = std::exchange(link->response, nullptr);
            if (!response)
                return;
            response->m_image = std::move(image);
            emit response->finished();
        };
    }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("The marketplace image isn't available") : QString();
    }
    void cancel() override
    {
        QMutexLocker lock(&m_link->mutex);
        if (std::exchange(m_link->response, nullptr))
            emit finished();
    }

private:
    struct Link {
        QRecursiveMutex mutex;
        MediaResponse *response = nullptr;
    };
    std::shared_ptr<Link> m_link;
    QImage m_image;
};

class MarketplaceImageProvider : public QQuickAsyncImageProvider {
public:
    explicit MarketplaceImageProvider(PluginMarketplace &marketplace) : m_marketplace(marketplace) {}

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &size) override
    {
        auto *response = new MediaResponse;
        QMetaObject::invokeMethod(
            &m_marketplace, [marketplace = &m_marketplace, id, size, done = response->receiver()] { marketplace->image(id, size, done); },
            Qt::QueuedConnection);
        return response;
    }

private:
    PluginMarketplace &m_marketplace;
};

}

void attachMarketplaceImages(QQmlEngine &engine, PluginMarketplace &marketplace)
{
    engine.addImageProvider(QStringLiteral("marketplace"), new MarketplaceImageProvider(marketplace));
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
    connect(&m_manager, &PluginManager::pluginsChanged, this, &PluginMarketplace::queueUpdates, Qt::QueuedConnection);
    if (m_settings)
        connect(m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
            if (id == QLatin1String("plugins.checkUpdates"))
                schedule();
            if (id == QLatin1String("plugins.autoUpdate")) {
                rebuild();
                queueUpdates();
            }
        });
}

bool PluginMarketplace::automaticUpdates() const
{
    return m_settings && m_settings->boolean("plugins.autoUpdate");
}

PluginMarketplace::~PluginMarketplace()
{
    QList<QNetworkReply *> replies{m_indexReply.data(), m_signatureReply.data(), m_packageReply.data(), m_updateReply.data()};
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_mediaReplies))
        replies << reply.data();
    for (QNetworkReply *reply : replies)
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    for (const auto &waiting : std::as_const(m_mediaWaiting))
        for (const auto &decode : waiting)
            decode({});
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

QUrl PluginMarketplace::mediaSource(const QUrl &url, bool icon)
{
    if (!sameOrigin(url))
        return {};
    const QString id = (icon ? QStringLiteral("icon/") : QStringLiteral("screenshot/")) +
                       QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
    m_media.insert(id, {url, icon});
    return QUrl(QStringLiteral("image://marketplace/") + id);
}

void PluginMarketplace::image(const QString &id, QSize size, std::function<void(QImage)> done)
{
    const auto media = m_media.constFind(id);
    if (media == m_media.cend()) {
        done({});
        return;
    }
    auto decode = [icon = media->icon, path = media->url.path(), size, done = std::move(done)](const QByteArray &data) {
        done(decodeMedia(data, icon, path, size));
    };
    if (const auto cached = m_mediaBytes.constFind(id); cached != m_mediaBytes.cend()) {
        decode(*cached);
        return;
    }
    auto &waiting = m_mediaWaiting[id];
    waiting.push_back(std::move(decode));
    if (waiting.size() > 1)
        return;
    QNetworkReply *reply = get(media->url, media->icon ? kMaxIconBytes : kMaxScreenshotBytes);
    m_mediaReplies.insert(id, reply);
    connect(reply, &QNetworkReply::finished, this, [this, id] { mediaFinished(id); });
}

void PluginMarketplace::mediaFinished(const QString &id)
{
    QNetworkReply *reply = m_mediaReplies.take(id);
    if (!reply)
        return;
    reply->deleteLater();
    const QByteArray data = failure(reply).isEmpty() ? reply->readAll() : QByteArray();
    if (!data.isEmpty()) {
        if (m_mediaCached + data.size() > kMaxMediaCacheBytes) {
            m_mediaBytes.clear();
            m_mediaCached = 0;
        }
        m_mediaBytes.insert(id, data);
        m_mediaCached += data.size();
    }
    for (const auto &decode : m_mediaWaiting.take(id))
        decode(data);
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
    m_triedThisIndex.clear();
    revoke();
    rebuild();
    queueUpdates();
    followLink();
}

void PluginMarketplace::revoke()
{
    const plugins::Registry registry = m_manager.registry().value_or(plugins::Registry{});
    for (const auto &[id, entry] : registry) {
        const plugins::Listing *listing = m_index->find(id);
        if (!listing || entry.key != listing->publicKey)
            continue;
        const std::optional<plugins::Revoked> decision = plugins::revocation(*listing, entry.version, api::kApiVersion);
        if (decision == entry.revoked)
            continue;
        const bool newly = decision && !entry.revoked;
        m_manager.edit(text(id), [&](plugins::InstalledPlugin &plugin) {
            plugin.revoked = decision;
            if (decision)
                plugin.enabled = false;
        });
        if (newly)
            emit revoked(text(id), m_manager.name(text(id)), text(entry.version), text(decision->reason), text(decision->lastGood));
    }
}

void PluginMarketplace::queueUpdates()
{
    if (!m_index || !automaticUpdates())
        return;
    const plugins::Registry registry = m_manager.registry().value_or(plugins::Registry{});
    for (const auto &[id, entry] : registry) {
        const plugins::Listing *listing = m_index->find(id);
        const plugins::ListedVersion *runnable = listing ? listing->newest(api::kApiVersion) : nullptr;
        if (!runnable || entry.source != plugins::Source::Marketplace || !entry.autoUpdate || entry.revoked ||
            entry.key != listing->publicKey || plugins::compareVersions(runnable->version, entry.version) <= 0 ||
            runnable->version == entry.skip || runnable->version == entry.held)
            continue;
        const QString key = text(id) + QLatin1Char(' ') + text(runnable->version);
        if (m_triedThisIndex.contains(key) || m_queue.contains(text(id)) || m_waiting.contains(text(id)))
            continue;
        m_triedThisIndex.insert(key);
        m_queue << text(id);
    }
    nextUpdate();
}

void PluginMarketplace::nextUpdate()
{
    if (m_updateReply || m_queue.isEmpty())
        return;
    const QString id = m_queue.takeFirst();
    const plugins::Listing *listing = m_index ? m_index->find(id.toStdString()) : nullptr;
    const plugins::ListedVersion *listed = listing ? listing->newest(api::kApiVersion) : nullptr;
    const QUrl url = listed ? QUrl(text(listed->url)) : QUrl();
    if (!listed || !sameOrigin(url)) {
        nextUpdate();
        return;
    }
    m_updateReply = get(url, listed->size);
    connect(m_updateReply, &QNetworkReply::finished, this,
            [this, id, reply = m_updateReply.data(), listing = *listing, version = *listed] {
                reply->deleteLater();
                m_updateReply.clear();
                if (failure(reply).isEmpty()) {
                    PluginManager::Prepared prepared = m_manager.prepareListed(reply->readAll(), listing, version);
                    if (prepared.review && prepared.asksForMore)
                        m_manager.edit(id, [&](plugins::InstalledPlugin &plugin) { plugin.held = version.version; });
                    else if (prepared.review && m_busy && m_busy(id))
                        m_waiting.insert_or_assign(id, std::move(*prepared.review));
                    else if (prepared.review)
                        m_manager.installPrepared(*prepared.review);
                }
                nextUpdate();
            });
}

void PluginMarketplace::pluginIdle(const QString &id)
{
    const auto waiting = m_waiting.find(id);
    if (waiting == m_waiting.end() || (m_busy && m_busy(id)))
        return;
    const plugins::InstallReview review = std::move(waiting->second);
    m_waiting.erase(waiting);
    m_manager.installPrepared(review);
}

void PluginMarketplace::fail(const QString &problem)
{
    m_status = QStringLiteral("failed");
    m_problem = problem;
    emit changed();
    if (!std::exchange(m_link, QString()).isEmpty())
        emit reviewed(refused(problem));
}

void PluginMarketplace::follow(const QString &id)
{
    if (!available()) {
        emit reviewed(notListed(id));
        return;
    }
    m_link = id;
    refresh();
}

void PluginMarketplace::followLink()
{
    const QString id = std::exchange(m_link, QString());
    if (id.isEmpty())
        return;
    const plugins::Listing *listing = m_index->find(id.toStdString());
    const plugins::ListedVersion *runnable = listing ? listing->newest(api::kApiVersion) : nullptr;
    const plugins::Registry registry = m_manager.registry().value_or(plugins::Registry{});
    const auto installed = registry.find(id.toStdString());
    if (runnable && installed != registry.end() && plugins::compareVersions(runnable->version, installed->second.version) <= 0)
        return;
    download(id);
}

void PluginMarketplace::rebuild()
{
    m_listings.clear();
    m_categories.clear();
    m_offers.clear();
    m_media.clear();
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
            if (const QUrl source = mediaSource(QUrl(text(shot.url)), false); !source.isEmpty())
                screenshots << QVariantMap{{QStringLiteral("url"), source}, {QStringLiteral("caption"), text(shot.caption)}};
        const auto needs = m_index->apiVersions.find(shown.api);
        m_listings << QVariantMap{
            {QStringLiteral("id"), text(listing.id)},
            {QStringLiteral("name"), text(shown.name)},
            {QStringLiteral("summary"), text(shown.summary)},
            {QStringLiteral("description"), text(shown.description)},
            {QStringLiteral("authorName"), text(listing.authorName)},
            {QStringLiteral("official"), m_manager.officialAuthor().owns(listing.id, listing.publicKey)},
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
            {QStringLiteral("icon"), mediaSource(QUrl(text(shown.icon)), true)},
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
        const bool automatic = automaticUpdates() && entry.autoUpdate && !entry.revoked;
        if (!entry.held.empty() || (!update.isEmpty() && runnable->version != entry.skip && !automatic))
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
    offer(id, {}, false);
}

void PluginMarketplace::keepCurrent(const QString &id)
{
    m_manager.edit(id, [](plugins::InstalledPlugin &plugin) {
        plugin.skip = plugin.held;
        plugin.held.clear();
    });
}

void PluginMarketplace::installLastGood(const QString &id)
{
    const plugins::Registry registry = m_manager.registry().value_or(plugins::Registry{});
    const auto entry = registry.find(id.toStdString());
    offer(id, entry != registry.end() && entry->second.revoked ? text(entry->second.revoked->lastGood) : QString(), true);
}

void PluginMarketplace::offer(const QString &id, const QString &version, bool allowDowngrade)
{
    if (m_packageReply)
        return;
    const plugins::Listing *listing = m_index ? m_index->find(id.toStdString()) : nullptr;
    const plugins::ListedVersion *listed = nullptr;
    if (listing && version.isEmpty())
        listed = listing->newest(api::kApiVersion);
    else if (listing && !listing->revoked)
        for (const plugins::ListedVersion &candidate : listing->versions)
            if (text(candidate.version) == version && !candidate.revoked && candidate.api <= api::kApiVersion)
                listed = &candidate;
    if (!listed) {
        emit reviewed(notListed(id));
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
            [this, allowDowngrade, reply = m_packageReply.data(), listing = *listing, version = *listed] {
                reply->deleteLater();
                m_packageReply.clear();
                m_downloading.clear();
                emit downloadingChanged();
                if (const QString problem = failure(reply); !problem.isEmpty()) {
                    emit reviewed(refused(QCoreApplication::translate("HikariSub.Plugins", "Couldn’t download the plugin: %1").arg(problem)));
                    return;
                }
                emit reviewed(m_manager.reviewListed(reply->readAll(), listing, version, allowDowngrade));
            });
}

}
