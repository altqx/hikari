// AD3 (#312): the runs kept for the Automation panel and their lines.
// Output is kept as Aegisub shows it (text without a newline continues on
// the same line); a run's lines show by a level threshold and a text filter,
// HikariSub's own lines whatever the level; the session keeps 20 runs, never
// dropping the one running; Copy and Save take the lines shown.

#include "automation_runs.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::ui::AutomationRunLines;
using hikari::ui::AutomationRuns;

class AutomationRunsTests : public QObject {
    Q_OBJECT

    static QStringList texts(const AutomationRunLines &lines)
    {
        QStringList out;
        for (int row = 0; row < lines.rowCount(); ++row)
            out << lines.data(lines.index(row), AutomationRunLines::TextRole).toString();
        return out;
    }

private slots:
    void outputContinuesTheOpenLine()
    {
        AutomationRuns runs;
        qint64 now = 0;
        runs.setClock([&] { return now; });
        const int id = runs.begin(QStringLiteral("Macro"), QStringLiteral("a.lua"), QStringLiteral("e.ass"));
        AutomationRunLines lines;
        lines.setRuns(&runs);
        lines.setRun(id);
        runs.append(id, QStringLiteral("one"), 2);
        now = 1208;
        runs.append(id, QStringLiteral(" two\nthree\n"));
        runs.append(id, QStringLiteral("\n"));
        QCOMPARE(texts(lines), (QStringList{QStringLiteral("one two"), QStringLiteral("three"), QString()}));
        // The level and time are the line's first part's.
        QCOMPARE(lines.data(lines.index(0), AutomationRunLines::LevelRole).toInt(), 2);
        QCOMPARE(lines.data(lines.index(0), AutomationRunLines::TimeRole).toString(), QStringLiteral("0:00.000"));
        QCOMPARE(lines.data(lines.index(1), AutomationRunLines::TimeRole).toString(), QStringLiteral("0:01.208"));
        QCOMPARE(lines.data(lines.index(0), AutomationRunLines::LevelNameRole).toString(), QStringLiteral("warning"));
        QCOMPARE(runs.data(runs.index(0), AutomationRuns::LineCountRole).toInt(), 3);
    }

    void aContinuedLineFollowsTheFilter()
    {
        AutomationRuns runs;
        const int id = runs.begin(QStringLiteral("Macro"), QStringLiteral("a.lua"), {});
        AutomationRunLines lines;
        lines.setRuns(&runs);
        lines.setRun(id);
        lines.setFilterText(QStringLiteral("needle"));
        runs.append(id, QStringLiteral("hay "));
        QCOMPARE(lines.count(), 0);
        runs.append(id, QStringLiteral("needle\n"));
        QCOMPARE(texts(lines), QStringList{QStringLiteral("hay needle")});
        lines.setFilterText(QStringLiteral("hay"));
        runs.append(id, QStringLiteral("more"));
        QCOMPARE(texts(lines), QStringList{QStringLiteral("hay needle")});
    }

    void theThresholdKeepsUnlevelledAndOwnLines()
    {
        AutomationRuns runs;
        runs.setClock([] { return qint64{0}; });
        const int id = runs.begin(QStringLiteral("Macro"), QStringLiteral("a.lua"), {});
        for (int level = 0; level <= 5; ++level)
            runs.append(id, QStringLiteral("l%1\n").arg(level), level);
        runs.append(id, QStringLiteral("plain\n"));
        runs.note(id, QStringLiteral("a.lua:3: boom\nstack traceback:\n"), AutomationRuns::kHostError);
        AutomationRunLines lines;
        lines.setRuns(&runs);
        lines.setRun(id);
        QCOMPARE(lines.count(), 9);
        lines.setLevelLimit(2);
        QCOMPARE(texts(lines), (QStringList{QStringLiteral("l0"), QStringLiteral("l1"), QStringLiteral("l2"),
                                            QStringLiteral("plain"), QStringLiteral("a.lua:3: boom"),
                                            QStringLiteral("stack traceback:")}));
        QCOMPARE(lines.countAt(4), 1);
        QCOMPARE(lines.countAt(AutomationRuns::kHostError), 1);
        // Copy takes the lines shown, levels named; times when on.
        QCOMPARE(lines.shownText(), QStringLiteral("[fatal] l0\n[error] l1\n[warning] l2\nplain\na.lua:3: boom\nstack traceback:\n"));
        lines.setFilterText(QStringLiteral("L1"));
        lines.setTimes(true);
        QCOMPARE(lines.shownText(), QStringLiteral("0:00.000 [error] l1\n"));
    }

    void saveWritesTheLinesShownHeadedByTheRun()
    {
        AutomationRuns runs;
        runs.setWallClock([] { return QDateTime(QDate(2026, 10, 22), QTime(16, 42, 5)); });
        const int id = runs.begin(QStringLiteral("Fix: overlaps?"), QStringLiteral("timing.lua"), QStringLiteral("e.ass"));
        runs.append(id, QStringLiteral("kept\n"), 1);
        runs.append(id, QStringLiteral("hidden\n"), 5);
        runs.finish(id, QStringLiteral("failed"), QStringLiteral("boom"));
        AutomationRunLines lines;
        lines.setRuns(&runs);
        lines.setRun(id);
        lines.setLevelLimit(3);
        QCOMPARE(lines.suggestedName(), QStringLiteral("Fix_ overlaps_ 2026-10-22 16-42.log"));
        QTemporaryDir dir;
        const QString path = dir.filePath(lines.suggestedName());
        QCOMPARE(lines.saveShown(QUrl::fromLocalFile(path)), QString());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()),
                 QStringLiteral("Fix: overlaps? (timing.lua)\ne.ass\n2026-10-22T16:42:05, failed\nboom\n\n[error] kept\n"));
        QVERIFY(!lines.saveShown(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/x.log")))).isEmpty());
    }

    void theSessionKeepsTwentyRunsAndNeverTheRunningOne()
    {
        AutomationRuns runs;
        const int first = runs.begin(QStringLiteral("Long"), QStringLiteral("a.lua"), {});
        QSignalSpy removed(&runs, &AutomationRuns::removed);
        for (int i = 0; i < AutomationRuns::kKept + 3; ++i) {
            const int id = runs.begin(QStringLiteral("Short"), QStringLiteral("a.lua"), {});
            runs.finish(id, QStringLiteral("applied"), {});
        }
        QCOMPARE(runs.count(), AutomationRuns::kKept);
        // The first run still runs: kept, the oldest finished ones went.
        QVERIFY(runs.run(first));
        QCOMPARE(removed.size(), 4);
        QCOMPARE(runs.data(runs.index(0), AutomationRuns::MacroRole).toString(), QStringLiteral("Short"));
        // Clear leaves only the running run.
        AutomationRunLines lines;
        lines.setRuns(&runs);
        lines.setRun(runs.idAt(0));
        runs.clear();
        QCOMPARE(runs.count(), 1);
        QCOMPARE(runs.idAt(0), first);
        QCOMPARE(lines.count(), 0);
        QCOMPARE(lines.total(), 0);
    }
};

QTEST_MAIN(AutomationRunsTests)
#include "automation_runs_tests.moc"
