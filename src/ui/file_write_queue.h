#pragma once

#include <QObject>
#include <QString>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hikari::ui {

/// Runs file writes on one worker thread, one at a time, so a busy disk never blocks the caller's thread.
/// A write put for a key replaces the one still waiting for that key; the running one finishes first.
/// Destruction waits for every queued write.
class FileWriteQueue : public QObject {
public:
    using Write = std::function<bool()>;
    using Done = std::function<void(bool written)>;

    explicit FileWriteQueue(QObject *parent = nullptr);
    ~FileWriteQueue() override;

    /// `done` runs on this object's thread after `write`, unless a newer write for `key` replaced it first.
    void put(const QString &key, Write write, Done done = {});
    /// Waits for every queued write, then runs their `done` before returning. Call it on this object's thread.
    void finishWrites();
    void setBeforeEachWrite(std::function<void()> hook);

private:
    struct Queued {
        QString key;
        Write write;
        Done done;
    };
    void work();
    void runHook();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_settled;
    std::vector<Queued> m_queued;
    bool m_busy = false;
    bool m_stopping = false;
    std::function<void()> m_beforeEachWrite;
    std::thread m_worker;
};

}
