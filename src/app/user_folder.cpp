#include "hikari/app/user_folder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace hikari::app {

namespace {

// XDG base directories: unset or relative means the default under $HOME.
QString xdgDir(const QString &value, const QString &home, const char *fallback)
{
    if (!value.isEmpty() && !QDir::isRelativePath(value))
        return QDir::cleanPath(value);
    return home + QLatin1Char('/') + QLatin1String(fallback);
}

QString windowsFolder(const QString &value)
{
    return QDir::cleanPath(QString(value).replace(QLatin1Char('\\'), QLatin1Char('/')));
}

QByteArray sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result().toHex();
}

bool copyOver(const QString &source, const QString &target)
{
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString part = target + QStringLiteral(".hikari-part");
    QFile::remove(part);
    if (!QFile::copy(source, part))
        return false;
    // QFile::copy keeps the source's permissions, which may be read-only.
    QFile::setPermissions(part, QFile::permissions(part) | QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        QFile::remove(part);
        return false;
    }
    return QFile::rename(part, target);
}

const QString kSeedRecord = QStringLiteral(".bundled-files");
const QString kMoveRecord = QStringLiteral(".old-folders");
const QString kLeftBehind = QStringLiteral("old-HikariSub");

struct SeedRecord {
    QByteArray manifest;
    QHash<QString, QByteArray> version;
};

SeedRecord readSeedRecord(const QString &file)
{
    SeedRecord record;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return record;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        const qsizetype space = line.indexOf(' ');
        if (space <= 0)
            continue;
        const QByteArray digest = line.left(space);
        const QString path = QString::fromUtf8(line.mid(space + 1));
        if (path == QLatin1String("*"))
            record.manifest = digest;
        else
            record.version.insert(path, digest);
    }
    return record;
}

bool writeSeedRecord(const QString &file, const SeedRecord &record)
{
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write(record.manifest + " *\n");
    QStringList paths = record.version.keys();
    paths.sort();
    for (const QString &path : paths)
        out.write(record.version.value(path) + ' ' + path.toUtf8() + '\n');
    return out.commit();
}

void moveEntry(const QString &source, const QString &target, QStringList &leftOver, const QStringList &stay,
               QStringList &keptBack)
{
    if (stay.contains(source)) {
        keptBack << source;
        return;
    }
    const bool holdsStay =
        std::any_of(stay.begin(), stay.end(), [&](const QString &path) { return path.startsWith(source + QLatin1Char('/')); });
    const QFileInfo from(source);
    const QFileInfo to(target);
    if (!to.exists() && !to.isSymLink()) {
        QDir().mkpath(to.absolutePath());
        if (!holdsStay && QDir().rename(source, target))
            return;
        if (!from.isDir() || from.isSymLink()) {
            if (QFile::copy(source, target) && QFile::remove(source))
                return;
            leftOver << source;
            return;
        }
        QDir().mkpath(target);
    } else if (!from.isDir() || from.isSymLink() || !to.isDir() || to.isSymLink()) {
        leftOver << source;
        return;
    }
    const QDir dir(source);
    for (const QString &name : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        moveEntry(dir.filePath(name), target + QLatin1Char('/') + name, leftOver, stay, keptBack);
    QDir().rmdir(source);
}

void removeEmpty(const QString &folder)
{
    const QDir dir(folder);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks))
        removeEmpty(dir.filePath(name));
    QDir().rmdir(folder);
}

}

UserFolder::Environment UserFolder::Environment::current(bool portable)
{
    Environment env;
    env.userDirOverride = qEnvironmentVariable("HIKARISUB_USER_DIR");
    env.xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    env.xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    env.xdgCacheHome = qEnvironmentVariable("XDG_CACHE_HOME");
    env.home = QDir::homePath();
    env.appData = qEnvironmentVariable("APPDATA");
    env.localAppData = qEnvironmentVariable("LOCALAPPDATA");
    env.programFolder = QCoreApplication::applicationDirPath();
    env.portable = portable;
#ifdef _WIN32
    env.windows = true;
#endif
    return env;
}

UserFolder UserFolder::locate(const Environment &env)
{
    QString cache;
    if (env.windows)
        cache = windowsFolder(env.localAppData) + QStringLiteral("/HikariSub/cache");
    else
        cache = xdgDir(env.xdgCacheHome, env.home, ".cache") + QStringLiteral("/hikarisub");
    QString root;
    if (!env.userDirOverride.isEmpty())
        root = QDir(QDir::cleanPath(QDir::fromNativeSeparators(env.userDirOverride))).absolutePath();
    else if (env.portable)
        root = QDir::cleanPath(QDir::fromNativeSeparators(env.programFolder)) + QStringLiteral("/user");
    else if (env.windows)
        root = windowsFolder(env.appData) + QStringLiteral("/HikariSub");
    else
        root = xdgDir(env.xdgConfigHome, env.home, ".config") + QStringLiteral("/hikarisub");
    return UserFolder(root, cache);
}

QString UserFolder::seededPath(const QString &bundledPath)
{
    static const QString autoload = QStringLiteral("Automation/automation/Autoload/");
    static const QString include = QStringLiteral("Automation/automation/Include/");
    static const QString dictionary = QStringLiteral("Dictionary/");
    if (bundledPath.contains(QLatin1String("..")) || bundledPath.contains(QLatin1Char('\\')))
        return {};
    if (bundledPath.startsWith(autoload) && bundledPath.size() > autoload.size())
        return QStringLiteral("automation/autoload/") + bundledPath.mid(autoload.size());
    if (bundledPath.startsWith(include) && bundledPath.size() > include.size())
        return QStringLiteral("automation/include/") + bundledPath.mid(include.size());
    if (bundledPath.startsWith(dictionary)) {
        const QString name = bundledPath.mid(dictionary.size());
        if (!name.contains(QLatin1Char('/')) &&
            (name.endsWith(QLatin1String(".dic")) || name.endsWith(QLatin1String(".aff"))))
            return QStringLiteral("dictionaries/") + name;
    }
    return {};
}

UserFolder::SeedReport UserFolder::seed(const QString &programFolder) const
{
    SeedReport report;
    QFile manifest(programFolder + QLatin1Char('/') + manifestName());
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text))
        return report;
    const QByteArray listing = manifest.readAll();
    const QByteArray listingDigest = QCryptographicHash::hash(listing, QCryptographicHash::Sha256).toHex();
    const QString recordFile = m_root + QLatin1Char('/') + kSeedRecord;
    SeedRecord record = readSeedRecord(recordFile);
    const bool sameBundle = record.manifest == listingDigest;
    bool changed = !sameBundle;
    QSet<QString> listed;
    for (const QByteArray &raw : listing.split('\n')) {
        const QByteArray line = raw.trimmed();
        const qsizetype space = line.indexOf(' ');
        if (space <= 0)
            continue;
        const QByteArray bundled = line.left(space).toLower();
        const QString bundledPath = QString::fromUtf8(line.mid(space + 1));
        const QString user = seededPath(bundledPath);
        if (user.isEmpty())
            continue;
        listed.insert(user);
        const QString source = programFolder + QLatin1Char('/') + bundledPath;
        const QString target = m_root + QLatin1Char('/') + user;
        const auto copy = [&](QStringList &into) {
            if (copyOver(source, target)) {
                into << user;
                record.version.insert(user, bundled);
                changed = true;
            } else {
                report.failed << user;
            }
        };
        if (!QFileInfo::exists(target)) {
            copy(report.copied);
            continue;
        }
        if (sameBundle || record.version.value(user) == bundled)
            continue;
        const QByteArray have = sha256Of(target);
        if (have == bundled) {
            record.version.insert(user, bundled);
            changed = true;
        } else if (record.version.contains(user) && record.version.value(user) == have) {
            copy(report.updated);
        } else {
            report.kept << user;
        }
    }
    if (!sameBundle) {
        const QStringList recorded = record.version.keys();
        for (const QString &user : recorded) {
            if (listed.contains(user))
                continue;
            const QString target = m_root + QLatin1Char('/') + user;
            if (QFileInfo::exists(target) && sha256Of(target) == record.version.value(user)) {
                if (!QFile::remove(target)) {
                    report.failed << user;
                    continue;
                }
                report.removed << user;
            }
            record.version.remove(user);
            changed = true;
        }
    }
    if (report.failed.isEmpty())
        record.manifest = listingDigest;
    if (changed) {
        QDir().mkpath(m_root);
        writeSeedRecord(recordFile, record);
    }
    return report;
}

UserFolder::OldFolders UserFolder::OldFolders::of(const Environment &env)
{
    OldFolders old;
    if (env.windows) {
        if (!env.localAppData.isEmpty())
            old.config = windowsFolder(env.localAppData) + QStringLiteral("/HikariSub/HikariSub");
        if (!env.appData.isEmpty())
            old.data = windowsFolder(env.appData) + QStringLiteral("/HikariSub/HikariSub");
    } else {
        old.config = xdgDir(env.xdgConfigHome, env.home, ".config") + QStringLiteral("/HikariSub/HikariSub");
        old.data = xdgDir(env.xdgDataHome, env.home, ".local/share") + QStringLiteral("/HikariSub/HikariSub");
    }
    return old;
}

UserFolder::MoveReport UserFolder::moveIn(const OldFolders &oldFolders, bool portable) const
{
    MoveReport report;
    const QString record = m_root + QLatin1Char('/') + kMoveRecord;
    if (QFileInfo::exists(record))
        return report;
    const auto recordDone = [&] {
        if (!report.from.isEmpty() || !report.earlierSettings.isEmpty())
            QDir().mkpath(m_root);
        if (QFile file(record); QFileInfo(m_root).isDir() && file.open(QIODevice::WriteOnly))
            file.close();
        return report;
    };
    if (QFileInfo::exists(settingsFile()) || QFileInfo::exists(m_root + QStringLiteral("/hikari.ini")) ||
        QFileInfo::exists(m_root + QStringLiteral("/hikari.ini.old")))
        return recordDone();
    if (portable) {
        if (!oldFolders.config.isEmpty() && QFileInfo::exists(oldFolders.config + QStringLiteral("/hikari.ini")))
            report.earlierSettings = QDir::toNativeSeparators(oldFolders.config);
        return recordDone();
    }
    OldFolders old = oldFolders;
    if (old.data.startsWith(m_root + QLatin1Char('/')) && QFileInfo(old.data).isDir()) {
        const QString renamed = m_root + QLatin1Char('/') + kLeftBehind;
        if (!QFileInfo::exists(renamed) && QDir().rename(old.data, renamed))
            old.data = renamed;
    }
    const auto into = [this](const QString &path) { return m_root + QLatin1Char('/') + path; };
    const auto cached = [this](const QString &path) { return m_cache + QLatin1Char('/') + path; };
    const auto configTarget = [&](const QString &name) {
        if (name == QLatin1String("Dictionary"))
            return into(QStringLiteral("dictionaries"));
        if (name == QLatin1String("Catalog"))
            return into(QStringLiteral("catalogs"));
        if (name.startsWith(QLatin1String("FontCatalogs")) && name.endsWith(QLatin1String(".txt")))
            return into(QStringLiteral("catalogs/") + name);
        if (name == QLatin1String("layouts.json"))
            return into(QStringLiteral("presets/") + name);
        if (name == QLatin1String("AudioCache") || name == QLatin1String("Indices"))
            return cached(name);
        return into(name);
    };
    QStringList stay;
    const auto dataTargets = [&](const QString &folder) {
        QList<std::pair<QString, QString>> out;
        const QDir dir(folder);
        const auto entries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
        for (const QString &name : dir.entryList(entries)) {
            const QString path = dir.filePath(name);
            if (name == QLatin1String("Recovery")) {
                out.append({path, into(QStringLiteral("recovery"))});
            } else if (name.startsWith(QLatin1String("HandedOn-"))) {
                out.append({path, cached(name)});
            } else if (name == QLatin1String("Automation") && QFileInfo(path).isDir()) {
                const QDir automation(path);
                for (const QString &sub : automation.entryList(entries)) {
                    if (sub != QLatin1String("automation")) {
                        out.append({automation.filePath(sub), into(sub)});
                        continue;
                    }
                    const QDir scripts(automation.filePath(sub));
                    QStringList parts = scripts.entryList(entries);
                    // MUST KILL: rank order mirrors the old script search, so the copy it used wins.
                    const auto rank = [](const QString &part) {
                        return part == QLatin1String("autoload") ? 0 : part == QLatin1String("Autoload") ? 1
                               : part == QLatin1String("Include")  ? 2 : part == QLatin1String("include") ? 3 : 4;
                    };
                    std::stable_sort(parts.begin(), parts.end(),
                                     [&](const QString &a, const QString &b) { return rank(a) < rank(b); });
                    for (const QString &part : std::as_const(parts)) {
                        const QString lower = part == QLatin1String("Autoload")  ? QStringLiteral("autoload")
                                              : part == QLatin1String("Include") ? QStringLiteral("include")
                                                                                 : part;
                        const QString partPath = scripts.filePath(part);
                        if (lower == QLatin1String("include"))
                            stay << partPath + QStringLiteral("/l0/DependencyControl.moon")
                                 << partPath + QStringLiteral("/l0/DependencyControl");
                        else if (lower == QLatin1String("autoload"))
                            stay << partPath + QStringLiteral("/l0.DependencyControl.Toolbox.moon");
                        out.append({partPath, into(QStringLiteral("automation/") + lower)});
                    }
                }
            } else {
                out.append({path, into(name)});
            }
        }
        return out;
    };
    for (const QString &folder : {old.config, old.data}) {
        if (folder.isEmpty() || !QFileInfo(folder).isDir())
            continue;
        QList<std::pair<QString, QString>> moves;
        if (folder == old.config) {
            const QDir dir(folder);
            for (const QString &name :
                 dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
                moves.append({dir.filePath(name), configTarget(name)});
        } else {
            moves = dataTargets(folder);
        }
        if (!moves.isEmpty())
            report.from << QDir::toNativeSeparators(folder == old.data ? oldFolders.data : folder);
        for (const auto &[source, target] : moves)
            moveEntry(source, target, report.leftOver, stay, report.keptBack);
        removeEmpty(folder);
        if (const QString parent = QFileInfo(folder).absolutePath(); parent != m_root)
            QDir().rmdir(parent);
    }
    return recordDone();
}

}
