#include "log_controller.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using hikari::ui::LogController;
using hikari::ui::LogEntries;

class LogControllerTests : public QObject {
    Q_OBJECT

    static QStringList shownTexts(const LogController &log)
    {
        QStringList texts;
        for (int row = 0; row < log.entries()->rowCount(); ++row)
            texts << log.entries()->data(log.entries()->index(row, 0), LogEntries::TextRole).toString();
        return texts;
    }

private slots:
    void messagesAreStampedAndOnlyNoticesAskForAToast()
    {
        LogController log;
        log.setClock([] { return QStringLiteral("12:34:56"); });
        QSignalSpy noticed(&log, &LogController::noticed);
        log.log(QStringLiteral("quiet"));
        QCOMPARE(noticed.size(), 0);
        QCOMPARE(log.history(), QStringLiteral("12:34:56 quiet\n"));
        log.notice(LogController::Error, QStringLiteral("Open subtitles"), QStringLiteral("Cannot open file"),
                   QStringLiteral("not UTF-8\nline 48"));
        QCOMPARE(noticed.size(), 1);
        QCOMPARE(noticed[0][0].toInt(), 2);
        QCOMPARE(noticed[0][1].value<LogController::Severity>(), LogController::Error);
        QCOMPARE(noticed[0][2].toString(), QStringLiteral("Open subtitles"));
        QCOMPARE(noticed[0][4].toString(), QStringLiteral("not UTF-8\nline 48"));
        QCOMPARE(log.lastMessage(), QStringLiteral("Cannot open file"));
        QCOMPARE(log.history(), QStringLiteral("12:34:56 quiet\n12:34:56 Open subtitles: Cannot open file\n"
                                               "    not UTF-8\n    line 48\n"));
        QCOMPARE(log.infoCount(), 1);
        QCOMPARE(log.errorCount(), 1);
        QCOMPARE(log.entries()->rowCount(), 2);
    }

    void theEntriesFilterBySeverityAndText()
    {
        LogController log;
        log.notice(LogController::Info, QStringLiteral("Hotkeys"), QStringLiteral("removed"));
        log.notice(LogController::Warning, QStringLiteral("Hotkeys"), QStringLiteral("invalid"));
        log.notice(LogController::Error, QStringLiteral("Save"), QStringLiteral("not saved"), QStringLiteral("disk full"));
        log.entries()->setShowInfo(false);
        QCOMPARE(shownTexts(log), (QStringList{QStringLiteral("invalid"), QStringLiteral("not saved")}));
        log.entries()->setFilterText(QStringLiteral("DISK"));
        QCOMPARE(shownTexts(log), QStringList{QStringLiteral("not saved")});
        log.entries()->setFilterText(QStringLiteral("hotkeys"));
        QCOMPARE(shownTexts(log), QStringList{QStringLiteral("invalid")});
        QCOMPARE(log.shownText().count(QLatin1Char('\n')), 1);
        log.notice(LogController::Warning, QStringLiteral("Hotkeys"), QStringLiteral("again"));
        QCOMPARE(shownTexts(log), (QStringList{QStringLiteral("invalid"), QStringLiteral("again")}));
    }

    void detailsMarksItsEntryWhateverTheFilters()
    {
        LogController log;
        log.notice(LogController::Info, QStringLiteral("Delete unused styles"), QStringLiteral("2 deleted"));
        log.notice(LogController::Warning, QStringLiteral("Hotkeys"), QStringLiteral("invalid"));
        log.entries()->setShowInfo(false);
        log.entries()->setFilterText(QStringLiteral("hotkeys"));
        QSignalSpy revealed(&log, &LogController::revealRequested);
        log.reveal(1);
        QCOMPARE(revealed.size(), 1);
        QCOMPARE(log.marked(), 1);
        QVERIFY(log.entries()->showInfo());
        QCOMPARE(log.entries()->filterText(), QString());
        QCOMPARE(log.entries()->rowOf(1), 0);
        log.reveal(0);
        QCOMPARE(log.marked(), 0);
        QCOMPARE(revealed.size(), 2);
    }

    void noticesBeforeTheToastsAreGivenAgain()
    {
        LogController log;
        for (int i = 1; i <= 12; ++i)
            log.notice(LogController::Warning, QStringLiteral("Hotkeys"), QStringLiteral("retired %1").arg(i));
        QSignalSpy noticed(&log, &LogController::noticed);
        log.notice(LogController::Info, QStringLiteral("Hotkeys"), QStringLiteral("connected"));
        QCOMPARE(noticed.size(), 1);
        log.replayNotices();
        QCOMPARE(noticed.size(), 11);
        QCOMPARE(noticed[1][0].toInt(), 3);
        QCOMPARE(noticed[1][3].toString(), QStringLiteral("retired 3"));
        QCOMPARE(noticed[10][3].toString(), QStringLiteral("retired 12"));
        log.replayNotices();
        QCOMPARE(noticed.size(), 11);
        QCOMPARE(log.warningCount(), 12);
    }

    void messagesFromOtherThreadsArePublishedHere()
    {
        LogController log;
        QSignalSpy noticed(&log, &LogController::noticed);
        QThread *worker = QThread::create([&] {
            log.log(QStringLiteral("from a worker"));
            log.notice(LogController::Warning, QStringLiteral("Audio"), QStringLiteral("noticed by a worker"));
        });
        worker->start();
        worker->wait();
        delete worker;
        QTRY_COMPARE(noticed.size(), 1);
        QCOMPARE(log.lastMessage(), QStringLiteral("noticed by a worker"));
        QCOMPARE(log.entries()->rowCount(), 2);
    }
};

QTEST_GUILESS_MAIN(LogControllerTests)
#include "log_controller_tests.moc"
