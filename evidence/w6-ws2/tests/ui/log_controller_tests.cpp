// P5: the log window's presenter against legacy LogHandler/LogWindow at
// 20d647c4: stamped history, a message pops the window up with just that
// message, silent messages only go into the history, the File menu entry
// shows the whole log and toggles, and other threads are published here.

#include "log_controller.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using hikari::ui::LogController;

class LogControllerTests : public QObject {
    Q_OBJECT

private slots:
    void messagesAreStampedAndAMessagePopsTheWindowUp()
    {
        LogController log;
        log.setClock([] { return QStringLiteral("12:34:56"); });
        log.log(QStringLiteral("quiet"), true);
        QVERIFY(!log.shown());
        QCOMPARE(log.history(), QStringLiteral("12:34:56 quiet\n"));
        log.log(QStringLiteral("Cannot open file"));
        QVERIFY(log.shown());
        QVERIFY(!log.full());
        QCOMPARE(log.lastMessage(), QStringLiteral("Cannot open file"));
        QCOMPARE(log.history(), QStringLiteral("12:34:56 quiet\n12:34:56 Cannot open file\n"));
        log.close();
        QVERIFY(!log.shown());
    }

    void theMenuShowsTheWholeLogAndToggles()
    {
        LogController log;
        log.toggleWindow();
        QVERIFY(log.shown());
        QVERIFY(log.full());
        log.toggleWindow();
        QVERIFY(!log.shown());
        // A later message pops up with just the message again.
        log.log(QStringLiteral("again"));
        QVERIFY(log.shown());
        QVERIFY(!log.full());
        // Legacy: the menu while a message shows switches to the whole log and hides.
        log.toggleWindow();
        QVERIFY(log.full());
        QVERIFY(!log.shown());
    }

    void messagesFromOtherThreadsArePublishedHere()
    {
        LogController log;
        QSignalSpy changed(&log, &LogController::changed);
        QThread *worker = QThread::create([&] { log.log(QStringLiteral("from a worker")); });
        worker->start();
        worker->wait();
        delete worker;
        QTRY_COMPARE(log.lastMessage(), QStringLiteral("from a worker"));
        QVERIFY(log.shown());
    }
};

QTEST_GUILESS_MAIN(LogControllerTests)
#include "log_controller_tests.moc"
