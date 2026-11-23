#include "file_write_queue.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>

#include <algorithm>

namespace hikari::ui {

FileWriteQueue::FileWriteQueue(QObject *parent) : QObject(parent) {}

FileWriteQueue::~FileWriteQueue()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void FileWriteQueue::put(const QString &key, Write write, Done done)
{
    {
        std::lock_guard lock(m_mutex);
        Queued queued{key, std::move(write), std::move(done)};
        if (const auto it = std::ranges::find(m_queued, key, &Queued::key); it != m_queued.end())
            *it = std::move(queued);
        else
            m_queued.push_back(std::move(queued));
        if (!m_worker.joinable())
            m_worker = std::thread([this] { work(); });
    }
    m_wake.notify_one();
}

void FileWriteQueue::finishWrites()
{
    {
        std::unique_lock lock(m_mutex);
        m_settled.wait(lock, [&] { return m_queued.empty() && !m_busy; });
    }
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

void FileWriteQueue::setBeforeEachWrite(std::function<void()> hook)
{
    std::lock_guard lock(m_mutex);
    m_beforeEachWrite = std::move(hook);
}

void FileWriteQueue::runHook()
{
    std::function<void()> hook;
    {
        std::lock_guard lock(m_mutex);
        hook = m_beforeEachWrite;
    }
    if (hook)
        hook();
}

void FileWriteQueue::work()
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [&] { return m_stopping || !m_queued.empty(); });
        if (m_queued.empty())
            return;
        Queued next = std::move(m_queued.front());
        m_queued.erase(m_queued.begin());
        m_busy = true;
        lock.unlock();
        runHook();
        const bool written = next.write();
        if (next.done)
            QMetaObject::invokeMethod(this, [done = std::move(next.done), written] { done(written); },
                                      Qt::QueuedConnection);
        lock.lock();
        m_busy = false;
        m_settled.notify_all();
    }
}

}
