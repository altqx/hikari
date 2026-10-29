// AD3 (#312): a macro run's progress card and log with real Lua helpers.
// A short run shows only the notice; about a second of the macro itself (not
// of its dialog) or its first output opens the card; when the run ends, the
// card says how and the run's log is kept in AutomationRuns, after Cancel
// and errors too, with the level each line was given.

#include "hikari/app/application.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::AutomationRunLines;
using ui::AutomationRuns;

namespace {

const std::string kFixture = HIKARI_LUA_FIXTURES "/progress-fixture.lua";

QStringList shown(const AutomationRunLines &lines)
{
    QStringList out;
    for (int row = 0; row < lines.rowCount(); ++row)
        out << lines.data(lines.index(row), AutomationRunLines::TextRole).toString();
    return out;
}

QList<int> levels(const AutomationRunLines &lines)
{
    QList<int> out;
    for (int row = 0; row < lines.rowCount(); ++row)
        out << lines.data(lines.index(row), AutomationRunLines::LevelRole).toInt();
    return out;
}

} // namespace

class AutomationProgressTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;

    app::AutomationShell &automation() { return application->automation(); }

    int ordinal(const char *name)
    {
        for (const auto &s : automation().scripts())
            if (s.path == kFixture)
                for (std::size_t i = 0; i < s.info.macros.size(); ++i)
                    if (s.info.macros[i].name == name)
                        return static_cast<int>(i);
        return -1;
    }
    bool start(const char *macro) { return automation().run(kFixture, ordinal(macro)); }
    bool ended(int ms = 30'000)
    {
        QDeadlineTimer deadline(ms);
        while (automation().running() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return !automation().running();
    }
    QString runState()
    {
        auto *runs = automation().runs();
        return runs->data(runs->index(runs->rowOf(automation().runId())), AutomationRuns::StateRole).toString();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        application = std::make_unique<app::Application>(o);
        const QString subtitles = dir.filePath(QStringLiteral("episode.ass"));
        QFile f(subtitles);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n");
        f.close();
        QVERIFY(application->openFile(subtitles));
        automation().load(kFixture);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(automation().scripts(), [](const auto &s) {
                                     return s.path == kFixture && s.state == application::ScriptStatus::State::Ready;
                                 }),
                                 30'000);
    }
    void cleanupTestCase() { application.reset(); }

    // A test that failed midway leaves no run behind for the next ones.
    void cleanup()
    {
        if (!automation().running())
            return;
        if (automation().dialogs()->isOpen())
            automation().dialogs()->finish(0, {});
        automation().cancelRun();
        QVERIFY(ended());
        automation().dismissCard();
    }

    // A run that ends at once and logs nothing: the notice, then nothing.
    void aShortRunShowsOnlyTheNotice()
    {
        automation().setCardDelay(1000);
        QVERIFY(start("Quick"));
        QCOMPARE(automation().cardState(), QStringLiteral("notice"));
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QString());
        QCOMPARE(runState(), QStringLiteral("applied"));
        QCOMPARE(automation().runs()->run(automation().runId())->lines.size(), std::size_t{0});
    }

    // The card after the delay of the macro's own time; Cancel keeps the
    // log and says how it ended.
    void theMacrosOwnSecondOpensTheCard()
    {
        automation().setCardDelay(300);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(start("Busy"));
        QCOMPARE(automation().cardState(), QStringLiteral("notice"));
        QTRY_COMPARE_WITH_TIMEOUT(automation().cardState(), QStringLiteral("running"), 10'000);
        QVERIFY2(timer.elapsed() >= 300, qPrintable(QString::number(timer.elapsed())));
        QTRY_VERIFY(automation().progressKnown());
        QCOMPARE(automation().task(), QStringLiteral("Working"));
        automation().cancelRun();
        QVERIFY(automation().cancelling());
        QVERIFY(ended());
        QVERIFY(!automation().cancelling());
        QCOMPARE(automation().cardState(), QStringLiteral("cancelled"));
        QCOMPARE(runState(), QStringLiteral("cancelled"));
        AutomationRunLines lines;
        lines.setRuns(automation().runs());
        lines.setRun(automation().runId());
        QCOMPARE(shown(lines), QStringList{QStringLiteral("The macro was cancelled; nothing was changed.")});
        QCOMPARE(levels(lines), QList<int>{AutomationRuns::kHostInfo});
        // × closes it; the run stays in Runs.
        automation().dismissCard();
        QCOMPARE(automation().cardState(), QString());
        QVERIFY(automation().runs()->run(automation().runId()));
    }

    // Output opens the card at once.
    void outputOpensTheCardAtOnce()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Talk then wait"));
        QTRY_COMPARE_WITH_TIMEOUT(automation().cardState(), QStringLiteral("running"), 10'000);
        AutomationRunLines lines;
        lines.setRuns(automation().runs());
        lines.setRun(automation().runId());
        QTRY_COMPARE(shown(lines), QStringList{QStringLiteral("started")});
        QCOMPARE(levels(lines), QList<int>{2});
        automation().cancelRun();
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("cancelled"));
        // The log is kept after Cancel: the script's line and how it ended.
        QCOMPARE(shown(lines), (QStringList{QStringLiteral("started"),
                                            QStringLiteral("The macro was cancelled; nothing was changed.")}));
    }

    // Each line keeps its level (those above the trace level, 3, never
    // arrive); the threshold hides the levels above it, never the lines
    // without one; output without a newline continues on the same line.
    void theLevelFilterIsAThreshold()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Levels"));
        QVERIFY(ended());
        // It applied and logged: the card stays.
        QCOMPARE(automation().cardState(), QStringLiteral("applied"));
        AutomationRunLines lines;
        lines.setRuns(automation().runs());
        lines.setRun(automation().runId());
        QCOMPARE(shown(lines), (QStringList{QStringLiteral("level 0"), QStringLiteral("level 1"), QStringLiteral("level 2"),
                                            QStringLiteral("level 3"), QStringLiteral("no level"),
                                            QStringLiteral("first part, second part")}));
        QCOMPARE(levels(lines), (QList<int>{0, 1, 2, 3, AutomationRuns::kNoLevel, AutomationRuns::kNoLevel}));
        QCOMPARE(lines.countAt(2), 1);
        QCOMPARE(lines.countAt(4), 0);
        lines.setLevelLimit(1);
        QCOMPARE(shown(lines), (QStringList{QStringLiteral("level 0"), QStringLiteral("level 1"), QStringLiteral("no level"),
                                            QStringLiteral("first part, second part")}));
        lines.setLevelLimit(0);
        QCOMPARE(lines.count(), 3);
        lines.setLevelLimit(5);
        lines.setFilterText(QStringLiteral("PART"));
        QCOMPARE(shown(lines), QStringList{QStringLiteral("first part, second part")});
        QCOMPARE(lines.total(), 6);
        automation().dismissCard();
    }

    // An error: the card says it failed with the error; the run's log keeps
    // the script's lines and the error.
    void anErrorKeepsTheLog()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Fail"));
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("failed"));
        QVERIFY2(automation().cardText().contains(QStringLiteral("boom")), qPrintable(automation().cardText()));
        QCOMPARE(runState(), QStringLiteral("failed"));
        AutomationRunLines lines;
        lines.setRuns(automation().runs());
        lines.setRun(automation().runId());
        const QStringList texts = shown(lines);
        QVERIFY(texts.size() >= 2);
        QCOMPARE(texts.first(), QStringLiteral("about to fail"));
        QCOMPARE(levels(lines).first(), 1);
        // Then the error, HikariSub's own line; its traceback's lines follow
        // without a level.
        QCOMPARE(levels(lines)[1], AutomationRuns::kHostError);
        QVERIFY2(texts.mid(1).join(QLatin1Char('|')).contains(QStringLiteral("boom")), qPrintable(texts.join(QLatin1Char('|'))));
        QCOMPARE(lines.countAt(AutomationRuns::kHostError), 1);
        // The level filter never hides HikariSub's own lines.
        lines.setLevelLimit(0);
        QVERIFY(shown(lines).join(QLatin1Char('|')).contains(QStringLiteral("boom")));
        automation().dismissCard();
    }

    // Clear runs takes an ended run's card with its run; a running one
    // stays, card and all.
    void clearingTheRunsTakesItsCard()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Fail"));
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("failed"));
        automation().runs()->clear();
        QCOMPARE(automation().cardState(), QString());
        QVERIFY(start("Talk then wait"));
        QTRY_COMPARE_WITH_TIMEOUT(automation().cardState(), QStringLiteral("running"), 10'000);
        automation().runs()->clear();
        QCOMPARE(automation().cardState(), QStringLiteral("running"));
        automation().cancelRun();
        QVERIFY(ended());
        automation().dismissCard();
    }

    // Validation that says no: failed, in a sentence.
    void aMacroThatIsNotValidFails()
    {
        QVERIFY(start("Never"));
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("failed"));
        QCOMPARE(automation().cardText(), QStringLiteral("Its validation did not let it run; nothing was changed."));
        automation().dismissCard();
    }

    // Time spent in the script's dialog does not count toward the second.
    void dialogTimeDoesNotCount()
    {
        automation().setCardDelay(300);
        QVERIFY(start("Ask"));
        auto *dialogs = automation().dialogs();
        QTRY_VERIFY_WITH_TIMEOUT(dialogs->isOpen(), 10'000);
        QTest::qWait(800);
        QCOMPARE(automation().cardState(), QStringLiteral("notice"));
        dialogs->finish(0, {});
        QVERIFY(ended());
        // It ended within its second, logging nothing: no card.
        QCOMPARE(automation().cardState(), QString());
    }

    // Time in a dialog pauses the second, it does not restart it: 1.5 s of
    // a 4 s second before the dialog (its work is CPU time, longer on a busy
    // machine), the card at most 2.5 s after it, not the 4 s of a restart.
    void theSecondResumesAfterADialog()
    {
        automation().setCardDelay(4000);
        QVERIFY(start("Work, ask, work"));
        auto *dialogs = automation().dialogs();
        QTRY_VERIFY_WITH_TIMEOUT(dialogs->isOpen(), 10'000);
        QTest::qWait(1000);
        QCOMPARE(automation().cardState(), QStringLiteral("notice"));
        QElapsedTimer timer;
        timer.start();
        dialogs->finish(0, {});
        QTRY_COMPARE_WITH_TIMEOUT(automation().cardState(), QStringLiteral("running"), 10'000);
        QVERIFY2(timer.elapsed() < 3500, qPrintable(QString::number(timer.elapsed())));
        automation().cancelRun();
        QVERIFY(ended());
        automation().dismissCard();
    }

    // Writing nothing is not logging: no card after it applied.
    void anEmptyWriteLeavesNoCard()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Empty log"));
        QVERIFY(ended());
        QCOMPARE(runState(), QStringLiteral("applied"));
        QCOMPARE(automation().cardState(), QString());
    }

    // The macro's validation function does not count toward the second: the
    // card opens about a second after the macro itself began.
    void validationTimeDoesNotCount()
    {
        automation().setCardDelay(1000);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(start("Slow to validate"));
        QTRY_COMPARE_WITH_TIMEOUT(automation().cardState(), QStringLiteral("running"), 10'000);
        QVERIFY2(timer.elapsed() >= 2400, qPrintable(QString::number(timer.elapsed())));
        automation().cancelRun();
        QVERIFY(ended());
        automation().dismissCard();
    }

    // Cancel during the validation opens the card: Cancelling…, and Force
    // stop if it does not stop, are in the card.
    void cancellingDuringValidationOpensTheCard()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Slow to validate"));
        QCOMPARE(automation().cardState(), QStringLiteral("notice"));
        automation().cancelRun();
        QCOMPARE(automation().cardState(), QStringLiteral("running"));
        QVERIFY(automation().cancelling());
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("cancelled"));
        automation().dismissCard();
    }

    // A first progress of 0 is a known progress (the bar, at its start).
    void aFirstProgressOfZeroIsKnown()
    {
        QVERIFY(start("Set zero"));
        QVERIFY(ended());
        QVERIFY(automation().progressKnown());
        QCOMPARE(automation().progress(), 0.0);
    }

    // A script's task is kept up to kMaxText characters.
    void aTaskIsBounded()
    {
        QVERIFY(start("Long task"));
        QVERIFY(ended());
        auto *runs = automation().runs();
        QCOMPARE(runs->run(automation().runId())->task.size(), AutomationRuns::kMaxText);
        QCOMPARE(automation().task().size(), AutomationRuns::kMaxText);
    }

    // The session keeps the last 20 runs, newest first.
    void runsKeepTheLastTwenty()
    {
        automation().setCardDelay(60'000);
        for (int i = 0; i < AutomationRuns::kKept + 2; ++i) {
            QVERIFY(start("Quick"));
            QVERIFY(ended());
        }
        auto *runs = automation().runs();
        QCOMPARE(runs->count(), AutomationRuns::kKept);
        QCOMPARE(runs->idAt(0), automation().runId());
        runs->clear();
        QCOMPARE(runs->count(), 0);
    }

    // The Document closed under a run (last: it closes the test's
    // Document): failed, saying why nothing was applied.
    void aClosedDocumentSaysWhy()
    {
        automation().setCardDelay(60'000);
        QVERIFY(start("Work a second"));
        QVERIFY(application->closeEditingTarget());
        QVERIFY(ended());
        QCOMPARE(automation().cardState(), QStringLiteral("failed"));
        QCOMPARE(automation().cardText(),
                 QStringLiteral("Its document was closed while the macro ran; nothing was changed."));
        AutomationRunLines lines;
        lines.setRuns(automation().runs());
        lines.setRun(automation().runId());
        QCOMPARE(lines.shownText(), QStringLiteral("Its document was closed while the macro ran; nothing was changed.\n"));
    }
};

QTEST_MAIN(AutomationProgressTests)
#include "automation_progress_tests.moc"
