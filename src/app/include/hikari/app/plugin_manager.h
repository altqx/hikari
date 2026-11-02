#pragma once

#include "hikari/plugins/installer.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace hikari::app {

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
    // Rows {id, name, version, description, enabled, keyId ("" when unsigned), unrestricted}, by name.
    QVariantList plugins() const { return m_rows; }
    // Why plugins.json cannot be read; "" when it can.
    QString problem() const { return m_problem; }

    // Reads a .hikariplugin and the .minisig beside it, and keeps the result for install().
    // Refused: {ok: false, message}. Otherwise {ok: true, id, name, version, change ("install",
    // "update", "reinstall", "downgrade"), installedVersion, keyId, pinnedKeyId, needsRetrust,
    // unrestricted, permissions: [text]}.
    Q_INVOKABLE QVariantMap review(const QUrl &file);
    // Installs what review() last accepted; "" on success, else the problem.
    Q_INVOKABLE QString install(bool retrusted);
    Q_INVOKABLE void cancelReview() { m_review.reset(); }
    Q_INVOKABLE QString setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool hasData(const QString &id) const;
    Q_INVOKABLE QString uninstall(const QString &id, bool removeData);

signals:
    void pluginsChanged();

private:
    void refresh();

    std::optional<plugins::Installer> m_installer;
    std::optional<plugins::InstallReview> m_review;
    QVariantList m_rows;
    QString m_problem;
};

}
