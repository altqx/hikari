#include "file_write_queue.h"

#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <mutex>
#include <optional>

using hikari::ui::FileWriteQueue;

class FileWriteQueueTest : public QObject {
    Q_OBJECT

    struct Log {
        std::mutex mutex;
        QStringList entries;
        void add(const QString &entry)
        {
            std::lock_guard lock(mutex);
            entries << entry;
        }
        QStringList all()
        {
            std::lock_guard lock(mutex);
            return entries;
        }
    };

private slots:
    void aNewerWriteReplacesTheOneWaitingForItsKey()
    {
        Log written;
        QStringList done;
        QSemaphore started, release;
        {
            FileWriteQueue queue;
            queue.put(QStringLiteral("a"), [&] {
                started.release();
                release.acquire();
                written.add(QStringLiteral("a1"));
                return true;
            }, [&](bool) { done << QStringLiteral("a1"); });
            started.acquire();
            for (const QString &version : {QStringLiteral("a2"), QStringLiteral("a3")})
                queue.put(QStringLiteral("a"), [&written, version] {
                    written.add(version);
                    return true;
                }, [&done, version](bool) { done << version; });
            queue.put(QStringLiteral("b"), [&] {
                written.add(QStringLiteral("b1"));
                return false;
            });
            release.release();
            queue.finishWrites();
            QCOMPARE(written.all(), (QStringList{QStringLiteral("a1"), QStringLiteral("a3"), QStringLiteral("b1")}));
            QCOMPARE(done, (QStringList{QStringLiteral("a1"), QStringLiteral("a3")}));
        }
    }

    void doneHearsWhetherTheWriteWorked()
    {
        FileWriteQueue queue;
        std::optional<bool> heard;
        queue.put(QStringLiteral("a"), [] { return false; }, [&](bool written) { heard = written; });
        QTRY_VERIFY(heard.has_value());
        QVERIFY(!*heard);
        queue.put(QStringLiteral("a"), [] { return true; }, [&](bool written) { heard = written; });
        queue.finishWrites();
        QVERIFY(*heard);
    }

    void aSlowWriteLeavesTheEventLoopRunning()
    {
        FileWriteQueue queue;
        queue.setBeforeEachWrite([] { QThread::msleep(1000); });
        int ticks = 0;
        QTimer tick;
        tick.setInterval(20);
        connect(&tick, &QTimer::timeout, this, [&ticks] { ++ticks; });
        tick.start();
        bool finished = false;
        QElapsedTimer took;
        took.start();
        queue.put(QStringLiteral("a"), [] { return true; }, [&](bool) { finished = true; });
        QVERIFY(took.elapsed() < 200);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
        QVERIFY2(ticks >= 15, qPrintable(QString::number(ticks)));
    }

    void destructionWaitsForEveryQueuedWrite()
    {
        std::atomic<int> written = 0;
        {
            FileWriteQueue queue;
            queue.setBeforeEachWrite([] { QThread::msleep(100); });
            for (const char *key : {"a", "b", "c"})
                queue.put(QLatin1String(key), [&written] {
                    ++written;
                    return true;
                });
        }
        QCOMPARE(written.load(), 3);
    }
};

QTEST_GUILESS_MAIN(FileWriteQueueTest)
#include "file_write_queue_tests.moc"
