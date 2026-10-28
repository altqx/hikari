#pragma once

// UD1 (#362): the user folder, Aegisub's `?user`: everything a user may edit
// or want to keep, in one place.
//
//   hikari.ini
//   automation/autoload, automation/include   (?user; DependencyControl's too)
//   dictionaries/   Hunspell .dic/.aff, UserDic.udic
//   catalogs/       style and font catalogs
//   last-session.json   the last session (CF3; LastSession.txt.old once converted)
//   presets/        saved workspace layouts and other presets
//   autosave/  recovery/  log/
//
// Where it is: HIKARISUB_USER_DIR when set; beside the program in a portable
// copy (`user`); else $XDG_CONFIG_HOME/hikarisub (~/.config/hikarisub) on
// Linux and %APPDATA%\HikariSub on Windows. Disposable files (index files,
// the audio cache) go to the cache folder instead: $XDG_CACHE_HOME/hikarisub
// (~/.cache/hikarisub), %LOCALAPPDATA%\HikariSub\cache.

#include <QString>
#include <QStringList>

namespace hikari::app {

class UserFolder {
public:
    // What locate() reads; current() fills it from this process.
    struct Environment {
        QString userDirOverride; // HIKARISUB_USER_DIR
        QString xdgConfigHome;
        QString xdgDataHome;
        QString xdgCacheHome;
        QString home;
        QString appData;      // %APPDATA%
        QString localAppData; // %LOCALAPPDATA%
        QString programFolder;
        bool portable = false;
        bool windows = false;
        static Environment current(bool portable);
    };

    UserFolder() = default;
    UserFolder(QString root, QString cache) : m_root(std::move(root)), m_cache(std::move(cache)) {}
    static UserFolder locate(const Environment &env);

    QString root() const { return m_root; }
    QString cache() const { return m_cache; }
    QString settingsFile() const { return m_root + QStringLiteral("/hikari.ini"); }
    QString dictionaries() const { return m_root + QStringLiteral("/dictionaries"); }
    QString catalogs() const { return m_root + QStringLiteral("/catalogs"); }
    QString presets() const { return m_root + QStringLiteral("/presets"); }
    QString recovery() const { return m_root + QStringLiteral("/recovery"); }
    QString audioCache() const { return m_cache + QStringLiteral("/AudioCache"); }
    QString indices() const { return m_cache + QStringLiteral("/Indices"); }

    // The bundled files' manifest: one "<sha256 hex> <path>" line per file,
    // the path relative to the program folder (Automation/automation/...,
    // Dictionary/*.dic and *.aff). cmake/HikariBundledManifest.cmake writes it.
    static QString manifestName() { return QStringLiteral("bundled-files.txt"); }
    // Where a bundled file goes in the user folder: Automation/automation/
    // Autoload and Include into automation/autoload and include, the
    // Dictionary folder's own .dic and .aff into dictionaries; empty for any
    // other path.
    static QString seededPath(const QString &bundledPath);

    struct SeedReport {
        QStringList copied;  // missing, copied in
        QStringList updated; // unchanged since the bundled version they came from, replaced
        QStringList kept;    // changed in the user folder, kept
        QStringList removed; // no longer bundled and unchanged, removed
        QStringList failed;  // could not be written or removed
    };
    // Copies the bundled files the manifest in `programFolder` lists into the
    // user folder: a missing file is copied in; a file still as the bundled
    // version it came from is replaced by the new one; a changed file is
    // kept; a file the bundle no longer has is removed while it is as
    // bundled. Which bundled version each file came from is recorded in
    // `.bundled-files` in the user folder. Paths in the report are the user
    // folder's. Nothing happens without a manifest.
    SeedReport seed(const QString &programFolder) const;

    struct MoveReport {
        QStringList from;     // the old folders something was moved from
        QStringList leftOver; // entries not moved (the new folder had one of that name)
        QStringList keptBack; // DependencyControl's own files, left in the old folder
        QString earlierSettings; // a portable copy's: the old folder with an earlier version's hikari.ini
    };
    // The Qt folders before UD1, `config` (hikari.ini's) and `data`
    // (Recovery, Automation): on Linux ~/.config/HikariSub/HikariSub and
    // ~/.local/share/HikariSub/HikariSub, on Windows
    // %LOCALAPPDATA%\HikariSub\HikariSub and %APPDATA%\HikariSub\HikariSub.
    struct OldFolders {
        QString config;
        QString data;
        static OldFolders of(const Environment &env);
    };
    // Moves the old folders' contents in once: the first time the user folder
    // has no hikari.ini, recorded in `.old-folders` in it. Caches go to the
    // cache folder, the rest into the user folder's layout; an entry whose
    // name the user folder already has stays, and so do DependencyControl's
    // own files (the old package's or its self-update), which would shadow
    // the bundled release. An old folder inside the user folder (Windows'
    // data folder) is renamed `old-HikariSub` first, so what stays is not
    // taken for the user's. Empty folders left behind are removed. Legacy's
    // folders are never read. A portable copy moves nothing; its report names
    // the old folder that holds an earlier version's hikari.ini.
    MoveReport moveIn(const OldFolders &old, bool portable = false) const;

private:
    QString m_root;
    QString m_cache;
};

} // namespace hikari::app
