#pragma once

#include <QString>
#include <QStringList>

namespace hikari::app {

class UserFolder {
public:
    struct Environment {
        QString userDirOverride;
        QString xdgConfigHome;
        QString xdgDataHome;
        QString xdgCacheHome;
        QString home;
        QString appData;
        QString localAppData;
        QString programFolder;
        bool portable = false;
        bool windows = false;
        static Environment current(bool portable);
    };

    UserFolder() = default;
    UserFolder(QString root, QString cache) : m_root(std::move(root)), m_cache(std::move(cache)) {}
    // HIKARISUB_USER_DIR, else `user` beside a portable program, else $XDG_CONFIG_HOME/hikarisub or
    // %APPDATA%\HikariSub. The cache: $XDG_CACHE_HOME/hikarisub or %LOCALAPPDATA%\HikariSub\cache.
    static UserFolder locate(const Environment &env);

    QString root() const { return m_root; }
    QString cache() const { return m_cache; }
    QString settingsFile() const { return m_root + QStringLiteral("/settings.toml"); }
    QString dictionaries() const { return m_root + QStringLiteral("/dictionaries"); }
    QString catalogs() const { return m_root + QStringLiteral("/catalogs"); }
    QString presets() const { return m_root + QStringLiteral("/presets"); }
    QString recovery() const { return m_root + QStringLiteral("/recovery"); }
    QString audioCache() const { return m_cache + QStringLiteral("/AudioCache"); }
    QString indices() const { return m_cache + QStringLiteral("/Indices"); }

    // Lines "<sha256 hex> <path>", the path relative to the program folder; written by
    // cmake/HikariBundledManifest.cmake.
    static QString manifestName() { return QStringLiteral("bundled-files.txt"); }
    // The path in the user folder for a bundled path; empty when it is not seeded.
    static QString seededPath(const QString &bundledPath);

    struct SeedReport {
        QStringList copied;
        QStringList updated;
        QStringList kept;
        QStringList removed;
        QStringList failed;
    };
    // Seeds the files the manifest in `programFolder` lists: a missing file is copied in, one still
    // as bundled is replaced, a changed one is kept, one no longer bundled is removed while still as
    // bundled. `.bundled-files` in the user folder records the versions. Report paths are the user
    // folder's. Nothing happens without a manifest.
    SeedReport seed(const QString &programFolder) const;

    struct MoveReport {
        QStringList from;
        QStringList leftOver; // entries not moved (the new folder had one of that name)
        QStringList keptBack; // DependencyControl's own files, left in the old folder
        QString earlierSettings; // a portable copy's: the old folder with an earlier version's hikari.ini
    };
    // The Qt folders before the user folder: ~/.config and ~/.local/share/HikariSub/HikariSub on
    // Linux, %LOCALAPPDATA% and %APPDATA%\HikariSub\HikariSub on Windows.
    struct OldFolders {
        QString config;
        QString data;
        static OldFolders of(const Environment &env);
    };
    // Once, when the user folder has no settings (recorded in `.old-folders`), moves the old
    // folders' contents in; caches go to the cache folder. Entries the user folder already has stay,
    // and so do DependencyControl's own files. A portable copy moves nothing and reports the old
    // folder holding an earlier hikari.ini.
    MoveReport moveIn(const OldFolders &old, bool portable = false) const;

private:
    QString m_root;
    QString m_cache;
};

}
