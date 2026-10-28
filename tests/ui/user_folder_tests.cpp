// UD1 (#362): the user folder (hikari/app/user_folder.h): where it is, the
// bundled files seeded in by their manifest, and the one-time move from the
// Qt folders before it.

#include "hikari/app/user_folder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::UserFolder;

namespace {

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A program folder with the bundled files and their manifest, as
// cmake/HikariBundledManifest.cmake writes it.
void bundle(const QString &program, const QList<std::pair<QString, QByteArray>> &files)
{
    QByteArray manifest;
    for (const auto &[path, bytes] : files) {
        QVERIFY(write(program + QLatin1Char('/') + path, bytes));
        manifest += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() + ' ' + path.toUtf8() + '\n';
    }
    QVERIFY(write(program + QStringLiteral("/bundled-files.txt"), manifest));
}

QStringList entries(const QString &folder)
{
    QStringList out;
    QDirIterator it(folder, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(folder).relativeFilePath(it.next());
    out.sort();
    return out;
}

// Whether names in `folder` ignore case (Windows, macOS): there
// config/HikariSub is the user folder config/hikarisub, and Autoload and
// autoload are one folder.
bool namesIgnoreCase(const QString &folder)
{
    const QString probe = folder + QStringLiteral("/case-probe");
    const bool blind = write(probe, {}) && QFileInfo::exists(folder + QStringLiteral("/CASE-PROBE"));
    QFile::remove(probe);
    return blind;
}

} // namespace

class UserFolderTests : public QObject {
    Q_OBJECT

private slots:
    void linuxFollowsXdgAndTheOverride()
    {
        UserFolder::Environment env;
        env.home = QStringLiteral("/home/a");
        env.programFolder = QStringLiteral("/opt/hikarisub");
        UserFolder f = UserFolder::locate(env);
        QCOMPARE(f.root(), QStringLiteral("/home/a/.config/hikarisub"));
        QCOMPARE(f.cache(), QStringLiteral("/home/a/.cache/hikarisub"));
        QCOMPARE(f.settingsFile(), QStringLiteral("/home/a/.config/hikarisub/hikari.ini"));
        QCOMPARE(f.dictionaries(), QStringLiteral("/home/a/.config/hikarisub/dictionaries"));
        QCOMPARE(f.catalogs(), QStringLiteral("/home/a/.config/hikarisub/catalogs"));
        QCOMPARE(f.presets(), QStringLiteral("/home/a/.config/hikarisub/presets"));
        QCOMPARE(f.recovery(), QStringLiteral("/home/a/.config/hikarisub/recovery"));
        QCOMPARE(f.audioCache(), QStringLiteral("/home/a/.cache/hikarisub/AudioCache"));
        QCOMPARE(f.indices(), QStringLiteral("/home/a/.cache/hikarisub/Indices"));

        env.xdgConfigHome = QStringLiteral("/srv/conf/");
        env.xdgCacheHome = QStringLiteral("/srv/cache");
        f = UserFolder::locate(env);
        QCOMPARE(f.root(), QStringLiteral("/srv/conf/hikarisub"));
        QCOMPARE(f.cache(), QStringLiteral("/srv/cache/hikarisub"));

        // A relative XDG value is ignored, as the specification says.
        env.xdgConfigHome = QStringLiteral("conf");
        QCOMPARE(UserFolder::locate(env).root(), QStringLiteral("/home/a/.config/hikarisub"));

        env.portable = true;
        QCOMPARE(UserFolder::locate(env).root(), QStringLiteral("/opt/hikarisub/user"));

        env.userDirOverride = QStringLiteral("/data/mine/");
        f = UserFolder::locate(env);
        QCOMPARE(f.root(), QStringLiteral("/data/mine"));
        QCOMPARE(f.cache(), QStringLiteral("/srv/cache/hikarisub"));
    }

    void windowsUsesAppDataAndLocalAppData()
    {
        UserFolder::Environment env;
        env.windows = true;
        env.home = QStringLiteral("C:/Users/a");
        env.appData = QStringLiteral("C:\\Users\\a\\AppData\\Roaming");
        env.localAppData = QStringLiteral("C:\\Users\\a\\AppData\\Local");
        env.programFolder = QStringLiteral("C:/Program Files/HikariSub");
        UserFolder f = UserFolder::locate(env);
        QCOMPARE(f.root(), QStringLiteral("C:/Users/a/AppData/Roaming/HikariSub"));
        QCOMPARE(f.cache(), QStringLiteral("C:/Users/a/AppData/Local/HikariSub/cache"));
        env.portable = true;
        QCOMPARE(UserFolder::locate(env).root(), QStringLiteral("C:/Program Files/HikariSub/user"));

        const auto old = UserFolder::OldFolders::of(env);
        QCOMPARE(old.config, QStringLiteral("C:/Users/a/AppData/Local/HikariSub/HikariSub"));
        QCOMPARE(old.data, QStringLiteral("C:/Users/a/AppData/Roaming/HikariSub/HikariSub"));
    }

    void theOldFoldersAreQtsAppFolders()
    {
        UserFolder::Environment env;
        env.home = QStringLiteral("/home/a");
        auto old = UserFolder::OldFolders::of(env);
        QCOMPARE(old.config, QStringLiteral("/home/a/.config/HikariSub/HikariSub"));
        QCOMPARE(old.data, QStringLiteral("/home/a/.local/share/HikariSub/HikariSub"));
        env.xdgConfigHome = QStringLiteral("/x/config");
        env.xdgDataHome = QStringLiteral("/x/data");
        old = UserFolder::OldFolders::of(env);
        QCOMPARE(old.config, QStringLiteral("/x/config/HikariSub/HikariSub"));
        QCOMPARE(old.data, QStringLiteral("/x/data/HikariSub/HikariSub"));
    }

    void bundledPathsMapIntoTheLayout()
    {
        QCOMPARE(UserFolder::seededPath(QStringLiteral("Automation/automation/Autoload/kara-templater.lua")),
                 QStringLiteral("automation/autoload/kara-templater.lua"));
        QCOMPARE(UserFolder::seededPath(QStringLiteral("Automation/automation/Include/l0/DependencyControl.moon")),
                 QStringLiteral("automation/include/l0/DependencyControl.moon"));
        QCOMPARE(UserFolder::seededPath(QStringLiteral("Dictionary/en_US.dic")), QStringLiteral("dictionaries/en_US.dic"));
        QCOMPARE(UserFolder::seededPath(QStringLiteral("Dictionary/en_US.aff")), QStringLiteral("dictionaries/en_US.aff"));
        // Not seeded: the furigana dictionary, other folders, escapes.
        QVERIFY(UserFolder::seededPath(QStringLiteral("Dictionary/unidic-lite/sys.dic")).isEmpty());
        QVERIFY(UserFolder::seededPath(QStringLiteral("Dictionary/README")).isEmpty());
        QVERIFY(UserFolder::seededPath(QStringLiteral("Themes/Dark.txt")).isEmpty());
        QVERIFY(UserFolder::seededPath(QStringLiteral("Automation/automation/Autoload/")).isEmpty());
        QVERIFY(UserFolder::seededPath(QStringLiteral("Automation/automation/Include/../../../evil.lua")).isEmpty());
    }

    // A missing file is copied in; a file still as the bundled version it
    // came from takes the new one; a changed file is kept, and said so.
    void seedingCopiesUpdatesAndKeeps()
    {
        QTemporaryDir program, user;
        QVERIFY(program.isValid() && user.isValid());
        const UserFolder folder(user.path(), user.filePath(QStringLiteral("cache")));
        const QString a = QStringLiteral("Automation/automation/Autoload/a.lua");
        const QString b = QStringLiteral("Automation/automation/Include/l0/b.moon");
        const QString dic = QStringLiteral("Dictionary/en_US.dic");
        bundle(program.path(), {{a, "a 1"}, {b, "b 1"}, {dic, "dic 1"}});

        UserFolder::SeedReport r = folder.seed(program.path());
        QCOMPARE(r.copied, (QStringList{QStringLiteral("automation/autoload/a.lua"),
                                        QStringLiteral("automation/include/l0/b.moon"),
                                        QStringLiteral("dictionaries/en_US.dic")}));
        QVERIFY(r.updated.isEmpty() && r.kept.isEmpty() && r.failed.isEmpty());
        QCOMPARE(read(user.filePath(QStringLiteral("automation/autoload/a.lua"))), QByteArray("a 1"));
        QCOMPARE(read(user.filePath(QStringLiteral("dictionaries/en_US.dic"))), QByteArray("dic 1"));

        // The same bundle again: nothing to do, a deleted file comes back.
        QVERIFY(QFile::remove(user.filePath(QStringLiteral("automation/include/l0/b.moon"))));
        QVERIFY(write(user.filePath(QStringLiteral("automation/autoload/a.lua")), "mine"));
        r = folder.seed(program.path());
        QCOMPARE(r.copied, QStringList{QStringLiteral("automation/include/l0/b.moon")});
        QVERIFY(r.updated.isEmpty() && r.kept.isEmpty());
        QCOMPARE(read(user.filePath(QStringLiteral("automation/autoload/a.lua"))), QByteArray("mine"));

        // A new version: the untouched files update, the changed one stays.
        bundle(program.path(), {{a, "a 2"}, {b, "b 2"}, {dic, "dic 2"}});
        r = folder.seed(program.path());
        QVERIFY(r.copied.isEmpty());
        QCOMPARE(r.updated, (QStringList{QStringLiteral("automation/include/l0/b.moon"),
                                         QStringLiteral("dictionaries/en_US.dic")}));
        QCOMPARE(r.kept, QStringList{QStringLiteral("automation/autoload/a.lua")});
        QCOMPARE(read(user.filePath(QStringLiteral("automation/autoload/a.lua"))), QByteArray("mine"));
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/l0/b.moon"))), QByteArray("b 2"));
        QCOMPARE(read(user.filePath(QStringLiteral("dictionaries/en_US.dic"))), QByteArray("dic 2"));

        // The kept file is said once per bundle, not at every start.
        r = folder.seed(program.path());
        QVERIFY(r.copied.isEmpty() && r.updated.isEmpty() && r.kept.isEmpty());

        // A file the user made the same as the new version counts as it.
        QVERIFY(write(user.filePath(QStringLiteral("automation/autoload/a.lua")), "a 3"));
        bundle(program.path(), {{a, "a 3"}, {b, "b 2"}, {dic, "dic 2"}});
        r = folder.seed(program.path());
        QVERIFY(r.copied.isEmpty() && r.updated.isEmpty() && r.kept.isEmpty());
        bundle(program.path(), {{a, "a 4"}, {b, "b 2"}, {dic, "dic 2"}});
        r = folder.seed(program.path());
        QCOMPARE(r.updated, QStringList{QStringLiteral("automation/autoload/a.lua")});
        QCOMPARE(read(user.filePath(QStringLiteral("automation/autoload/a.lua"))), QByteArray("a 4"));
    }

    // A seeded file the bundle no longer has goes (DC1 dropped
    // DependencyControl's FileOps.moon); one the user changed stays theirs.
    void aFileTheBundleDroppedGoesUnlessChanged()
    {
        QTemporaryDir program, user;
        const UserFolder folder(user.path(), user.filePath(QStringLiteral("cache")));
        const QString a = QStringLiteral("Automation/automation/Include/l0/DependencyControl/FileOps.moon");
        const QString b = QStringLiteral("Automation/automation/Include/mine.lua");
        const QString c = QStringLiteral("Automation/automation/Include/kept.lua");
        bundle(program.path(), {{a, "a"}, {b, "b"}, {c, "c"}});
        QCOMPARE(folder.seed(program.path()).copied.size(), 3);
        QVERIFY(write(user.filePath(QStringLiteral("automation/include/mine.lua")), "changed"));
        QVERIFY(QFile::remove(program.filePath(a)) && QFile::remove(program.filePath(b)));
        bundle(program.path(), {{c, "c"}});
        UserFolder::SeedReport r = folder.seed(program.path());
        QCOMPARE(r.removed, QStringList{QStringLiteral("automation/include/l0/DependencyControl/FileOps.moon")});
        QVERIFY(r.failed.isEmpty() && r.kept.isEmpty());
        QVERIFY(!QFileInfo::exists(user.filePath(QStringLiteral("automation/include/l0/DependencyControl/FileOps.moon"))));
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/mine.lua"))), QByteArray("changed"));
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/kept.lua"))), QByteArray("c"));
        // Forgotten: a bundle that brings it back finds the user's file.
        bundle(program.path(), {{b, "b 2"}, {c, "c"}});
        r = folder.seed(program.path());
        QCOMPARE(r.kept, QStringList{QStringLiteral("automation/include/mine.lua")});
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/mine.lua"))), QByteArray("changed"));
    }

    // A file that could not be replaced is tried again at the next start,
    // though the bundle is the same.
    void aFailedUpdateIsRetried()
    {
#ifdef Q_OS_WIN
        QSKIP("a read-only folder does not stop writes on Windows");
#endif
        QTemporaryDir program, user;
        const UserFolder folder(user.path(), user.filePath(QStringLiteral("cache")));
        const QString a = QStringLiteral("Automation/automation/Include/BM/BadMutex/a.so");
        bundle(program.path(), {{a, "a 1"}});
        QCOMPARE(folder.seed(program.path()).copied.size(), 1);
        bundle(program.path(), {{a, "a 2"}});
        const QString locked = user.filePath(QStringLiteral("automation/include/BM/BadMutex"));
        const auto permissions = QFile::permissions(locked);
        QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        UserFolder::SeedReport r = folder.seed(program.path());
        QVERIFY(QFile::setPermissions(locked, permissions));
        QCOMPARE(r.failed, QStringList{QStringLiteral("automation/include/BM/BadMutex/a.so")});
        r = folder.seed(program.path());
        QCOMPARE(r.updated, QStringList{QStringLiteral("automation/include/BM/BadMutex/a.so")});
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/BM/BadMutex/a.so"))), QByteArray("a 2"));
    }

    // A file that was there before seeding (a script of the user's with a
    // bundled one's name, one DependencyControl updated) is the user's.
    void aFileTheSeedingDidNotBringIsKept()
    {
        QTemporaryDir program, user;
        const UserFolder folder(user.path(), user.filePath(QStringLiteral("cache")));
        QVERIFY(write(user.filePath(QStringLiteral("automation/include/l0/DependencyControl.moon")), "updated by DepCtrl"));
        bundle(program.path(), {{QStringLiteral("Automation/automation/Include/l0/DependencyControl.moon"), "bundled"}});
        const UserFolder::SeedReport r = folder.seed(program.path());
        QCOMPARE(r.kept, QStringList{QStringLiteral("automation/include/l0/DependencyControl.moon")});
        QCOMPARE(read(user.filePath(QStringLiteral("automation/include/l0/DependencyControl.moon"))),
                 QByteArray("updated by DepCtrl"));
    }

    void withoutAManifestNothingIsSeeded()
    {
        QTemporaryDir program, user;
        QVERIFY(write(program.filePath(QStringLiteral("Automation/automation/Autoload/a.lua")), "a"));
        const UserFolder folder(user.filePath(QStringLiteral("u")), user.filePath(QStringLiteral("cache")));
        const UserFolder::SeedReport r = folder.seed(program.path());
        QVERIFY(r.copied.isEmpty() && r.failed.isEmpty());
        QVERIFY(!QFileInfo::exists(folder.root()));
    }

    // The old folders' contents go into the layout; caches to the cache
    // folder; legacy's folder beside them is never touched.
    void aPopulatedOldFolderMovesIn()
    {
        QTemporaryDir home;
        const QString config = home.filePath(QStringLiteral("config/HikariSub/HikariSub"));
        const QString data = home.filePath(QStringLiteral("data/HikariSub/HikariSub"));
        const QString legacy = home.filePath(QStringLiteral("data/HikariSub/Legacy/Config/Config.txt"));
        for (const QString &path :
             {config + QStringLiteral("/hikari.ini"), config + QStringLiteral("/layout.json"),
              config + QStringLiteral("/layouts.json"), config + QStringLiteral("/Rules.txt"),
              config + QStringLiteral("/Dictionary/UserDic.udic"), config + QStringLiteral("/Catalog/Default.sty"),
              config + QStringLiteral("/FontCatalogs.txt"), config + QStringLiteral("/FontCatalogsAutosave1.txt"),
              config + QStringLiteral("/AudioCache/x.w64"), config + QStringLiteral("/Indices/y.ffindex"),
              config + QStringLiteral("/Import/manifest.json"), data + QStringLiteral("/Recovery/sessions/s.json"),
              data + QStringLiteral("/HandedOn-0123.txt"), data + QStringLiteral("/Automation/automation/Autoload/mine.lua"),
              data + QStringLiteral("/Automation/automation/Include/mod.lua"),
              data + QStringLiteral("/Automation/automation/autoload/dep.moon"),
              data + QStringLiteral("/Automation/config/l0.DependencyControl.json"),
              data + QStringLiteral("/Automation/log/x.log"), data + QStringLiteral("/Automation/autosave/a.ass"), legacy})
            QVERIFY(write(path, path.toUtf8()));
        const UserFolder folder(home.filePath(QStringLiteral("config/hikarisub")), home.filePath(QStringLiteral("cache/hikarisub")));
        const UserFolder::MoveReport r = folder.moveIn({config, data});
        QCOMPARE(r.from, (QStringList{QDir::toNativeSeparators(config), QDir::toNativeSeparators(data)}));
        QVERIFY2(r.leftOver.isEmpty(), qPrintable(r.leftOver.join(QLatin1Char(','))));
        QCOMPARE(entries(folder.root()),
                 (QStringList{".old-folders", "Import", "Import/manifest.json", "Rules.txt", "automation", "automation/autoload",
                              "automation/autoload/dep.moon", "automation/autoload/mine.lua", "automation/include",
                              "automation/include/mod.lua", "autosave", "autosave/a.ass", "catalogs", "catalogs/Default.sty",
                              "catalogs/FontCatalogs.txt", "catalogs/FontCatalogsAutosave1.txt", "config",
                              "config/l0.DependencyControl.json", "dictionaries", "dictionaries/UserDic.udic",
                              "hikari.ini", "layout.json", "log", "log/x.log", "presets", "presets/layouts.json",
                              "recovery", "recovery/sessions", "recovery/sessions/s.json"}));
        QCOMPARE(entries(folder.cache()), (QStringList{"AudioCache", "AudioCache/x.w64", "HandedOn-0123.txt", "Indices",
                                                       "Indices/y.ffindex"}));
        QCOMPARE(read(folder.settingsFile()), (config + QStringLiteral("/hikari.ini")).toUtf8());
        // The old folders are gone, the organisation folder only when empty
        // (where names ignore case it is the user folder, as on Windows,
        // %APPDATA%\HikariSub around the old HikariSub\HikariSub).
        QVERIFY(!QFileInfo::exists(config));
        QCOMPARE(QFileInfo::exists(home.filePath(QStringLiteral("config/HikariSub"))), namesIgnoreCase(home.path()));
        QVERIFY(!QFileInfo::exists(data));
        QCOMPARE(read(legacy), legacy.toUtf8());

        // Once: with hikari.ini in the user folder, a later old folder stays,
        // and so it does after hikari.ini is deleted to reset the settings
        // (#397).
        QVERIFY(write(config + QStringLiteral("/hikari.ini"), "older"));
        QVERIFY(folder.moveIn({config, data}).from.isEmpty());
        QCOMPARE(read(config + QStringLiteral("/hikari.ini")), QByteArray("older"));
        QVERIFY(QFile::remove(folder.settingsFile()));
        QVERIFY(folder.moveIn({config, data}).from.isEmpty());
        QCOMPARE(read(config + QStringLiteral("/hikari.ini")), QByteArray("older"));
        QVERIFY(!QFileInfo::exists(folder.settingsFile()));
    }

    // #397: a user folder a start before the record moved into (it has
    // hikari.ini) is recorded too, so deleting hikari.ini moves nothing in.
    void aUserFolderWithSettingsIsRecordedAsMovedInto()
    {
        QTemporaryDir home;
        const QString config = home.filePath(QStringLiteral("old-config"));
        const UserFolder folder(home.filePath(QStringLiteral("user")), home.filePath(QStringLiteral("cache")));
        QVERIFY(write(folder.settingsFile(), "mine"));
        QVERIFY(write(config + QStringLiteral("/hikari.ini"), "older"));
        QVERIFY(folder.moveIn({config, {}}).from.isEmpty());
        QVERIFY(QFile::remove(folder.settingsFile()));
        QVERIFY(folder.moveIn({config, {}}).from.isEmpty());
        QCOMPARE(read(config + QStringLiteral("/hikari.ini")), QByteArray("older"));
    }

    // #397: on Windows the old data folder, %APPDATA%\HikariSub\HikariSub,
    // is inside the user folder; what stays of it (here the old
    // DependencyControl) is in old-HikariSub, not in a HikariSub folder that
    // looks like the user's.
    void windowsLeftOversAreNamedOld()
    {
        QTemporaryDir home;
        UserFolder::Environment env;
        env.windows = true;
        env.appData = QDir::toNativeSeparators(home.filePath(QStringLiteral("Roaming")));
        env.localAppData = QDir::toNativeSeparators(home.filePath(QStringLiteral("Local")));
        const UserFolder folder = UserFolder::locate(env);
        const UserFolder::OldFolders old = UserFolder::OldFolders::of(env);
        QCOMPARE(old.data, folder.root() + QStringLiteral("/HikariSub"));
        const QString scripts = old.data + QStringLiteral("/Automation/automation/");
        QVERIFY(write(old.config + QStringLiteral("/hikari.ini"), "settings"));
        QVERIFY(write(old.data + QStringLiteral("/Recovery/sessions/s.json"), "session"));
        QVERIFY(write(scripts + QStringLiteral("include/l0/DependencyControl.moon"), "0.8.1"));
        const UserFolder::MoveReport r = folder.moveIn(old);
        QCOMPARE(r.from, (QStringList{QDir::toNativeSeparators(old.config), QDir::toNativeSeparators(old.data)}));
        const QString renamed = folder.root() + QStringLiteral("/old-HikariSub");
        QCOMPARE(r.keptBack, QStringList{renamed + QStringLiteral("/Automation/automation/include/l0/DependencyControl.moon")});
        QCOMPARE(read(r.keptBack.first()), QByteArray("0.8.1"));
        QVERIFY(!QFileInfo::exists(old.data));
        QCOMPARE(read(folder.recovery() + QStringLiteral("/sessions/s.json")), QByteArray("session"));
        QCOMPARE(read(folder.settingsFile()), QByteArray("settings"));
        // Moved entirely, nothing of it stays.
        QTemporaryDir other;
        env.appData = QDir::toNativeSeparators(other.filePath(QStringLiteral("Roaming")));
        const UserFolder clean = UserFolder::locate(env);
        QVERIFY(write(clean.root() + QStringLiteral("/HikariSub/Recovery/s.json"), "session"));
        QVERIFY(clean.moveIn(UserFolder::OldFolders::of(env)).keptBack.isEmpty());
        QCOMPARE(entries(clean.root()), (QStringList{".old-folders", "recovery", "recovery/s.json"}));
    }

    // #397: a portable copy moves nothing out of the system folders; it
    // names the old folder with an earlier version's settings, once.
    void aPortableCopyNamesTheEarlierSettings()
    {
        QTemporaryDir home;
        const QString config = home.filePath(QStringLiteral("old-config"));
        const QString data = home.filePath(QStringLiteral("old-data"));
        QVERIFY(write(config + QStringLiteral("/hikari.ini"), "settings"));
        QVERIFY(write(data + QStringLiteral("/Recovery/s.json"), "session"));
        const UserFolder folder(home.filePath(QStringLiteral("program/user")), home.filePath(QStringLiteral("cache")));
        UserFolder::MoveReport r = folder.moveIn({config, data}, true);
        QVERIFY(r.from.isEmpty() && r.leftOver.isEmpty());
        QCOMPARE(r.earlierSettings, QDir::toNativeSeparators(config));
        QCOMPARE(read(config + QStringLiteral("/hikari.ini")), QByteArray("settings"));
        QCOMPARE(read(data + QStringLiteral("/Recovery/s.json")), QByteArray("session"));
        QVERIFY(!QFileInfo::exists(folder.settingsFile()));
        r = folder.moveIn({config, data}, true);
        QVERIFY(r.earlierSettings.isEmpty());
        // Without an earlier hikari.ini there is nothing to name.
        const UserFolder fresh(home.filePath(QStringLiteral("fresh/user")), home.filePath(QStringLiteral("cache")));
        QVERIFY(fresh.moveIn({home.filePath(QStringLiteral("none")), data}, true).earlierSettings.isEmpty());
    }

    void emptyOrMissingOldFoldersMoveNothing()
    {
        QTemporaryDir home;
        const QString config = home.filePath(QStringLiteral("config/HikariSub/HikariSub"));
        const QString data = home.filePath(QStringLiteral("data/HikariSub/HikariSub"));
        const UserFolder folder(home.filePath(QStringLiteral("config/hikarisub")), home.filePath(QStringLiteral("cache/hikarisub")));
        UserFolder::MoveReport r = folder.moveIn({config, data});
        QVERIFY(r.from.isEmpty() && r.leftOver.isEmpty());
        QVERIFY(QDir().mkpath(config) && QDir().mkpath(data));
        r = folder.moveIn({config, data});
        QVERIFY(r.from.isEmpty() && r.leftOver.isEmpty());
        QVERIFY(!QFileInfo::exists(config) && !QFileInfo::exists(data));
        QVERIFY(!QFileInfo::exists(folder.root()));
    }

    // Autoload and autoload (Include and include) merge as the old search
    // read them: the lower-case autoload copy replaced an Autoload one, the
    // Include copy shadowed an include one; the other is left behind.
    void mergedScriptFoldersKeepTheCopyThatWasUsed()
    {
        QTemporaryDir home;
        if (namesIgnoreCase(home.path()))
            QSKIP("Autoload and autoload are one folder where names ignore case");
        const QString data = home.filePath(QStringLiteral("old-data"));
        const QString scripts = data + QStringLiteral("/Automation/automation/");
        QVERIFY(write(scripts + QStringLiteral("Autoload/s.lua"), "shadowed"));
        QVERIFY(write(scripts + QStringLiteral("autoload/s.lua"), "used"));
        QVERIFY(write(scripts + QStringLiteral("Include/m.lua"), "used"));
        QVERIFY(write(scripts + QStringLiteral("include/m.lua"), "shadowed"));
        const UserFolder folder(home.filePath(QStringLiteral("user")), home.filePath(QStringLiteral("cache")));
        const UserFolder::MoveReport r = folder.moveIn({{}, data});
        QCOMPARE(read(folder.root() + QStringLiteral("/automation/autoload/s.lua")), QByteArray("used"));
        QCOMPARE(read(folder.root() + QStringLiteral("/automation/include/m.lua")), QByteArray("used"));
        QCOMPARE(r.leftOver, (QStringList{scripts + QStringLiteral("Autoload/s.lua"), scripts + QStringLiteral("include/m.lua")}));
    }

    // The old package's DependencyControl (0.6.4, or the 0.8.1 it updated
    // itself to in ?user) stays in the old folder, so the bundled release is
    // the one loaded; the scripts and modules beside it move in.
    void theOldDependencyControlStaysBehind()
    {
        QTemporaryDir home;
        const QString data = home.filePath(QStringLiteral("old-data"));
        const QString scripts = data + QStringLiteral("/Automation/automation/");
        const QStringList old{scripts + QStringLiteral("include/l0/DependencyControl.moon"),
                              scripts + QStringLiteral("include/l0/DependencyControl/Updater.moon"),
                              scripts + QStringLiteral("autoload/l0.DependencyControl.Toolbox.moon")};
        for (const QString &path : old)
            QVERIFY(write(path, "0.8.1"));
        QVERIFY(write(scripts + QStringLiteral("include/l0/Functional.moon"), "module"));
        QVERIFY(write(scripts + QStringLiteral("autoload/mine.lua"), "script"));
        const UserFolder folder(home.filePath(QStringLiteral("user")), home.filePath(QStringLiteral("cache")));
        const UserFolder::MoveReport r = folder.moveIn({{}, data});
        QVERIFY2(r.leftOver.isEmpty(), qPrintable(r.leftOver.join(QLatin1Char(','))));
        QStringList keptBack = r.keptBack;
        keptBack.sort();
        QStringList expected{old[0], scripts + QStringLiteral("include/l0/DependencyControl"), old[2]};
        expected.sort();
        QCOMPARE(keptBack, expected);
        QCOMPARE(entries(folder.root() + QStringLiteral("/automation")),
                 (QStringList{"autoload", "autoload/mine.lua", "include", "include/l0", "include/l0/Functional.moon"}));
        for (const QString &path : old)
            QCOMPARE(read(path), QByteArray("0.8.1"));
    }

    // What the user folder already has stays; the old entry is left where it
    // was and named.
    void aNameTheUserFolderHasIsLeftBehind()
    {
        QTemporaryDir home;
        const QString config = home.filePath(QStringLiteral("old-config"));
        const QString data = home.filePath(QStringLiteral("old-data"));
        QVERIFY(write(config + QStringLiteral("/hikari.ini"), "settings"));
        QVERIFY(write(config + QStringLiteral("/Rules.txt"), "old rules"));
        QVERIFY(write(data + QStringLiteral("/Automation/automation/Autoload/a.lua"), "old a"));
        QVERIFY(write(data + QStringLiteral("/Automation/automation/Autoload/b.lua"), "old b"));
        const UserFolder folder(home.filePath(QStringLiteral("user")), home.filePath(QStringLiteral("cache")));
        QVERIFY(write(folder.root() + QStringLiteral("/Rules.txt"), "new rules"));
        QVERIFY(write(folder.root() + QStringLiteral("/automation/autoload/a.lua"), "new a"));
        const UserFolder::MoveReport r = folder.moveIn({config, data});
        QCOMPARE(r.leftOver, (QStringList{config + QStringLiteral("/Rules.txt"),
                                          data + QStringLiteral("/Automation/automation/Autoload/a.lua")}));
        QCOMPARE(read(folder.root() + QStringLiteral("/Rules.txt")), QByteArray("new rules"));
        QCOMPARE(read(folder.root() + QStringLiteral("/automation/autoload/a.lua")), QByteArray("new a"));
        QCOMPARE(read(folder.root() + QStringLiteral("/automation/autoload/b.lua")), QByteArray("old b"));
        QCOMPARE(read(folder.settingsFile()), QByteArray("settings"));
        QCOMPARE(read(config + QStringLiteral("/Rules.txt")), QByteArray("old rules"));
    }
};

QTEST_GUILESS_MAIN(UserFolderTests)
#include "user_folder_tests.moc"
