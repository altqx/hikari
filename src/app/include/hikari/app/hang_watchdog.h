#pragma once

// CR1 (#452): the hang watchdog. The thread that makes it (the window's)
// stamps a heartbeat every tick; a thread of the watchdog's own checks it.
// After `thresholdMs` without one, the watchdog records where the window's
// thread is, its stack as module + offset frames like a crash record
// (crash_record.h, type APP_HANG), in `recordPath`, and keeps the record's
// seconds up to date while the thread stays stuck. When it answers again,
// the record leaves the disk and comes back through recovered(); when the
// app is closed while stuck, the record stays for the next start.

#include "hikari/app/crash_record.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace hikari::app {

class HangWatchdog : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString recordPath;
        QString version;
        int thresholdMs = 10000;
        int tickMs = 1000;
    };

    // On the thread to watch; it starts watching at once.
    explicit HangWatchdog(Config config, QObject *parent = nullptr);
    ~HangWatchdog() override;

signals:
    // The watched thread answered again after `record.seconds` without.
    void recovered(const hikari::app::crash::Record &record);

private:
    struct Target;
    void watch();
    QList<crash::Frame> captureStack();

    Config m_config;
    std::unique_ptr<Target> m_target;
    QTimer m_heartbeat;
    std::atomic<qint64> m_beat;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_thread;
};

} // namespace hikari::app
