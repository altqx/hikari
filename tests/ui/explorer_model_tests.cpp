#include "explorer_model.h"

#include "hikari/application/document_files.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::ui::ExplorerModel;

namespace {

void touch(const QString &path, const QByteArray &bytes = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QStringList names(const ExplorerModel &model)
{
    QStringList out;
    for (int row = 0; row < model.count(); ++row) {
        const auto r = model.rowAt(row);
        out << QString(r.value(QStringLiteral("depth")).toInt() * 2, QLatin1Char(' ')) +
                   r.value(QStringLiteral("name")).toString();
    }
    return out;
}

QString kindOf(const ExplorerModel &model, const QString &name)
{
    for (int row = 0; row < model.count(); ++row)
        if (model.rowAt(row).value(QStringLiteral("name")).toString() == name)
            return model.rowAt(row).value(QStringLiteral("kind")).toString();
    return {};
}

int rowNamed(const ExplorerModel &model, const QString &name)
{
    for (int row = 0; row < model.count(); ++row)
        if (model.rowAt(row).value(QStringLiteral("name")).toString() == name)
            return row;
    return -1;
}

}

class ExplorerModelTests : public QObject {
    Q_OBJECT

    // Off the home volume (a tmpfs /tmp) Qt trashes into a top-level .Trash-<uid> outside the test.
    QTemporaryDir m_dir{QDir::current().filePath(QStringLiteral("explorer-XXXXXX"))};
    QString m_root;

    QString at(const QString &relative) const { return QDir(m_root).filePath(relative); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString xdg = QDir(m_dir.path()).filePath(QStringLiteral("xdg"));
        QVERIFY(QDir().mkpath(xdg));
        qputenv("XDG_DATA_HOME", xdg.toUtf8());
    }

    void init()
    {
        m_root = QDir(m_dir.path()).filePath(QString::fromLatin1(QTest::currentTestFunction()));
        QDir().mkpath(m_root);
        for (const char *name : {"ep01.ass", "ep01.mkv", "ep01_keyframes.txt", "ep01.mkv.lwi", "ep01.mkv.ffindex",
                                 "ep02.ass", "ep10.ass", "readme.md", ".notes.ass", "fonts/Font.ttf", "raws/ep02.mkv",
                                 "chapters.xml", "music.flac", "ep01_tc.txt", ".hikari/project.toml", ".git/HEAD"})
            touch(at(QString::fromLatin1(name)));
    }

    void cleanup() { QDir(m_root).removeRecursively(); }

    void theDefaultFilterShowsHikariSubFilesFoldersFirst()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        QCOMPARE(names(model), (QStringList{"Project settings", "fonts", "raws", "chapters.xml", "ep01.ass",
                                            "ep01.mkv", "ep01_keyframes.txt", "ep01_tc.txt", "ep02.ass", "ep10.ass",
                                            "music.flac"}));
        QCOMPARE(kindOf(model, "Project settings"), QStringLiteral("projectSettings"));
        QCOMPARE(kindOf(model, "fonts"), QStringLiteral("folder"));
        QCOMPARE(kindOf(model, "ep01.ass"), QStringLiteral("subtitles"));
        QCOMPARE(kindOf(model, "ep01.mkv"), QStringLiteral("video"));
        QCOMPARE(kindOf(model, "ep01_keyframes.txt"), QStringLiteral("keyframes"));
        QCOMPARE(kindOf(model, "ep01_tc.txt"), QStringLiteral("timecodes"));
        QCOMPARE(kindOf(model, "chapters.xml"), QStringLiteral("chapters"));
        QCOMPARE(kindOf(model, "music.flac"), QStringLiteral("audio"));
        QCOMPARE(model.rowAt(0).value(QStringLiteral("path")).toString(), at(".hikari"));
    }

    void allFilesShowsOtherFilesButNotHiddenOrExcludedOnes()
    {
        ExplorerModel model;
        model.setFolder(m_root);
        QSignalSpy filter(&model, &ExplorerModel::filterChanged);
        model.setAllFiles(true);
        QCOMPARE(filter.count(), 1);
        QVERIFY(rowNamed(model, "readme.md") >= 0);
        QCOMPARE(rowNamed(model, "ep01.mkv.lwi"), -1);
        QCOMPARE(rowNamed(model, "ep01.mkv.ffindex"), -1);
        QCOMPARE(rowNamed(model, ".notes.ass"), -1);
        QCOMPARE(rowNamed(model, ".git"), -1);
        model.setAllFiles(false);
        QCOMPARE(rowNamed(model, "readme.md"), -1);
    }

    void excludesFollowTheSettingAndReachFolders()
    {
        ExplorerModel model;
        QCOMPARE(model.excludes(), (QStringList{"*.lwi", "*.ffindex"}));
        model.setFolder(m_root);
        model.setAllFiles(true);
        model.setExcludes({QStringLiteral("raws"), QStringLiteral("*.md"), QStringLiteral(" ")});
        QCOMPARE(rowNamed(model, "raws"), -1);
        QCOMPARE(rowNamed(model, "readme.md"), -1);
        QVERIFY(rowNamed(model, "ep01.mkv.lwi") >= 0);
        model.setExcludes({QStringLiteral("fonts/*.ttf")});
        model.setExpanded(rowNamed(model, "fonts"), true);
        QCOMPARE(rowNamed(model, "Font.ttf"), -1);
        QVERIFY(rowNamed(model, "raws") >= 0);
    }

    void foldersExpandAndCollapse()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        const int fonts = rowNamed(model, "fonts");
        QVERIFY(model.rowAt(fonts).value(QStringLiteral("expandable")).toBool());
        QVERIFY(!model.rowAt(fonts).value(QStringLiteral("expanded")).toBool());
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.toggle(fonts);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(names(model).mid(fonts, 3), (QStringList{"fonts", "  Font.ttf", "raws"}));
        QCOMPARE(model.folderAt(fonts + 1), at("fonts"));
        QCOMPARE(model.folderAt(fonts), at("fonts"));
        QCOMPARE(model.folderAt(0), m_root);
        model.toggle(fonts);
        QCOMPARE(rowNamed(model, "Font.ttf"), -1);
    }

    void groupByNameGathersTheEpisodeFiles()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        model.setGroupByName(true);
        QCOMPARE(names(model), (QStringList{"Project settings", "fonts", "raws", "chapters.xml", "ep01.ass",
                                            "ep02.ass", "ep10.ass", "music.flac"}));
        const int lead = rowNamed(model, "ep01.ass");
        QVERIFY(model.rowAt(lead).value(QStringLiteral("expandable")).toBool());
        QVERIFY(!model.rowAt(rowNamed(model, "ep02.ass")).value(QStringLiteral("expandable")).toBool());
        model.setExpanded(lead, true);
        QCOMPARE(names(model).mid(lead, 5),
                 (QStringList{"ep01.ass", "  ep01.mkv", "  ep01_keyframes.txt", "  ep01_tc.txt", "ep02.ass"}));
        QCOMPARE(model.folderAt(lead + 1), m_root);
        model.setGroupByName(false);
        QVERIFY(!model.rowAt(rowNamed(model, "ep01.ass")).value(QStringLiteral("expandable")).toBool());
        QCOMPARE(model.rowAt(rowNamed(model, "ep01.mkv")).value(QStringLiteral("depth")).toInt(), 0);
    }

    void namesAreChecked()
    {
        ExplorerModel model;
        model.setFolder(m_root);
        QVERIFY(!model.nameProblem(m_root, QStringLiteral(" ")).isEmpty());
        QVERIFY(!model.nameProblem(m_root, QStringLiteral("..")).isEmpty());
        QVERIFY(!model.nameProblem(m_root, QStringLiteral("a/b.ass")).isEmpty());
        QVERIFY(!model.nameProblem(m_root, QStringLiteral("a\\b.ass")).isEmpty());
        QVERIFY(!model.nameProblem(m_root, QStringLiteral("ep02.ass")).isEmpty());
        QVERIFY(model.nameProblem(m_root, QStringLiteral("ep02.ass"), at("ep02.ass")).isEmpty());
        QVERIFY(model.nameProblem(m_root, QStringLiteral("ep03.ass")).isEmpty());
    }

    void renameKeepsTheSelectionAndOpenFolders()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        model.setExpanded(rowNamed(model, "fonts"), true);
        model.setCurrentRow(rowNamed(model, "Font.ttf"));
        QCOMPARE(model.currentPath(), at("fonts/Font.ttf"));
        auto result = model.rename(at("fonts"), QStringLiteral("typefaces"));
        QCOMPARE(result.value(QStringLiteral("problem")).toString(), QString());
        QCOMPARE(result.value(QStringLiteral("path")).toString(), at("typefaces"));
        QVERIFY(QFileInfo::exists(at("typefaces/Font.ttf")));
        QVERIFY(model.rowAt(rowNamed(model, "typefaces")).value(QStringLiteral("expanded")).toBool());
        QCOMPARE(model.currentPath(), at("typefaces/Font.ttf"));
        QCOMPARE(model.currentRow(), rowNamed(model, "Font.ttf"));

        result = model.rename(at("ep02.ass"), QStringLiteral("ep10.ass"));
        QVERIFY(!result.value(QStringLiteral("problem")).toString().isEmpty());
        QVERIFY(QFileInfo::exists(at("ep02.ass")));
        result = model.rename(at("ep02.ass"), QStringLiteral("ep03.ass"));
        QCOMPARE(result.value(QStringLiteral("path")).toString(), at("ep03.ass"));
        QVERIFY(rowNamed(model, "ep03.ass") >= 0);
        QCOMPARE(rowNamed(model, "ep02.ass"), -1);
    }

    void moveToTrashTakesTheRowAway()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        model.setCurrentRow(rowNamed(model, "ep02.ass"));
        const int row = model.currentRow();
        QCOMPARE(model.moveToTrash(at("ep02.ass")), QString());
        QVERIFY(!QFileInfo::exists(at("ep02.ass")));
        QCOMPARE(rowNamed(model, "ep02.ass"), -1);
        QCOMPARE(model.currentRow(), row);
        QCOMPARE(model.currentPath(), at("ep10.ass"));
        QVERIFY(!model.moveToTrash(at("missing.ass")).isEmpty());
    }

    void newSubtitleFileWritesTheUntitledScript()
    {
        ExplorerModel model;
        model.setFolder(m_root);
        auto result = model.newSubtitleFile(at("raws"), QStringLiteral("ep02.ass"));
        QCOMPARE(result.value(QStringLiteral("problem")).toString(), QString());
        const QString path = result.value(QStringLiteral("path")).toString();
        QCOMPARE(path, at("raws/ep02.ass"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto script = hikari::application::defaultScript();
        QCOMPARE(file.readAll(), QByteArray(script.data(), static_cast<qsizetype>(script.size())));
        QVERIFY(model.rowAt(rowNamed(model, "raws")).value(QStringLiteral("expanded")).toBool());
        QCOMPARE(model.currentPath(), path);
        result = model.newSubtitleFile(at("raws"), QStringLiteral("ep02.ass"));
        QVERIFY(!result.value(QStringLiteral("problem")).toString().isEmpty());
    }

    void theTreeFollowsTheDisk()
    {
        ExplorerModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setFolder(m_root);
        model.setExpanded(rowNamed(model, "raws"), true);
        touch(at("ep03.ass"));
        QTRY_VERIFY(rowNamed(model, "ep03.ass") >= 0);
        touch(at("raws/ep03.mkv"));
        QTRY_COMPARE(model.rowAt(rowNamed(model, "ep03.mkv")).value(QStringLiteral("depth")).toInt(), 1);
        QVERIFY(QFile::remove(at("ep01.ass")));
        QTRY_COMPARE(rowNamed(model, "ep01.ass"), -1);
        QVERIFY(QDir(at(".hikari")).removeRecursively());
        QTRY_COMPARE(rowNamed(model, "Project settings"), -1);
        QVERIFY(QDir(at("raws")).removeRecursively());
        QTRY_COMPARE(rowNamed(model, "raws"), -1);
        QDir().mkpath(at("raws"));
        touch(at("raws/ep04.mkv"));
        QTRY_VERIFY(rowNamed(model, "raws") >= 0);
        QVERIFY(model.rowAt(rowNamed(model, "raws")).value(QStringLiteral("expanded")).toBool());
        QTRY_VERIFY(rowNamed(model, "ep04.mkv") >= 0);
    }

    void aNewRootStartsOverAndNoRootIsEmpty()
    {
        ExplorerModel model;
        model.setFolder(m_root);
        model.setExpanded(rowNamed(model, "fonts"), true);
        model.setCurrentRow(1);
        model.setFolder(QString());
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.currentPath(), QString());
        model.setFolder(m_root);
        QVERIFY(!model.rowAt(rowNamed(model, "fonts")).value(QStringLiteral("expanded")).toBool());
        model.setFolder(at("missing"));
        QCOMPARE(model.count(), 0);
    }
};

QTEST_GUILESS_MAIN(ExplorerModelTests)
#include "explorer_model_tests.moc"
