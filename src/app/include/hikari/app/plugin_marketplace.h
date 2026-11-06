#pragma once

#include "hikari/app/plugin_manager.h"
#include "hikari/plugins/marketplace.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

class QNetworkReply;

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

struct MarketplaceFeed {
    // Holds index.json and index.json.minisig; packages, icons and screenshots must share its origin.
    // Empty: no marketplace.
    QUrl base;
    std::vector<minisign::PublicKey> keys;
    static MarketplaceFeed hikarisub();
};

class PluginMarketplace : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    // "", "loading", "ready" or "failed".
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QVariantList listings READ listings NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
    Q_PROPERTY(QVariantMap offers READ offers NOTIFY changed)
    Q_PROPERTY(int updates READ updates NOTIFY changed)
    // The plugin id being downloaded; "" for none.
    Q_PROPERTY(QString downloading READ downloading NOTIFY downloadingChanged)

public:
    // `network` null: its own. `highestSerialFile` keeps the highest index serial accepted.
    PluginMarketplace(PluginManager &manager, ui::SettingsStore *settings, MarketplaceFeed feed, QString highestSerialFile,
                      QString version, QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~PluginMarketplace() override;

    bool available() const { return !m_feed.base.isEmpty(); }
    QString status() const { return m_status; }
    QString problem() const { return m_problem; }
    // Rows {id, name, summary, description, authorName, version, unrestricted, categories, permissions: [text], license,
    // homepage, repo, published (yyyy-MM-dd), changelog, needs (the first HikariSub version with its API, or ""), icon,
    // screenshots: [{url, caption}], installedVersion, action ("install", "update", "installed" or "incompatible")},
    // newest version first per plugin, by name. Revoked plugins and versions are left out.
    QVariantList listings() const { return m_listings; }
    QStringList categories() const { return m_categories; }
    // By installed id: {update: a newer listed version for a marketplace install, else "", move: true for a file
    // install the marketplace lists with the same author key}.
    QVariantMap offers() const { return m_offers; }
    int updates() const { return m_updates; }
    QString downloading() const { return m_downloading; }

    // Fetches the whole index; never sends which plugins are installed.
    Q_INVOKABLE void refresh();
    // Refreshes now and every 24 hours while plugins.checkUpdates is on.
    void start();
    // Downloads the newest version `id` can run and reports the review through reviewed().
    Q_INVOKABLE void download(const QString &id);
    // Opens the listing's source repository; only https links open.
    Q_INVOKABLE bool openSource(const QString &id) const;

signals:
    void changed();
    void downloadingChanged();
    // PluginManager::reviewListed's answer; install with PluginManager::install.
    void reviewed(const QVariantMap &review);

private:
    QNetworkReply *get(const QUrl &url, qint64 limit);
    void indexFinished();
    void fail(const QString &problem);
    void rebuild();
    void schedule();
    bool sameOrigin(const QUrl &url) const;

    PluginManager &m_manager;
    ui::SettingsStore *m_settings;
    MarketplaceFeed m_feed;
    QString m_highestSerialFile;
    QString m_version;
    std::unique_ptr<QNetworkAccessManager> m_ownNetwork;
    QNetworkAccessManager *m_network;
    QTimer m_timer;
    QPointer<QNetworkReply> m_indexReply;
    QPointer<QNetworkReply> m_signatureReply;
    QPointer<QNetworkReply> m_packageReply;
    std::optional<plugins::MarketIndex> m_index;
    QString m_status;
    QString m_problem;
    QVariantList m_listings;
    QStringList m_categories;
    QVariantMap m_offers;
    int m_updates = 0;
    QString m_downloading;
};

}
