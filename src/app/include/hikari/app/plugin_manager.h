#pragma once

#include "hikari/plugins/installer.h"
#include "hikari/plugins/marketplace.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace hikari::app {

// The plugin manager's permission lines, as the install question lists them.
QStringList permissionLines(bool unrestricted, const plugins::Permissions &permissions, bool qmlPanels = false);

class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY pluginsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    struct Folders {
        QString plugins;
        QString data;
        QString cache;
        QString registry;
    };
    // Empty folders: nothing can be installed.
    explicit PluginManager(Folders folders, QObject *parent = nullptr);

    bool available() const { return m_installer.has_value(); }
    const plugins::OfficialAuthor &officialAuthor() const { return m_official; }
    void setOfficialAuthor(plugins::OfficialAuthor official);
    // Rows {id, name, version, description, enabled, keyId ("" when unsigned), source ("file" or "marketplace"),
    // unrestricted, official (signed by officialAuthor() under its handle), icon (a file URL, or empty), authors, homepage, permissions: [text], adds: [text], settings: [the
    // manifest's setting with its value], folders: [{id, title, path, writable}] remembered for it, autoUpdate,
    // previous (the version Roll back returns to, or ""), held (a newer version waiting for consent, or ""),
    // revoked, revokedReason, lastGood (the version to offer instead of a revoked one, or "")}, by name.
    QVariantList plugins() const { return m_rows; }
    // Why plugins.json cannot be read; "" when it can.
    QString problem() const { return m_problem; }

    // Reads a .hikariplugin and the .minisig beside it, and keeps the result for install().
    // Refused: {ok: false, message}. Otherwise {ok: true, id, name, version, change ("install",
    // "update", "reinstall", "downgrade"), installedVersion, keyId, pinnedKeyId, needsRetrust,
    // unrestricted, permissions: [text], marketplace (false), changes: [{text, added}] (over an
    // installed copy: every permission it asks for, added when the installed copy didn't),
    // asksForMore}.
    Q_INVOKABLE QVariantMap review(const QUrl &file);
    // review() for a marketplace download: checks it against the listing, which then vouches for the
    // author key. Adds {marketplace: true, signedBy: the listing's authorName, changelog}.
    QVariantMap reviewListed(const QByteArray &package, const plugins::Listing &listing,
                             const plugins::ListedVersion &listed, bool allowDowngrade = false);
    struct Prepared {
        std::optional<plugins::InstallReview> review;
        QString problem;
        bool asksForMore = false;
    };
    // reviewListed() without touching the review install() takes, for automatic updates.
    Prepared prepareListed(const QByteArray &package, const plugins::Listing &listing,
                           const plugins::ListedVersion &listed, bool allowDowngrade = false) const;
    // Installs a prepared update while the installed version is still the one it was prepared over.
    QString installPrepared(const plugins::InstallReview &review);
    std::expected<plugins::Registry, std::string> registry() const;
    QString name(const QString &id) const;
    // Changes the plugins.json entry only; "" on success, else the problem.
    QString edit(const QString &id, const std::function<void(plugins::InstalledPlugin &)> &change);
    // Installs what review() last accepted; "" on success, else the problem.
    Q_INVOKABLE QString install(bool retrusted);
    Q_INVOKABLE QString setAutoUpdate(const QString &id, bool on);
    // Returns to the version kept by the last update, which automatic updates then pass over.
    Q_INVOKABLE QString rollBack(const QString &id);
    // Takes back rollBack(): the version left returns and is no longer passed over.
    Q_INVOKABLE QString undoRollBack(const QString &id);
    Q_INVOKABLE void cancelReview() { m_review.reset(); }
    // review() for a package that must have the id `id`.
    Q_INVOKABLE QVariantMap reviewUpdate(const QUrl &file, const QString &id);
    Q_INVOKABLE QString setEnabled(const QString &id, bool enabled);
    // `json` is the value as JSON text; "" when it is kept, else why not.
    Q_INVOKABLE QString setSetting(const QString &id, const QString &setting, const QString &json);
    Q_INVOKABLE bool hasData(const QString &id) const;
    // The plugin's data folder, made when missing; empty when it can't be.
    QUrl dataFolder(const QString &id) const;
    Q_INVOKABLE bool openDataFolder(const QString &id) const;
    Q_INVOKABLE QString uninstall(const QString &id, bool removeData);
    // "" when the plugin no longer has the folder, else why not.
    Q_INVOKABLE QString forgetFolder(const QString &id, const QString &folder);

signals:
    void pluginsChanged();
    void folderForgotten(const QString &id);
    void settingChanged(const QString &id, const QString &setting, const QString &json);
    // `version` is the one now installed.
    void rolledBack(const QString &id, const QString &name, const QString &version);

private:
    void refresh();
    QVariantMap describe(const plugins::InstallReview &review) const;
    QVariantMap reviewed(std::expected<plugins::InstallReview, plugins::InstallRefusal> result);

    std::optional<plugins::Installer> m_installer;
    std::optional<plugins::InstallReview> m_review;
    plugins::OfficialAuthor m_official = plugins::OfficialAuthor::hikarisub();
    QVariantList m_rows;
    QString m_problem;
};

}
