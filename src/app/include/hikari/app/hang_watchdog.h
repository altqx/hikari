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

signals:
    // The watched thread answers again after `record.seconds` stuck.
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

}
