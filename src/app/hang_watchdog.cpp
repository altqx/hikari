#include "hikari/app/hang_watchdog.h"

#include <QFile>
#include <QThread>

#include <chrono>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <execinfo.h>
#include <pthread.h>
#endif

namespace hikari::app {

namespace {

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr qint64 kRewriteMs = 5000;

#ifndef _WIN32

int stackSignal()
{
    return SIGRTMIN + 7;
}

constexpr int kRoom = crash::kMaxFrames + 2;
void *g_stack[kRoom];
std::atomic<int> g_count{-1};

void onStackSignal(int)
{
    const int saved = errno;
    g_count.store(backtrace(g_stack, kRoom));
    errno = saved;
}

#endif

}

#ifdef _WIN32

// The stack is copied and unwound after the thread resumes: the unwinder may take locks
// the suspended thread holds (the QML JIT's dynamic function tables).
struct HangWatchdog::Target {
    HANDLE thread = nullptr;
    quintptr stackBase = 0;
    std::vector<char> copy = std::vector<char>(1024 * 1024);
    ~Target()
    {
        if (thread)
            CloseHandle(thread);
    }
};

#else

struct HangWatchdog::Target {
    pthread_t thread{};
    struct sigaction previous{};
};

#endif

HangWatchdog::HangWatchdog(Config config, QObject *parent)
    : QObject(parent), m_config(std::move(config)), m_target(std::make_unique<Target>()), m_beat(nowMs())
{
#ifdef _WIN32
    m_target->thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                  GetCurrentThreadId());
    m_target->stackBase = reinterpret_cast<quintptr>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
#else
    m_target->thread = pthread_self();
    // glibc's first backtrace() call loads libgcc, which a signal handler must not do.
    void *warm[1];
    backtrace(warm, 1);
    struct sigaction action {};
    action.sa_handler = onStackSignal;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(stackSignal(), &action, &m_target->previous);
#endif
    m_heartbeat.setInterval(m_config.tickMs);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] { m_beat.store(nowMs()); });
    m_heartbeat.start();
    m_thread = std::thread([this] { watch(); });
}

HangWatchdog::~HangWatchdog()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
#ifndef _WIN32
    sigaction(stackSignal(), &m_target->previous, nullptr);
#endif
}

void HangWatchdog::watch()
{
    bool frozen = false;
    qint64 frozenFrom = 0;
    qint64 written = 0;
    qint64 last = nowMs();
    crash::Record record;
    std::unique_lock lock(m_mutex);
    while (!m_wake.wait_for(lock, std::chrono::milliseconds(m_config.tickMs), [this] { return m_stop; })) {
        const qint64 now = nowMs();
        // Slept far past the tick: the system was suspended, not the window's thread stuck.
        if (now - last > 5 * qint64(m_config.tickMs) && !frozen)
            m_beat.store(now);
        last = now;
        const qint64 beat = m_beat.load();
        if (!frozen) {
            if (now - beat < m_config.thresholdMs)
                continue;
            frozen = true;
            frozenFrom = beat;
            record = {QLatin1String(crash::kFreezeType), QStringLiteral("watchdog"), m_config.version, captureStack(),
                      int((now - beat) / 1000)};
            crash::write(m_config.recordPath, record);
            written = now;
        } else if (beat > frozenFrom) {
            frozen = false;
            QFile::remove(m_config.recordPath);
            record.seconds = std::max(int((beat - frozenFrom) / 1000), m_config.thresholdMs / 1000);
            QMetaObject::invokeMethod(this, [this, record] { emit recovered(record); }, Qt::QueuedConnection);
        } else if (now - written >= kRewriteMs) {
            record.seconds = int((now - frozenFrom) / 1000);
            crash::write(m_config.recordPath, record);
            written = now;
        }
    }
}

#ifdef _WIN32

QList<crash::Frame> HangWatchdog::captureStack()
{
#if defined(_M_X64)
    Target &t = *m_target;
    if (!t.thread || SuspendThread(t.thread) == DWORD(-1))
        return {};
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    const bool ok = GetThreadContext(t.thread, &context);
    const quintptr top = context.Rsp;
    std::size_t size = 0;
    if (ok && top < t.stackBase) {
        size = std::min<std::size_t>(t.stackBase - top, t.copy.size());
        std::memcpy(t.copy.data(), reinterpret_cast<const void *>(top), size);
    }
    ResumeThread(t.thread);
    if (!ok || size < 8)
        return {};
    const quintptr copyAt = reinterpret_cast<quintptr>(t.copy.data());
    const auto rebase = [&](DWORD64 &value) {
        if (value >= top && value < top + size)
            value = value - top + copyAt;
    };
    for (std::size_t at = 0; at + 8 <= size; at += 8)
        rebase(*reinterpret_cast<DWORD64 *>(t.copy.data() + at));
    for (DWORD64 *reg : {&context.Rsp, &context.Rbp, &context.Rax, &context.Rbx, &context.Rcx, &context.Rdx,
                         &context.Rsi, &context.Rdi, &context.R8, &context.R9, &context.R10, &context.R11,
                         &context.R12, &context.R13, &context.R14, &context.R15})
        rebase(*reg);
    const auto inCopy = [&](DWORD64 at) { return at >= copyAt && at + 8 <= copyAt + size; };
    quintptr addresses[crash::kMaxFrames];
    int n = 0;
    while (n < crash::kMaxFrames && context.Rip && inCopy(context.Rsp)) {
        addresses[n++] = context.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) {
            // A leaf: its return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
            context.Rsp += 8;
            continue;
        }
        void *handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher,
                         nullptr);
    }
    return crash::framesOf(addresses, n);
#else
    return {};
#endif
}

#else

QList<crash::Frame> HangWatchdog::captureStack()
{
    g_count.store(-1);
    if (pthread_kill(m_target->thread, stackSignal()) != 0)
        return {};
    for (int i = 0; i < 200 && g_count.load() < 0; ++i)
        QThread::msleep(5);
    const int n = g_count.load();
    if (n <= 2)
        return {};
    // The first two are the handler and the signal trampoline.
    quintptr addresses[kRoom];
    for (int i = 2; i < n; ++i)
        addresses[i - 2] = reinterpret_cast<quintptr>(g_stack[i]);
    return crash::framesOf(addresses, n - 2);
}

#endif

}
