// O3: the importer's transaction over a settings folder (S44-import): the
// legacy roots listed, the snapshot with its hashes and the legacy files
// left as they were, staged generations activated through the manifest at
// the next start, an interrupted activation keeping the previous
// generation (none, or an earlier import live or waiting), a repeated
// import as a no-op, edits made after staging kept (also over the import's
// own changes), two imports waiting for one start,
// rollback to the previous complete generation with the edits it replaces,
// stale sources and destinations refused; and the window's controller. The
// readers and the plan are hikari_application_settings_import_tests.

#include "hikari/app/settings_import_controller.h"
#include "hikari/app/settings_import_store.h"

#include "settings_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;
using app::SettingsImportStore;
using hikari::ui::SettingsStore;
namespace si = application::settings_import;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString hashOf(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// Eleven records: more than SetRawOptions' ten (config.cpp:150).
QByteArray configText(const QByteArray &extra)
{
    QByteArray out = "[HikariSub v0.0.1-rc.1]\r\n";
    for (int i = 1; i <= 11; ++i)
        out += "EDITBOX_TAG_BUTTON_VALUE" + QByteArray::number(i) + "=\r\n";
    return out + extra;
}

// A legacy installation: Config/, Dictionary/, Themes/, written with CRLF.
// The brace table closes on every platform (O3-linux-crlf-blocks;
// settings_import_tests CrlfIsAcceptedOnEveryPlatform).
struct LegacyRoot {
    QTemporaryDir dir;
    LegacyRoot()
    {
        writeFile(path("Config/Config.txt"), configText("GRID_FONT=Arial\r\nGRID_FONT_SIZE=12\r\nPROGRAM_THEME=Mine\r\n"
                                                        "TOOLBAR_ALIGNMENT=1\r\nSUBS_RECENT_FILES={\r\n\tD:\\media\\a.ass\r\n}\r\n"));
        writeFile(path("Config/Hotkeys.txt"), "[Kainote v0.9.0.1500]\r\nGLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n"
                                              "GLOBAL_VIDEO_INDEXING G=Ctrl-Alt-I\r\nNOT_AN_ACTION G=Ctrl-K\r\n");
        writeFile(path("Config/Rules.txt"), "1\tteh\tthe\r\n");
        writeFile(path("Dictionary/UserDic.udic"), "\xEF\xBB\xBFs\xC5\x82owo\r\n");
        writeFile(path("Themes/Mine.txt"), "WINDOW_BACKGROUND=#000000\r\n");
    }
    QString path(const char *relative) const { return dir.filePath(QString::fromLatin1(relative)); }
    std::map<QString, QByteArray> bytes() const
    {
        std::map<QString, QByteArray> out;
        for (const char *p : {"Config/Config.txt", "Config/Hotkeys.txt", "Config/Rules.txt", "Dictionary/UserDic.udic",
                              "Themes/Mine.txt"})
            out[QString::fromLatin1(p)] = readFile(path(p));
        return out;
    }
};

// One session over the settings folder: its registry and the import store.
struct Session {
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<SettingsImportStore> store;
    explicit Session(const QString &folder)
    {
        settings = std::make_unique<SettingsStore>(folder + QStringLiteral("/hikari.ini"));
        store = std::make_unique<SettingsImportStore>(*settings, folder);
    }
    ~Session()
    {
        if (settings)
            settings->sync();
    }
};

struct Review {
    SettingsImportStore::Snapshot snapshot;
    si::Plan plan;
    QString revision;
};

Review review(SettingsImportStore &store, const QString &rootPath)
{
    Review r;
    const auto root = SettingsImportStore::inspectRoot(rootPath);
    if (!root)
        return r;
    r.snapshot = *store.snapshot(*root);
    const auto previous = store.activeReceipt();
    si::PlanOptions o;
    o.previous = previous ? &*previous : nullptr;
    for (const QString &t : r.snapshot.themeFiles)
        o.themeFiles.push_back(t.toStdString());
    r.revision = store.revision();
    r.plan = si::buildPlan(r.snapshot.sources, store.destination(), o);
    return r;
}

} // namespace

class SettingsImportTests : public QObject {
    Q_OBJECT

private slots:
    void rootsAreListedNeverCombined()
    {
        LegacyRoot a, b;
        QTemporaryDir empty;
        const auto roots = SettingsImportStore::discoverRoots(
            {empty.path(), a.dir.path(), b.dir.path(), a.dir.path() + QStringLiteral("/."), QString()});
        QCOMPARE(roots.size(), 2);
        QCOMPARE(roots[0].path, QDir::cleanPath(a.dir.path()));
        QCOMPARE(roots[1].path, QDir::cleanPath(b.dir.path()));
        QCOMPARE(roots[0].files, (QStringList{"Config/Config.txt", "Config/Hotkeys.txt", "Config/Rules.txt",
                                              "Dictionary/UserDic.udic"}));
        QCOMPARE(roots[0].themes, QStringList{"Themes/Mine.txt"});
        QVERIFY(!SettingsImportStore::inspectRoot(empty.path()));
    }

    void snapshotKeepsTheBytesAndTheirHashes()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        Session s(folder.path());
        const auto before = legacy.bytes();
        const auto root = SettingsImportStore::inspectRoot(legacy.dir.path());
        QVERIFY(root);
        const auto snap = s.store->snapshot(*root);
        QVERIFY(snap);
        QCOMPARE(snap->sources.size(), std::size_t(4));
        for (const auto &src : snap->sources) {
            const QString path = QString::fromStdString(src.path);
            const QByteArray copied = readFile(snap->dir + QLatin1Char('/') + path);
            QCOMPARE(copied, before.at(path));
            QCOMPARE(QString::fromStdString(src.sha256), hashOf(before.at(path)));
        }
        // The evidence: path, hash, encoding and header.
        const QJsonObject json = QJsonDocument::fromJson(readFile(snap->dir + QStringLiteral("/snapshot.json"))).object();
        const QJsonArray files = json.value(QStringLiteral("files")).toArray();
        QCOMPARE(files.size(), 4);
        // CRLF reads as LF on every platform (O3-linux-crlf-blocks).
        QCOMPARE(files[0].toObject().value(QStringLiteral("header")).toString(), QStringLiteral("[HikariSub v0.0.1-rc.1]"));
        QCOMPARE(files[3].toObject().value(QStringLiteral("encoding")).toString(), QStringLiteral("UTF-8"));
        QVERIFY(files[3].toObject().value(QStringLiteral("bom")).toBool());
        // The same bytes again: the same snapshot.
        QCOMPARE(s.store->snapshot(*root)->dir, snap->dir);
        QVERIFY(SettingsImportStore::sourcesUnchanged(*snap));
        QCOMPARE(legacy.bytes(), before);
    }

    void importTakesEffectAtTheNextStart()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        const auto before = legacy.bytes();
        {
            Session s(folder.path());
            const Review r = review(*s.store, legacy.dir.path());
            QCOMPARE(r.plan.row("setting:grid.font")->disposition, si::Disposition::Change);
            QCOMPARE(r.plan.row("setting:TOOLBAR_ALIGNMENT")->disposition, si::Disposition::Retired);
            QCOMPARE(r.plan.row("shortcut:GLOBAL_VIDEO_INDEXING:G")->disposition, si::Disposition::Retired);
            QCOMPARE(r.plan.row("record:Config/Hotkeys.txt:4")->disposition, si::Disposition::Unresolved);
            QCOMPARE(r.plan.row("file:Themes/Mine.txt")->disposition, si::Disposition::Excluded);
            QCOMPARE(SettingsImportStore::Result(s.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot,
                                                                   r.revision)),
                     SettingsImportStore::Result::Activated);
            QVERIFY(s.store->pending());
            // Nothing read yet changes in this session.
            QVERIFY(!s.settings->isSet(QStringLiteral("grid.font")));
            QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("Rules.txt"))));
            // A plan made now compares with the pending generation.
            QCOMPARE(std::get<std::string>(s.store->destination().values.at("grid.font")), std::string("Arial"));
        }
        Session next(folder.path());
        QVERIFY(next.store->recover());
        QVERIFY(!next.store->pending());
        QCOMPARE(next.settings->text("grid.font"), QStringLiteral("Arial"));
        QCOMPARE(next.settings->integer("grid.fontSize"), 12);
        QVERIFY(!next.settings->isSet(QStringLiteral("program.theme")));
        QVERIFY(!next.settings->isSet(QStringLiteral("toolbar.alignment")));
        // The foreign path is kept in the list, unresolved, not repaired.
        QCOMPARE(next.settings->list("recent.subtitles"), QStringList{QStringLiteral("D:\\media\\a.ass")});
        QVERIFY(next.settings->list(application::kHotkeysSetting.data()).contains(QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S")));
        QCOMPARE(readFile(folder.filePath(QStringLiteral("Rules.txt"))), QByteArray("1\tteh\tthe\r\n"));
        QCOMPARE(readFile(folder.filePath(QStringLiteral("Dictionary/UserDic.udic"))),
                 QByteArray("\xEF\xBB\xBFs\xC5\x82owo\r\n"));
        QVERIFY(!next.store->recover()); // once
        // The legacy files are never written.
        QCOMPARE(legacy.bytes(), before);
    }

    void repeatedImportIsANoOp()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        {
            Session s(folder.path());
            const Review r = review(*s.store, legacy.dir.path());
            QCOMPARE(s.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot, r.revision),
                     SettingsImportStore::Result::Activated);
        }
        Session next(folder.path());
        next.store->recover();
        const int active = next.store->activeGeneration();
        const QStringList generations =
            QDir(next.store->importDir() + QStringLiteral("/generations")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const Review again = review(*next.store, legacy.dir.path());
        QVERIFY(si::proposedRows(again.plan).empty());
        QCOMPARE(next.store->activate(again.plan, si::proposedRows(again.plan), again.snapshot, again.revision),
                 SettingsImportStore::Result::NoChange);
        QCOMPARE(next.store->activeGeneration(), active);
        QCOMPARE(QDir(next.store->importDir() + QStringLiteral("/generations")).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
                 generations);
        // The recents are not doubled.
        QCOMPARE(next.settings->list("recent.subtitles").size(), 1);
    }

    void interruptedActivationKeepsThePreviousGeneration_data()
    {
        QTest::addColumn<int>("step");
        QTest::newRow("backup") << int(SettingsImportStore::Step::Backup);
        QTest::newRow("files") << int(SettingsImportStore::Step::Files);
        QTest::newRow("profile") << int(SettingsImportStore::Step::Profile);
        QTest::newRow("complete") << int(SettingsImportStore::Step::Complete);
        QTest::newRow("manifest") << int(SettingsImportStore::Step::Manifest);
    }
    void interruptedActivationKeepsThePreviousGeneration()
    {
        QFETCH(int, step);
        LegacyRoot legacy;
        QTemporaryDir folder;
        {
            Session s(folder.path());
            s.settings->set("grid.font", QStringLiteral("Mine"));
            const Review r = review(*s.store, legacy.dir.path());
            auto chosen = si::proposedRows(r.plan);
            chosen.insert("setting:grid.font");
            s.store->failAt = [step](SettingsImportStore::Step at) { return int(at) == step; };
            QCOMPARE(s.store->activate(r.plan, chosen, r.snapshot, r.revision), SettingsImportStore::Result::StagingFailed);
            QCOMPARE(s.store->activeGeneration(), 0);
            QVERIFY(!s.store->pending());
            if (step != int(SettingsImportStore::Step::Backup) && step != int(SettingsImportStore::Step::Manifest))
                QVERIFY(!s.store->incompleteStaging().isEmpty()); // reported for cleanup
        }
        {
            Session next(folder.path());
            QVERIFY(!next.store->recover());
            QCOMPARE(next.settings->text("grid.font"), QStringLiteral("Mine"));
            QVERIFY(!next.settings->isSet(QStringLiteral("grid.fontSize")));
            QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("Rules.txt"))));
            // Retrying works and skips the broken staging.
            const Review r = review(*next.store, legacy.dir.path());
            QCOMPARE(next.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot, r.revision),
                     SettingsImportStore::Result::Activated);
        }
        Session last(folder.path());
        QVERIFY(last.store->recover());
        QCOMPARE(last.settings->text("grid.font"), QStringLiteral("Mine")); // kept: not chosen this time
        QCOMPARE(last.settings->integer("grid.fontSize"), 12);
    }

    void interruptedStartFinishesTheActivation()
    {
        // The switch happened; the start that applied it stopped before it
        // recorded so: the next start applies it again, to the same result.
        LegacyRoot legacy;
        QTemporaryDir folder;
        {
            Session s(folder.path());
            const Review r = review(*s.store, legacy.dir.path());
            s.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot, r.revision);
        }
        QString ini;
        {
            Session s(folder.path());
            QVERIFY(s.store->recover());
            s.settings->sync();
            ini = QString::fromUtf8(readFile(folder.filePath(QStringLiteral("hikari.ini"))));
            QVERIFY(QFile::remove(s.store->importDir() + QStringLiteral("/live")));
        }
        Session again(folder.path());
        QVERIFY(again.store->pending());
        QVERIFY(again.store->recover());
        again.settings->sync();
        QCOMPARE(QString::fromUtf8(readFile(folder.filePath(QStringLiteral("hikari.ini")))), ini);
        QCOMPARE(again.settings->text("grid.font"), QStringLiteral("Arial"));
    }

    // settings-import.md step 5: never overwrite later user edits
    // automatically. An edit made after the import was staged is kept at the
    // next start, also where the import changes the same setting.
    void editsAfterStagingAreKept()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        {
            Session s(folder.path());
            const Review r = review(*s.store, legacy.dir.path());
            QVERIFY(r.plan.row("setting:grid.font")->proposedImport);
            QCOMPARE(s.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot, r.revision),
                     SettingsImportStore::Result::Activated);
            QVERIFY(s.store->editsKeptOverImport().isEmpty());
            // Changed in the same session, after the import was staged.
            s.settings->set("grid.hideColumns", 6);
            s.settings->set("grid.font", QStringLiteral("Later"));
            writeFile(folder.filePath(QStringLiteral("Rules.txt")), "1\tmine\tours\r\n");
            QCOMPARE(s.store->editsKeptOverImport(), (QStringList{"grid.font", "Rules.txt"}));
            // A plan made now compares with what the next start will hold.
            QCOMPARE(std::get<std::string>(s.store->destination().values.at("grid.font")), std::string("Later"));
        }
        Session next(folder.path());
        QVERIFY(next.store->recover());
        QCOMPARE(next.settings->integer("grid.hideColumns"), 6); // not touched by the import: kept
        QCOMPARE(next.settings->text("grid.font"), QStringLiteral("Later")); // the import changes it: kept too
        QCOMPARE(readFile(folder.filePath(QStringLiteral("Rules.txt"))), QByteArray("1\tmine\tours\r\n"));
        QCOMPARE(next.settings->integer("grid.fontSize"), 12); // the import's other changes go in
        QVERIFY(next.store->editsKeptOverImport().isEmpty());
        // A rollback would replace them: they are not the import's.
        QCOMPARE(next.store->editsSinceActivation(), (QStringList{"grid.font", "grid.hideColumns", "Rules.txt"}));
    }

    // Two imports before a start: the second one's base is the live
    // profile, so the first one's changes go in with it.
    void importsWaitingTogetherBothTakeEffect()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        int first = 0;
        {
            Session s(folder.path());
            const Review r = review(*s.store, legacy.dir.path());
            auto chosen = si::proposedRows(r.plan);
            QVERIFY(chosen.erase("setting:grid.fontSize"));
            QCOMPARE(s.store->activate(r.plan, chosen, r.snapshot, r.revision), SettingsImportStore::Result::Activated);
            first = s.store->activeGeneration();
            const Review again = review(*s.store, legacy.dir.path());
            chosen = si::proposedRows(again.plan);
            chosen.insert("setting:grid.fontSize");
            QCOMPARE(s.store->activate(again.plan, chosen, again.snapshot, again.revision),
                     SettingsImportStore::Result::Activated);
            QCOMPARE(s.store->previousGeneration(), first);
        }
        {
            Session next(folder.path());
            QVERIFY(next.store->recover());
            QCOMPARE(next.settings->text("grid.font"), QStringLiteral("Arial"));
            QCOMPARE(next.settings->integer("grid.fontSize"), 12);
            QCOMPARE(readFile(folder.filePath(QStringLiteral("Rules.txt"))), QByteArray("1\tteh\tthe\r\n"));
            QVERIFY(next.settings->list(application::kHotkeysSetting.data())
                        .contains(QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S")));
            QCOMPARE(next.store->rollback(), SettingsImportStore::Result::Activated);
        }
        // Rolled back to the first import, whole.
        Session back(folder.path());
        QVERIFY(back.store->recover());
        QCOMPARE(back.store->activeGeneration(), first);
        QCOMPARE(back.settings->text("grid.font"), QStringLiteral("Arial"));
        QVERIFY(!back.settings->isSet(QStringLiteral("grid.fontSize")));
        QVERIFY(QFileInfo::exists(folder.filePath(QStringLiteral("Rules.txt"))));
    }

    // An activation interrupted while an earlier import is active, live or
    // waiting for the next start: that import stays active, with the same
    // previous generation, and the next start holds it.
    void interruptedActivationKeepsTheActiveImport_data()
    {
        QTest::addColumn<int>("step");
        QTest::addColumn<bool>("waiting");
        for (const bool waiting : {false, true}) {
            const char *when = waiting ? "waiting" : "live";
            QTest::addRow("backup, %s", when) << int(SettingsImportStore::Step::Backup) << waiting;
            QTest::addRow("files, %s", when) << int(SettingsImportStore::Step::Files) << waiting;
            QTest::addRow("profile, %s", when) << int(SettingsImportStore::Step::Profile) << waiting;
            QTest::addRow("complete, %s", when) << int(SettingsImportStore::Step::Complete) << waiting;
            QTest::addRow("manifest, %s", when) << int(SettingsImportStore::Step::Manifest) << waiting;
        }
    }
    void interruptedActivationKeepsTheActiveImport()
    {
        QFETCH(int, step);
        QFETCH(bool, waiting);
        LegacyRoot legacy;
        QTemporaryDir folder;
        int first = 0, previous = 0;
        const auto importAgain = [&](Session &s) {
            // An edit since the first import: the second one backs up the
            // current profile before staging (the Backup step).
            s.settings->set("grid.hideColumns", 4);
            const Review r = review(*s.store, legacy.dir.path());
            auto chosen = si::proposedRows(r.plan);
            chosen.insert("setting:grid.fontSize");
            s.store->failAt = [step](SettingsImportStore::Step at) { return int(at) == step; };
            QCOMPARE(s.store->activate(r.plan, chosen, r.snapshot, r.revision), SettingsImportStore::Result::StagingFailed);
            QCOMPARE(s.store->activeGeneration(), first);
            QCOMPARE(s.store->previousGeneration(), previous);
            QCOMPARE(s.store->pending(), waiting);
            if (step != int(SettingsImportStore::Step::Backup) && step != int(SettingsImportStore::Step::Manifest))
                QVERIFY(!s.store->incompleteStaging().isEmpty());
        };
        {
            Session s(folder.path());
            s.settings->set("grid.font", QStringLiteral("Mine"));
            const Review r = review(*s.store, legacy.dir.path());
            auto chosen = si::proposedRows(r.plan);
            QVERIFY(chosen.erase("setting:grid.fontSize"));
            QCOMPARE(s.store->activate(r.plan, chosen, r.snapshot, r.revision), SettingsImportStore::Result::Activated);
            first = s.store->activeGeneration();
            previous = s.store->previousGeneration();
            QVERIFY(first > 0 && previous > 0);
            if (waiting)
                importAgain(s);
        }
        if (!waiting) {
            Session s(folder.path());
            QVERIFY(s.store->recover());
            importAgain(s);
        }
        {
            Session next(folder.path());
            QCOMPARE(next.store->recover(), waiting);
            QCOMPARE(next.store->activeGeneration(), first);
            QCOMPARE(next.store->previousGeneration(), previous);
            // The first import, and the edit made since; not the second import.
            QCOMPARE(next.settings->text("grid.font"), QStringLiteral("Mine"));
            QCOMPARE(readFile(folder.filePath(QStringLiteral("Rules.txt"))), QByteArray("1\tteh\tthe\r\n"));
            QCOMPARE(next.settings->integer("grid.hideColumns"), 4);
            QVERIFY(!next.settings->isSet(QStringLiteral("grid.fontSize")));
            // Its rollback is still the one it had.
            QCOMPARE(next.store->rollback(), SettingsImportStore::Result::Activated);
        }
        Session back(folder.path());
        QVERIFY(back.store->recover());
        QCOMPARE(back.store->activeGeneration(), previous);
        QCOMPARE(back.settings->text("grid.font"), QStringLiteral("Mine"));
        QVERIFY(!back.settings->isSet(QStringLiteral("grid.hideColumns")));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("Rules.txt"))));
    }

    void rollbackRestoresThePreviousGeneration()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        const auto before = legacy.bytes();
        {
            Session s(folder.path());
            s.settings->set("grid.font", QStringLiteral("Mine"));
            s.settings->set("grid.hideColumns", 3);
            const Review r = review(*s.store, legacy.dir.path());
            auto chosen = si::proposedRows(r.plan);
            chosen.insert("setting:grid.font");
            QCOMPARE(s.store->activate(r.plan, chosen, r.snapshot, r.revision), SettingsImportStore::Result::Activated);
            QVERIFY(s.store->canRollBack());
        }
        {
            Session s(folder.path());
            s.store->recover();
            QCOMPARE(s.settings->text("grid.font"), QStringLiteral("Arial"));
            // Edited after the import: a rollback says it replaces them.
            s.settings->set("grid.fontSize", 20);
            writeFile(folder.filePath(QStringLiteral("Dictionary/UserDic.udic")), "inne\r\n");
            const QStringList edits = s.store->editsSinceActivation();
            QCOMPARE(edits, (QStringList{"grid.fontSize", "Dictionary/UserDic.udic"}));
            QCOMPARE(s.store->rollback(), SettingsImportStore::Result::Activated);
            QVERIFY(s.store->pending());
        }
        Session back(folder.path());
        QVERIFY(back.store->recover());
        QCOMPARE(back.settings->text("grid.font"), QStringLiteral("Mine"));
        QCOMPARE(back.settings->integer("grid.hideColumns"), 3);
        QVERIFY(!back.settings->isSet(QStringLiteral("grid.fontSize")));
        QVERIFY(!back.settings->isSet(QStringLiteral("recent.subtitles")));
        QVERIFY(!back.settings->isSet(application::kHotkeysSetting.data()));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("Rules.txt"))));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("Dictionary/UserDic.udic"))));
        QVERIFY(!back.store->canRollBack());
        QCOMPARE(back.store->rollback(), SettingsImportStore::Result::NothingToRollBack);
        QCOMPARE(legacy.bytes(), before);
        // Importing again after the rollback proposes the import again.
        const Review again = review(*back.store, legacy.dir.path());
        QVERIFY(again.plan.row("setting:grid.fontSize")->proposedImport);
    }

    void staleSourcesAndDestinationsAreRefused()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        Session s(folder.path());
        const Review r = review(*s.store, legacy.dir.path());
        s.settings->set("grid.hideColumns", 2);
        QCOMPARE(s.store->activate(r.plan, si::proposedRows(r.plan), r.snapshot, r.revision),
                 SettingsImportStore::Result::StaleDestination);
        const Review fresh = review(*s.store, legacy.dir.path());
        writeFile(legacy.path("Config/Hotkeys.txt"), "[Kainote v0.9.0.1500]\r\nGLOBAL_SAVE_SUBS G=Ctrl-Alt-Q\r\n");
        QCOMPARE(s.store->activate(fresh.plan, si::proposedRows(fresh.plan), fresh.snapshot, fresh.revision),
                 SettingsImportStore::Result::StaleSources);
        QCOMPARE(s.store->activeGeneration(), 0);
    }

    void controllerReviewsImportsAndRollsBack()
    {
        LegacyRoot legacy;
        QTemporaryDir folder;
        Session s(folder.path());
        app::SettingsImportController c(s.store.get());
        QVERIFY(c.available());
        QSignalSpy plan(&c, &app::SettingsImportController::planChanged);
        c.discover({legacy.dir.path()});
        QCOMPARE(c.roots().size(), 1);
        QVERIFY(!c.addRoot(folder.path() + QStringLiteral("/none")));
        QVERIFY(c.status().contains(QStringLiteral("holds no legacy settings")));
        QVERIFY(c.addRoot(QUrl::fromLocalFile(legacy.dir.path()).toString()));
        QCOMPARE(c.roots().size(), 1); // the same root once
        QVERIFY(c.choose(c.roots().first().toMap().value(QStringLiteral("path")).toString()));
        QVERIFY(plan.count() > 0);
        QVariantMap font;
        for (const auto &row : c.rows())
            if (row.toMap().value(QStringLiteral("id")) == QStringLiteral("setting:grid.font"))
                font = row.toMap();
        QCOMPARE(font.value(QStringLiteral("value")).toString(), QStringLiteral("Arial"));
        QCOMPARE(font.value(QStringLiteral("current")).toString(), QStringLiteral("Tahoma"));
        QCOMPARE(font.value(QStringLiteral("disposition")).toString(), QStringLiteral("change"));
        QVERIFY(c.chosenIds().contains(QStringLiteral("setting:grid.font")));
        // Choosing rows leaves the plan (the window's list) as it is.
        const int plans = plan.count();
        QSignalSpy chosenSpy(&c, &app::SettingsImportController::chosenChanged);
        const qsizetype proposed = c.chosenIds().size();
        c.setChosen(QStringLiteral("setting:grid.font"), false);
        QVERIFY(!c.chosen().contains("setting:grid.font"));
        QVERIFY(!c.chosenIds().contains(QStringLiteral("setting:grid.font")));
        QCOMPARE(chosenSpy.count(), 1);
        c.setChosen(QStringLiteral("setting:grid.font"), false); // no change, no signal
        QCOMPARE(chosenSpy.count(), 1);
        c.setChosen(QStringLiteral("setting:PROGRAM_THEME"), true); // excluded: not selectable
        QVERIFY(!c.chosen().contains("setting:PROGRAM_THEME"));
        QCOMPARE(chosenSpy.count(), 1);
        c.chooseAll(QStringLiteral("proposed"), true);
        QCOMPARE(c.chosenIds().size(), proposed);
        c.setChosen(QStringLiteral("setting:grid.font"), false);
        QCOMPARE(plan.count(), plans);
        // The theme setting and the theme file are the excluded rows.
        QStringList excluded;
        for (const auto &row : c.rows())
            if (row.toMap().value(QStringLiteral("disposition")) == QStringLiteral("excluded"))
                excluded << row.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(excluded, (QStringList{"setting:PROGRAM_THEME", "file:Themes/Mine.txt"}));
        QVERIFY2(c.summary().startsWith(QStringLiteral("%1 to import of ").arg(proposed - 1)), qPrintable(c.summary()));
        QVERIFY2(c.summary().contains(QStringLiteral(", 2 excluded, ")), qPrintable(c.summary()));
        QVERIFY(c.importChosen());
        QVERIFY(c.pending());
        QVERIFY(c.status().contains(QStringLiteral("starts again")));
        QVERIFY(c.canRollBack());
        QVERIFY(c.rollback());
        QVERIFY(c.status().contains(QStringLiteral("before the import")));
    }
};

QTEST_GUILESS_MAIN(SettingsImportTests)
#include "settings_import_ui_tests.moc"
