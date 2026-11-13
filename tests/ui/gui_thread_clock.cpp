#include "gui_thread_clock.h"

#ifdef _WIN32
#include <cstdint>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

struct GuiThreadClock::Thread {
#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    clockid_t clock{};
    bool ok = false;
#endif
};

GuiThreadClock::GuiThreadClock()
    : m_thread(std::make_unique<Thread>())
{
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_thread->handle,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
#else
    m_thread->ok = pthread_getcpuclockid(pthread_self(), &m_thread->clock) == 0;
#endif
}

GuiThreadClock::~GuiThreadClock()
{
#ifdef _WIN32
    if (m_thread->handle)
        CloseHandle(m_thread->handle);
#endif
}

double GuiThreadClock::ms() const
{
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!m_thread->handle || !GetThreadTimes(m_thread->handle, &created, &exited, &kernel, &user))
        return -1;
    auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10'000; // 100 ns units
#else
    timespec t{};
    if (!m_thread->ok || clock_gettime(m_thread->clock, &t) != 0)
        return -1;
    return static_cast<double>(t.tv_sec) * 1000 + static_cast<double>(t.tv_nsec) / 1e6;
#endif
}
