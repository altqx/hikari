#pragma once

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

    // Construct on the thread to watch; watching starts at once.
    explicit HangWatchdog(Config config, QObject *parent = nullptr);
    ~HangWatchdog() override;

    // Call on a window's render thread so a freeze record keeps its stack too; cheap to repeat, and
    // the latest render thread wins. Ignored on the watched thread (a non-threaded render loop).
    void recordRenderThread();
    // Call on that render thread before it ends.
    void forgetRenderThread();

signals:
    // The watched thread answers again after `record.seconds` stuck.
    void recovered(const hikari::app::crash::Record &record);

private:
    struct Thread;
    struct Stacks;
    void watch();
    QList<crash::Frame> captureStack(Thread &thread);

    Config m_config;
    std::unique_ptr<Stacks> m_stacks;
    std::unique_ptr<Thread> m_watched;
    std::unique_ptr<Thread> m_render;
    std::atomic<std::thread::id> m_renderId;
    QTimer m_heartbeat;
    std::atomic<qint64> m_beat;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_thread;
};

}
