#pragma once

#include "hikari/api/host.h"
#include "hikari/plugins/installer.h"
#include "hikari/plugins/marketplace.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <optional>

class QFileSystemWatcher;
class QTimer;

namespace hikari::app {

// The plugin manager's permission lines, as the install question lists them.
QStringList permissionLines(bool unrestricted, const plugins::Permissions &permissions, bool qmlPanels = false,
                            const std::vector<plugins::Program> &programs = {},
                            const std::vector<plugins::McpServer> &mcpServers = {});

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
        QString programs;
    };
    // Empty folders: nothing can be installed.
    explicit PluginManager(Folders folders, QObject *parent = nullptr);

    bool available() const { return m_installer.has_value(); }
    // plugins.json; empty without a user folder.
    const QString &registryFile() const { return m_registryFile; }
    const plugins::OfficialAuthor &officialAuthor() const { return m_official; }
    void setOfficialAuthor(plugins::OfficialAuthor official);
    // Rows {id, name, version, description, enabled, keyId ("" when unsigned), source ("file", "marketplace" or "dev"),
    // unrestricted, official (signed by officialAuthor() under its handle), icon (a file URL, or empty), authors, homepage, permissions: [text], adds: [text], settings: [the
    // manifest's setting with its value], folders: [{id, title, path, writable}] remembered for it, autoUpdate,
    // previous (the version Roll back returns to, or ""), held (a newer version waiting for consent, or ""),
    // revoked, revokedReason, lastGood (the version to offer instead of a revoked one, or ""), path (a dev plugin's
    // folder, else ""), folder (path as a file URL), overrides (the installed version a dev plugin stands in for, or ""), devProblem (why its last
    // reload failed, or ""), devAsks (its files ask for more than the user said yes to)}, by name.
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
    // review() for a folder run in place as a dev plugin. Adds {dev: true, path}.
    Q_INVOKABLE QVariantMap reviewDev(const QUrl &folder);
    Q_INVOKABLE void cancelReview() { m_review.reset(); }
    // review() for a package that must have the id `id`.
    Q_INVOKABLE QVariantMap reviewUpdate(const QUrl &file, const QString &id);
    Q_INVOKABLE QString setEnabled(const QString &id, bool enabled);
    // `json` is the value as JSON text; "" when it is kept, else why not.
    Q_INVOKABLE QString setSetting(const QString &id, const QString &setting, const QString &json);
    // The settings of the plugins that are turned on.
    std::vector<api::PluginSettingState> settingStates() const;
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
    // A dev plugin's files changed and the new ones were loaded.
    void devReloaded(const QString &id);
    // A dev plugin's files changed but weren't loaded: they break a rule, or ask for more than the user said yes to.
    void devProblem(const QString &id, const QString &name, const QString &message);

private:
    void refresh();
    QVariantMap describe(const plugins::InstallReview &review) const;
    QVariantMap reviewed(std::expected<plugins::InstallReview, plugins::InstallRefusal> result);
    void watchDevFolders();
    void devFilesChanged(const QString &path);
    void reloadDev(const QString &id);

    std::optional<plugins::Installer> m_installer;
    std::optional<plugins::InstallReview> m_review;
    std::filesystem::path m_reviewedFolder;
    QFileSystemWatcher *m_devWatcher = nullptr;
    QHash<QString, QString> m_devFolders;
    QHash<QString, QTimer *> m_devTimers;
    QHash<QString, QString> m_devProblems;
    QSet<QString> m_devAsks;
    plugins::OfficialAuthor m_official = plugins::OfficialAuthor::hikarisub();
    QVariantList m_rows;
    QString m_problem;
    QString m_registryFile;
};

}
