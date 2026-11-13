#pragma once

#include "hikari/application/recent_files.h"
#include "hikari/application/session_file.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <map>
#include <optional>
#include <string>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// UNC paths and network drives count as present without a check.
bool recentPathMissing(const std::string &path, bool folder);

// Each shell window's Project (ADR 0030), Recent projects, and each Project's last tabs, kept in
// the user folder's `projects/` and never in `.hikari/`.
class ProjectWindows : public QObject {
    Q_OBJECT
public:
    ProjectWindows(ui::SettingsStore &settings, QString userFolder, QObject *parent = nullptr);

    // The folder with links resolved and native separators; empty when `path` is no folder.
    static QString canonicalFolder(const QString &path);

    QString root(int window) const;
    // Every window's Project folder.
    QStringList roots() const;
    QString name(int window) const;
    std::optional<int> windowOf(const QString &root) const;
    // An empty root clears the window's Project; a set one goes first in Recent projects.
    void set(int window, const QString &root);
    void forget(int window);

    QVariantList recentRows() const;
    bool anyRecent() const { return !m_recent.entries().empty(); }
    void remove(const QString &path);
    void removeMissing();

    void keepTabs(const QString &root, application::Session session) const;
    std::optional<application::Session> lastTabs(const QString &root) const;

signals:
    void changed(int window);
    void recentChanged();

private:
    QString tabsFile(const QString &root) const;
    void storeRecent();

    ui::SettingsStore &m_settings;
    QString m_userFolder;
    std::map<int, QString> m_roots;
    application::RecentFiles m_recent;
};

}
