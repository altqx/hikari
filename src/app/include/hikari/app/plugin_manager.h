#pragma once

#include "hikari/plugins/installer.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace hikari::app {

// The plugin manager's permission lines, as the install question lists them.
QStringList permissionLines(bool unrestricted, const plugins::Permissions &permissions);

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
    // Rows {id, name, version, description, enabled, keyId ("" when unsigned), source ("file" or "marketplace"),
    // unrestricted, icon (a file URL, or empty), authors, homepage, permissions: [text], adds: [text], settings: [the
    // manifest's setting with its value]}, by name.
    QVariantList plugins() const { return m_rows; }
    // Why plugins.json cannot be read; "" when it can.
    QString problem() const { return m_problem; }

    // Reads a .hikariplugin and the .minisig beside it, and keeps the result for install().
    // Refused: {ok: false, message}. Otherwise {ok: true, id, name, version, change ("install",
    // "update", "reinstall", "downgrade"), installedVersion, keyId, pinnedKeyId, needsRetrust,
    // unrestricted, permissions: [text], marketplace (false)}.
    Q_INVOKABLE QVariantMap review(const QUrl &file);
    // review() for a marketplace download: checks it against the listing, which then vouches for the
    // author key. Adds {marketplace: true, signedBy: the listing's authorName}.
    QVariantMap reviewListed(const QByteArray &package, const plugins::Listing &listing,
                             const plugins::ListedVersion &listed);
    std::expected<plugins::Registry, std::string> registry() const;
    // Installs what review() last accepted; "" on success, else the problem.
    Q_INVOKABLE QString install(bool retrusted);
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

signals:
    void pluginsChanged();
    void settingChanged(const QString &id, const QString &setting, const QString &json);

private:
    void refresh();
    QVariantMap reviewed(std::expected<plugins::InstallReview, plugins::InstallRefusal> result);

    std::optional<plugins::Installer> m_installer;
    std::optional<plugins::InstallReview> m_review;
    QVariantList m_rows;
    QString m_problem;
};

}
